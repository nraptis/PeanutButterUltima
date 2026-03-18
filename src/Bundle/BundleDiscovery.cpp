#include "Bundle/BundleDiscovery.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace bundle_internal {
namespace {

constexpr std::uint64_t kFnvOffsetBasis64 = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime64 = 1099511628211ULL;

std::uint64_t Fnv1aUpdate(std::uint64_t pState, unsigned char pByte) {
  pState ^= static_cast<std::uint64_t>(pByte);
  pState *= kFnvPrime64;
  return pState;
}

std::uint64_t HashBytes(std::uint64_t pState,
                        const unsigned char* pData,
                        std::size_t pLength) {
  if (pData == nullptr) {
    return pState;
  }
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    pState = Fnv1aUpdate(pState, pData[aIndex]);
  }
  return pState;
}

std::uint64_t HashString(std::uint64_t pState, const std::string& pValue) {
  return HashBytes(pState,
                   reinterpret_cast<const unsigned char*>(pValue.data()),
                   pValue.size());
}

std::uint64_t MixU64(std::uint64_t pValue) {
  pValue ^= (pValue >> 33);
  pValue *= 0xff51afd7ed558ccdULL;
  pValue ^= (pValue >> 33);
  pValue *= 0xc4ceb9fe1a85ec53ULL;
  pValue ^= (pValue >> 33);
  return pValue;
}

std::uint64_t ComputeArchiveFamilyId(const BundleRequest& pRequest,
                                     const BundleDiscovery& pDiscovery) {
  std::uint64_t aState = kFnvOffsetBasis64 ^ 0x9E3779B97F4A7C15ULL;
  aState = HashString(aState, pRequest.mArchivePrefix);
  aState = HashString(aState, pRequest.mSourceStem);
  aState = HashString(aState, pRequest.mArchiveSuffix);
  aState = HashString(aState, pRequest.mDestinationDirectory);

  const std::uint64_t aMeta[] = {
      static_cast<std::uint64_t>(pDiscovery.mArchives.size()),
      static_cast<std::uint64_t>(pDiscovery.mTotalLogicalBytes),
      static_cast<std::uint64_t>(pDiscovery.mTotalFileBytes),
      static_cast<std::uint64_t>(pDiscovery.mFileCount),
      static_cast<std::uint64_t>(pDiscovery.mFolderCount),
      static_cast<std::uint64_t>(pRequest.mArchiveBlockCount),
      pRequest.mUseEncryption ? 1ull : 0ull,
      static_cast<std::uint64_t>(
          static_cast<std::uint8_t>(pRequest.mEncryptionStrength)),
      static_cast<std::uint64_t>(static_cast<std::uint8_t>(
          static_cast<ExpansionStrength>(
              static_cast<std::uint8_t>(pRequest.mEncryptionStrength)))),
  };
  aState = HashBytes(aState,
                     reinterpret_cast<const unsigned char*>(aMeta),
                     sizeof(aMeta));
  return MixU64(aState);
}

OperationResult MakeSuccess() {
  OperationResult aResult;
  aResult.mSucceeded = true;
  aResult.mErrorCode = ErrorCode::kNone;
  return aResult;
}

OperationResult MakeCanceled() {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mCanceled = true;
  aResult.mErrorCode = ErrorCode::kCanceled;
  aResult.mFailureMessage = "Bundle canceled by user.";
  return aResult;
}

OperationResult MakeFailure(ErrorCode pCode,
                            const std::string& pMessage,
                            Logger& pLogger) {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mErrorCode = pCode;
  aResult.mFailureMessage = pMessage;
  pLogger.LogError(std::string(ErrorCodeToString(pCode)) + ": " + pMessage);
  return aResult;
}

}  // namespace

OperationResult DiscoverBundlePlanCore(const BundleRequest& pRequest,
                                       const std::vector<SourceEntry>& pSourceEntries,
                                       FileSystem& pFileSystem,
                                       Logger& pLogger,
                                       BundleDiscovery& pOutDiscovery,
                                       CancelCoordinator* pCancelCoordinator) {
  pOutDiscovery = BundleDiscovery{};
  ReportProgress(pLogger,
                 "Bundle",
                 ProgressProfileKind::kBundle,
                 ProgressPhase::kDiscovery,
                 0.0,
                 "Scanning source entries.");

  if (pRequest.mDestinationDirectory.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "destination directory is required.",
                       pLogger);
  }
  if (!IsValidArchiveBlockCount(pRequest.mArchiveBlockCount)) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "archive block count must be between 1 and " +
                           std::to_string(kMaxBlocksPerArchive) + ".",
                       pLogger);
  }

  pLogger.LogStatus("[Bundle][Discovery] Collecting source entries START.");

  pOutDiscovery.mResolvedEntries = pSourceEntries;
  std::sort(pOutDiscovery.mResolvedEntries.begin(),
            pOutDiscovery.mResolvedEntries.end(),
            [](const SourceEntry& pLeft, const SourceEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  std::uint64_t aTotalFileBytes = 0u;
  std::size_t aFileCount = 0u;
  std::size_t aFolderCount = 0u;
  std::size_t aMissingEntryCount = 0u;
  std::vector<SourceEntry> aExistingEntries;
  aExistingEntries.reserve(pOutDiscovery.mResolvedEntries.size());

  const auto aLogMissingEntry = [&](const std::string& pPath) {
    ++aMissingEntryCount;
    if (aMissingEntryCount <= 100u) {
      pLogger.LogStatus("[Bundle][Discovery] Missing source entry, skipping: '" +
                        pPath + "'.");
    }
  };

  for (std::size_t aIndex = 0u; aIndex < pOutDiscovery.mResolvedEntries.size();
       ++aIndex) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }

    SourceEntry& aEntry = pOutDiscovery.mResolvedEntries[aIndex];
    if (aEntry.mRelativePath.empty()) {
      return MakeFailure(ErrorCode::kInvalidRequest,
                         "source entry has empty relative path.",
                         pLogger);
    }
    if (aEntry.mRelativePath.size() > kMaxPathLength) {
      return MakeFailure(ErrorCode::kInvalidRequest,
                         "source entry path exceeds kMaxPathLength.",
                         pLogger);
    }

    if (!aEntry.mIsDirectory && !pFileSystem.Exists(aEntry.mSourcePath)) {
      aLogMissingEntry(aEntry.mSourcePath);
      continue;
    }

    if (aEntry.mIsDirectory) {
      ++aFolderCount;
      aExistingEntries.push_back(aEntry);
    } else {
      std::unique_ptr<FileReadStream> aRead =
          pFileSystem.OpenReadStream(aEntry.mSourcePath);
      if (aRead == nullptr || !aRead->IsReady()) {
        if (!pFileSystem.Exists(aEntry.mSourcePath)) {
          aLogMissingEntry(aEntry.mSourcePath);
          continue;
        }
        return MakeFailure(ErrorCode::kFileSystem,
                           "could not read source file length: " +
                               aEntry.mSourcePath,
                           pLogger);
      }
      aEntry.mFileLength = static_cast<std::uint64_t>(aRead->GetLength());
      aTotalFileBytes += aEntry.mFileLength;
      ++aFileCount;
      aExistingEntries.push_back(aEntry);
    }

    if ((aIndex + 1u) %
            static_cast<std::size_t>(
                std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) ==
        0u) {
      pLogger.LogStatus("[Bundle][Discovery] Scanned " +
                        std::to_string(aIndex + 1u) + " source entries.");
    }
    if (((aIndex + 1u) % 128u) == 0u ||
        (aIndex + 1u) == pOutDiscovery.mResolvedEntries.size()) {
      ReportProgress(
          pLogger,
          "Bundle",
          ProgressProfileKind::kBundle,
          ProgressPhase::kDiscovery,
          pOutDiscovery.mResolvedEntries.empty()
              ? 1.0
              : (static_cast<double>(aIndex + 1u) /
                 static_cast<double>(pOutDiscovery.mResolvedEntries.size())),
          "Scanning source entries.");
    }
  }

  if (aMissingEntryCount > 100u) {
    pLogger.LogStatus("[Bundle][Discovery] Skipped " +
                      std::to_string(aMissingEntryCount - 100u) +
                      " additional missing source entries beyond the first 100.");
  }

  pOutDiscovery.mResolvedEntries = std::move(aExistingEntries);

  if (pOutDiscovery.mResolvedEntries.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "no source entries remained after skipping missing entries.",
                       pLogger);
  }

  std::vector<std::uint64_t> aRecordStarts;
  aRecordStarts.reserve(pOutDiscovery.mResolvedEntries.size() + 1u);
  std::uint64_t aLogicalOffset = 0u;
  for (const SourceEntry& aEntry : pOutDiscovery.mResolvedEntries) {
    aRecordStarts.push_back(aLogicalOffset);
    const std::uint64_t aRecordOverhead = 2u + 8u;
    if (aEntry.mRelativePath.size() >
        std::numeric_limits<std::uint64_t>::max() - aRecordOverhead) {
      return MakeFailure(ErrorCode::kInternal,
                         "path length overflow during discovery.",
                         pLogger);
    }
    const std::uint64_t aPathLength =
        static_cast<std::uint64_t>(aEntry.mRelativePath.size());
    const std::uint64_t aContentBytes =
        aEntry.mIsDirectory ? 0u : aEntry.mFileLength;
    aLogicalOffset += aRecordOverhead + aPathLength + aContentBytes;
  }
  aRecordStarts.push_back(aLogicalOffset);
  aLogicalOffset += 2u;

  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(pRequest.mArchiveBlockCount) *
      static_cast<std::uint64_t>(kPayloadBytesPerL3);
  if (aPayloadBytesPerArchive == 0u) {
    return MakeFailure(ErrorCode::kInternal,
                       "invalid payload bytes per archive.",
                       pLogger);
  }

  std::uint64_t aArchiveCount64 =
      (aLogicalOffset + aPayloadBytesPerArchive - 1u) / aPayloadBytesPerArchive;
  if (aArchiveCount64 == 0u) {
    aArchiveCount64 = 1u;
  }
  if (aArchiveCount64 > static_cast<std::uint64_t>(kMaxArchiveCount)) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "planned archive count exceeds MAX_ARCHIVE_COUNT (" +
                           std::to_string(kMaxArchiveCount) + ").",
                       pLogger);
  }
  if (aArchiveCount64 >
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return MakeFailure(ErrorCode::kInternal,
                       "archive count overflow.",
                       pLogger);
  }

  const std::size_t aArchiveCount = static_cast<std::size_t>(aArchiveCount64);
  pOutDiscovery.mArchives.reserve(aArchiveCount);

  for (std::size_t aArchiveOrdinal = 0u; aArchiveOrdinal < aArchiveCount;
       ++aArchiveOrdinal) {
    const std::uint64_t aArchiveStartLogical =
        static_cast<std::uint64_t>(aArchiveOrdinal) * aPayloadBytesPerArchive;
    const std::uint64_t aArchiveEndLogical =
        aArchiveStartLogical + aPayloadBytesPerArchive;

    std::uint32_t aRecordCount = 0u;
    std::uint32_t aFolderCountInArchive = 0u;
    for (std::size_t aEntryIndex = 0u;
         aEntryIndex < pOutDiscovery.mResolvedEntries.size();
         ++aEntryIndex) {
      if (aRecordStarts[aEntryIndex] < aArchiveStartLogical ||
          aRecordStarts[aEntryIndex] >= aArchiveEndLogical) {
        continue;
      }
      ++aRecordCount;
      if (pOutDiscovery.mResolvedEntries[aEntryIndex].mIsDirectory) {
        ++aFolderCountInArchive;
      }
    }

    BundleArchivePlan aPlan;
    aPlan.mArchiveOrdinal = aArchiveOrdinal;
    aPlan.mArchiveIndex = static_cast<std::uint32_t>(aArchiveOrdinal);
    aPlan.mBlockCount = pRequest.mArchiveBlockCount;
    aPlan.mPayloadBytes = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(pRequest.mArchiveBlockCount) *
        static_cast<std::uint64_t>(kBlockSizeL3));
    aPlan.mRecordCountMod256 = static_cast<std::uint8_t>(aRecordCount & 0xFFu);
    aPlan.mFolderCountMod256 =
        static_cast<std::uint8_t>(aFolderCountInArchive & 0xFFu);
    aPlan.mArchivePath = pFileSystem.JoinPath(
        pRequest.mDestinationDirectory,
        MakeArchiveFileName(pRequest.mArchivePrefix,
                            pRequest.mSourceStem,
                            pRequest.mArchiveSuffix,
                            aArchiveOrdinal,
                            aArchiveCount));
    pOutDiscovery.mArchives.push_back(std::move(aPlan));
  }

  pOutDiscovery.mRecordStartLogicalOffsets = std::move(aRecordStarts);
  pOutDiscovery.mTotalLogicalBytes = aLogicalOffset;
  pOutDiscovery.mTotalFileBytes = aTotalFileBytes;
  pOutDiscovery.mFileCount = aFileCount;
  pOutDiscovery.mFolderCount = aFolderCount;
  pOutDiscovery.mArchiveFamilyId =
      ComputeArchiveFamilyId(pRequest, pOutDiscovery);

  pLogger.LogStatus("[Bundle][Discovery] Found " + std::to_string(aFileCount) +
                    " files and " + std::to_string(aFolderCount) +
                    " empty folders.");
  pLogger.LogStatus("[Bundle][Discovery] Planned " +
                    std::to_string(aArchiveCount) + " archives.");
  pLogger.LogStatus("[Bundle][Discovery] Planned " +
                    std::to_string(aArchiveCount) + " archives, " +
                    std::to_string(
                        pOutDiscovery.mResolvedEntries.size() + 1u) +
                    " records, payload " + FormatHumanBytes(aTotalFileBytes) +
                    " DONE.");
  ReportProgress(pLogger,
                 "Bundle",
                 ProgressProfileKind::kBundle,
                 ProgressPhase::kDiscovery,
                 1.0,
                 "Bundle discovery complete.");
  return MakeSuccess();
}

}  // namespace bundle_internal
}  // namespace peanutbutter
