#include "AppShell_Bundle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace {

struct RecordInfo {
  std::string mSourcePath;
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::uint64_t mContentLength = 0;
  std::uint64_t mStartLogicalOffset = 0;
};

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

OperationResult MakeFailure(ErrorCode pCode, const std::string& pMessage, Logger& pLogger) {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mErrorCode = pCode;
  aResult.mFailureMessage = pMessage;
  pLogger.LogError(std::string(ErrorCodeToString(pCode)) + ": " + pMessage);
  return aResult;
}

std::string RecordDisplayName(const FileSystem& pFileSystem, const std::string& pRelativePath) {
  if (pRelativePath.empty()) {
    return std::string("<unknown>");
  }
  return pFileSystem.StemName(pRelativePath);
}

std::uint64_t NextRecordDistance(const std::vector<std::uint64_t>& pRecordStarts,
                                 std::uint64_t pBlockLogicalStart) {
  if (pRecordStarts.empty()) {
    return 0;
  }
  const auto aIt = std::lower_bound(pRecordStarts.begin(), pRecordStarts.end(), pBlockLogicalStart);
  if (aIt == pRecordStarts.end()) {
    return 0;
  }
  return (*aIt >= pBlockLogicalStart) ? (*aIt - pBlockLogicalStart) : 0;
}

bool TryBuildSkipRecord(std::uint64_t pDistanceBytes,
                        std::uint32_t pBlocksPerArchive,
                        SkipRecord& pOutSkip) {
  pOutSkip = SkipRecord{};
  if (pBlocksPerArchive == 0u || pBlocksPerArchive > kMaxBlocksPerArchive) {
    return false;
  }
  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(pBlocksPerArchive) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
  if (aPayloadBytesPerArchive == 0u) {
    return false;
  }

  const std::uint64_t aArchiveDistance = pDistanceBytes / aPayloadBytesPerArchive;
  const std::uint64_t aArchiveRemainder = pDistanceBytes % aPayloadBytesPerArchive;
  const std::uint64_t aBlockDistance = aArchiveRemainder / static_cast<std::uint64_t>(kPayloadBytesPerL3);
  const std::uint64_t aByteDistance = aArchiveRemainder % static_cast<std::uint64_t>(kPayloadBytesPerL3);

  if (aArchiveDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  if (aBlockDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  if (aByteDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }

  pOutSkip.mArchiveDistance = static_cast<std::uint16_t>(aArchiveDistance);
  pOutSkip.mBlockDistance = static_cast<std::uint16_t>(aBlockDistance);
  pOutSkip.mByteDistance = static_cast<std::uint32_t>(aByteDistance);
  return true;
}

class BundleLogicalStream final {
 public:
  BundleLogicalStream(const std::vector<RecordInfo>& pRecords,
                      FileSystem& pFileSystem,
                      Logger& pLogger)
      : mRecords(pRecords),
        mFileSystem(pFileSystem),
        mLogger(pLogger) {
    StartNextRecord();
  }

  bool Fill(unsigned char* pDestination,
            std::size_t pCapacity,
            std::size_t& pOutBytesWritten,
            std::uint64_t& pOutLogicalBytesWritten,
            std::uint64_t& pOutFileBytesWritten,
            std::string& pOutFailureMessage) {
    pOutBytesWritten = 0u;
    pOutLogicalBytesWritten = 0u;
    pOutFileBytesWritten = 0u;
    pOutFailureMessage.clear();

    if (pDestination == nullptr && pCapacity > 0u) {
      pOutFailureMessage = "null destination payload buffer.";
      return false;
    }

    while (pOutBytesWritten < pCapacity && !mDone) {
      const std::size_t aWritable = pCapacity - pOutBytesWritten;
      switch (mStage) {
        case Stage::kPathLength: {
          while (mPathLengthBytesUsed < 2u && pOutBytesWritten < pCapacity) {
            pDestination[pOutBytesWritten++] = mPathLengthLe[mPathLengthBytesUsed++];
            ++pOutLogicalBytesWritten;
          }
          if (mPathLengthBytesUsed == 2u) {
            if (mCurrentPathLength == 0u) {
              mDone = true;
            } else {
              mStage = Stage::kPathBytes;
            }
          }
          break;
        }

        case Stage::kPathBytes: {
          const std::size_t aRemaining = mCurrentPathLength - mPathBytesUsed;
          const std::size_t aChunk = std::min<std::size_t>(aWritable, aRemaining);
          if (aChunk == 0u) {
            mStage = Stage::kContentLength;
            break;
          }
          std::memcpy(pDestination + pOutBytesWritten,
                      mCurrentRecordRelativePath.data() + mPathBytesUsed,
                      aChunk);
          pOutBytesWritten += aChunk;
          pOutLogicalBytesWritten += static_cast<std::uint64_t>(aChunk);
          mPathBytesUsed += aChunk;
          if (mPathBytesUsed == mCurrentPathLength) {
            mStage = Stage::kContentLength;
          }
          break;
        }

        case Stage::kContentLength: {
          while (mContentLengthBytesUsed < 8u && pOutBytesWritten < pCapacity) {
            pDestination[pOutBytesWritten++] = mContentLengthLe[mContentLengthBytesUsed++];
            ++pOutLogicalBytesWritten;
          }
          if (mContentLengthBytesUsed == 8u) {
            if (mCurrentRecordIsDirectory) {
              FinishRecord();
            } else {
              mStage = Stage::kContentBytes;
            }
          }
          break;
        }

        case Stage::kContentBytes: {
          if (mCurrentRead == nullptr || !mCurrentRead->IsReady()) {
            pOutFailureMessage = "source file stream is not readable.";
            return false;
          }

          const std::size_t aChunk = static_cast<std::size_t>(
              std::min<std::uint64_t>(static_cast<std::uint64_t>(aWritable), mContentBytesRemaining));
          if (aChunk == 0u) {
            FinishRecord();
            break;
          }
          if (!mCurrentRead->Read(mCurrentFileReadOffset, pDestination + pOutBytesWritten, aChunk)) {
            pOutFailureMessage = "failed reading source file bytes.";
            return false;
          }

          pOutBytesWritten += aChunk;
          pOutLogicalBytesWritten += static_cast<std::uint64_t>(aChunk);
          pOutFileBytesWritten += static_cast<std::uint64_t>(aChunk);
          mCurrentFileReadOffset += aChunk;
          mContentBytesRemaining -= static_cast<std::uint64_t>(aChunk);
          if (mContentBytesRemaining == 0u) {
            FinishRecord();
          }
          break;
        }
      }
    }

    return true;
  }

  bool IsDone() const {
    return mDone;
  }

  bool IsInsideFile() const {
    return mStage == Stage::kContentBytes && !mCurrentRecordIsDirectory;
  }

  void RequestStopAfterCurrentFile() {
    if (mStopAfterCurrentFileRequested) {
      return;
    }
    mStopAfterCurrentFileRequested = true;
  }

  bool ReachedStopBoundary() const {
    return mReachedStopBoundary;
  }

 private:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
  };

  void StartNextRecord() {
    mPathLengthBytesUsed = 0u;
    mPathBytesUsed = 0u;
    mContentLengthBytesUsed = 0u;
    mContentBytesRemaining = 0u;
    mCurrentFileReadOffset = 0u;
    mCurrentRead.reset();
    mCurrentRecordRelativePath.clear();
    mCurrentRecordDisplayName.clear();

    if (mRecordIndex >= mRecords.size()) {
      mCurrentPathLength = 0u;
      mPathLengthLe[0] = 0;
      mPathLengthLe[1] = 0;
      mCurrentRecordIsDirectory = false;
      mStage = Stage::kPathLength;
      return;
    }

    const RecordInfo& aRecord = mRecords[mRecordIndex];
    mCurrentRecordRelativePath = aRecord.mRelativePath;
    mCurrentRecordIsDirectory = aRecord.mIsDirectory;
    mCurrentPathLength = static_cast<std::uint16_t>(mCurrentRecordRelativePath.size());
    mPathLengthLe[0] = static_cast<unsigned char>(mCurrentPathLength & 0xFFu);
    mPathLengthLe[1] = static_cast<unsigned char>((mCurrentPathLength >> 8) & 0xFFu);

    std::uint64_t aContentLengthField = aRecord.mIsDirectory ? kDirectoryRecordContentMarker : aRecord.mContentLength;
    for (int aByte = 0; aByte < 8; ++aByte) {
      mContentLengthLe[static_cast<std::size_t>(aByte)] =
          static_cast<unsigned char>((aContentLengthField >> (8 * aByte)) & 0xFFu);
    }
    mContentBytesRemaining = aRecord.mIsDirectory ? 0u : aRecord.mContentLength;

    mCurrentRecordDisplayName = RecordDisplayName(mFileSystem, mCurrentRecordRelativePath);
    if (!mCurrentRecordIsDirectory) {
      mLogger.LogStatus("[Bundle][Flight] Writing file: [[ " + mCurrentRecordDisplayName + " ]].");
      mCurrentRead = mFileSystem.OpenReadStream(aRecord.mSourcePath);
    }

    mStage = Stage::kPathLength;
  }

  void FinishRecord() {
    if (!mCurrentRecordIsDirectory) {
      mLogger.LogStatus("[Bundle][Flight] File packed: [[ " + mCurrentRecordDisplayName + " ]].");
      if (mStopAfterCurrentFileRequested) {
        mReachedStopBoundary = true;
        mDone = true;
        return;
      }
    }

    ++mRecordIndex;
    StartNextRecord();
  }

 private:
  const std::vector<RecordInfo>& mRecords;
  FileSystem& mFileSystem;
  Logger& mLogger;

  std::size_t mRecordIndex = 0u;
  Stage mStage = Stage::kPathLength;
  bool mDone = false;

  std::string mCurrentRecordRelativePath;
  std::string mCurrentRecordDisplayName;
  bool mCurrentRecordIsDirectory = false;
  std::uint16_t mCurrentPathLength = 0u;
  unsigned char mPathLengthLe[2] = {};
  std::size_t mPathLengthBytesUsed = 0u;
  std::size_t mPathBytesUsed = 0u;

  unsigned char mContentLengthLe[8] = {};
  std::size_t mContentLengthBytesUsed = 0u;
  std::uint64_t mContentBytesRemaining = 0u;

  std::unique_ptr<FileReadStream> mCurrentRead;
  std::size_t mCurrentFileReadOffset = 0u;

  bool mStopAfterCurrentFileRequested = false;
  bool mReachedStopBoundary = false;
};

}  // namespace

OperationResult DiscoverBundlePlan(const BundleRequest& pRequest,
                                   const std::vector<SourceEntry>& pSourceEntries,
                                   FileSystem& pFileSystem,
                                   Logger& pLogger,
                                   BundleDiscovery& pOutDiscovery,
                                   CancelCoordinator* pCancelCoordinator) {
  pOutDiscovery = BundleDiscovery{};

  if (pRequest.mDestinationDirectory.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "destination directory is required.", pLogger);
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

  for (std::size_t aIndex = 0u; aIndex < pOutDiscovery.mResolvedEntries.size(); ++aIndex) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }

    SourceEntry& aEntry = pOutDiscovery.mResolvedEntries[aIndex];
    if (aEntry.mRelativePath.empty()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "source entry has empty relative path.", pLogger);
    }
    if (aEntry.mRelativePath.size() > kMaxPathLength) {
      return MakeFailure(ErrorCode::kInvalidRequest, "source entry path exceeds kMaxPathLength.", pLogger);
    }

    if (aEntry.mIsDirectory) {
      ++aFolderCount;
    } else {
      std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(aEntry.mSourcePath);
      if (aRead == nullptr || !aRead->IsReady()) {
        return MakeFailure(ErrorCode::kFileSystem,
                           "could not read source file length: " + aEntry.mSourcePath,
                           pLogger);
      }
      aEntry.mFileLength = static_cast<std::uint64_t>(aRead->GetLength());
      aTotalFileBytes += aEntry.mFileLength;
      ++aFileCount;
    }

    if ((aIndex + 1u) % static_cast<std::size_t>(std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) == 0u) {
      pLogger.LogStatus("[Bundle][Discovery] Scanned " + std::to_string(aIndex + 1u) + " source entries.");
    }
  }

  if (pOutDiscovery.mResolvedEntries.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "source entries are empty.", pLogger);
  }

  std::vector<std::uint64_t> aRecordStarts;
  aRecordStarts.reserve(pOutDiscovery.mResolvedEntries.size() + 1u);
  std::uint64_t aLogicalOffset = 0u;
  for (const SourceEntry& aEntry : pOutDiscovery.mResolvedEntries) {
    aRecordStarts.push_back(aLogicalOffset);
    const std::uint64_t aRecordOverhead = 2u + 8u;
    if (aEntry.mRelativePath.size() > std::numeric_limits<std::uint64_t>::max() - aRecordOverhead) {
      return MakeFailure(ErrorCode::kInternal, "path length overflow during discovery.", pLogger);
    }
    const std::uint64_t aPathLength = static_cast<std::uint64_t>(aEntry.mRelativePath.size());
    const std::uint64_t aContentBytes = aEntry.mIsDirectory ? 0u : aEntry.mFileLength;
    aLogicalOffset += aRecordOverhead + aPathLength + aContentBytes;
  }
  aRecordStarts.push_back(aLogicalOffset);  // terminator start
  aLogicalOffset += 2u;

  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(pRequest.mArchiveBlockCount) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
  if (aPayloadBytesPerArchive == 0u) {
    return MakeFailure(ErrorCode::kInternal, "invalid payload bytes per archive.", pLogger);
  }

  std::uint64_t aArchiveCount64 = (aLogicalOffset + aPayloadBytesPerArchive - 1u) / aPayloadBytesPerArchive;
  if (aArchiveCount64 == 0u) {
    aArchiveCount64 = 1u;
  }
  if (aArchiveCount64 > static_cast<std::uint64_t>(kMaxArchiveCount)) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "planned archive count exceeds MAX_ARCHIVE_COUNT (" +
                           std::to_string(kMaxArchiveCount) + ").",
                       pLogger);
  }
  if (aArchiveCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return MakeFailure(ErrorCode::kInternal, "archive count overflow.", pLogger);
  }

  const std::size_t aArchiveCount = static_cast<std::size_t>(aArchiveCount64);
  pOutDiscovery.mArchives.reserve(aArchiveCount);

  for (std::size_t aArchiveOrdinal = 0u; aArchiveOrdinal < aArchiveCount; ++aArchiveOrdinal) {
    const std::uint64_t aArchiveStartLogical =
        static_cast<std::uint64_t>(aArchiveOrdinal) * aPayloadBytesPerArchive;
    const std::uint64_t aArchiveEndLogical = aArchiveStartLogical + aPayloadBytesPerArchive;

    std::uint32_t aRecordCount = 0u;
    std::uint32_t aFolderCountInArchive = 0u;
    for (std::size_t aEntryIndex = 0u; aEntryIndex < pOutDiscovery.mResolvedEntries.size(); ++aEntryIndex) {
      if (aRecordStarts[aEntryIndex] < aArchiveStartLogical || aRecordStarts[aEntryIndex] >= aArchiveEndLogical) {
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
        static_cast<std::uint64_t>(pRequest.mArchiveBlockCount) * static_cast<std::uint64_t>(kBlockSizeL3));
    aPlan.mRecordCountMod256 = static_cast<std::uint8_t>(aRecordCount & 0xFFu);
    aPlan.mFolderCountMod256 = static_cast<std::uint8_t>(aFolderCountInArchive & 0xFFu);
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

  pLogger.LogStatus("[Bundle][Discovery] Found " + std::to_string(aFileCount) +
                    " files and " + std::to_string(aFolderCount) + " empty folders.");
  pLogger.LogStatus("[Bundle][Discovery] Planned " + std::to_string(aArchiveCount) + " archives.");
  pLogger.LogStatus("[Bundle][Discovery] Planned " + std::to_string(aArchiveCount) +
                    " archives, " + std::to_string(pOutDiscovery.mResolvedEntries.size() + 1u) +
                    " records, payload " + FormatHumanBytes(aTotalFileBytes) + " DONE.");
  return MakeSuccess();
}

OperationResult PerformBundleFlight(const BundleRequest& pRequest,
                                    const BundleDiscovery& pDiscovery,
                                    FileSystem& pFileSystem,
                                    const Crypt& pCrypt,
                                    Logger& pLogger,
                                    CancelCoordinator* pCancelCoordinator) {
  if (pDiscovery.mArchives.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "bundle discovery did not plan archives.", pLogger);
  }

  if (!pFileSystem.EnsureDirectory(pRequest.mDestinationDirectory)) {
    return MakeFailure(ErrorCode::kFileSystem, "failed to create destination directory.", pLogger);
  }

  pLogger.LogStatus("[Bundle][Flight] Writing " + std::to_string(pDiscovery.mArchives.size()) +
                    " archives to '" + pRequest.mDestinationDirectory + "' START.");

  std::vector<RecordInfo> aRecords;
  aRecords.reserve(pDiscovery.mResolvedEntries.size());
  for (std::size_t aIndex = 0u; aIndex < pDiscovery.mResolvedEntries.size(); ++aIndex) {
    const SourceEntry& aEntry = pDiscovery.mResolvedEntries[aIndex];
    RecordInfo aRecord;
    aRecord.mSourcePath = aEntry.mSourcePath;
    aRecord.mRelativePath = aEntry.mRelativePath;
    aRecord.mIsDirectory = aEntry.mIsDirectory;
    aRecord.mContentLength = aEntry.mIsDirectory ? 0u : aEntry.mFileLength;
    aRecord.mStartLogicalOffset =
        (aIndex < pDiscovery.mRecordStartLogicalOffsets.size()) ? pDiscovery.mRecordStartLogicalOffsets[aIndex] : 0u;
    aRecords.push_back(std::move(aRecord));
  }

  BundleLogicalStream aStream(aRecords, pFileSystem, pLogger);
  L3BlockBuffer aPlainBlock{};
  L3BlockBuffer aEncryptedBlock{};
  L3BlockBuffer aWorkerBlock{};

  std::uint64_t aLogicalBytesWritten = 0u;
  std::uint64_t aFileBytesWritten = 0u;
  std::uint64_t aNextByteLog = std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
  std::size_t aArchivesWritten = 0u;
  bool aLoggedCancelFinishFile = false;
  bool aLoggedCancelBoundary = false;

  for (const BundleArchivePlan& aArchive : pDiscovery.mArchives) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() && !aStream.IsInsideFile()) {
      if (pCancelCoordinator->ShouldCancelNow()) {
        pCancelCoordinator->LogEndingJob();
        pCancelCoordinator->LogModeCancelled("Bundle");
        return MakeCanceled();
      }
    }

    std::unique_ptr<FileWriteStream> aWrite = pFileSystem.OpenWriteStream(aArchive.mArchivePath);
    if (aWrite == nullptr || !aWrite->IsReady()) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed creating archive file: " + aArchive.mArchivePath,
                         pLogger);
    }

    ArchiveHeader aHeader{};
    aHeader.mMagic = kMagicHeaderBytes;
    aHeader.mVersionMajor = static_cast<std::uint16_t>(kMajorVersion & 0xFFFFu);
    aHeader.mVersionMinor = static_cast<std::uint16_t>(kMinorVersion & 0xFFFFu);
    aHeader.mArchiveIndex = aArchive.mArchiveIndex;
    aHeader.mArchiveCount = static_cast<std::uint32_t>(pDiscovery.mArchives.size());
    aHeader.mPayloadLength = aArchive.mPayloadBytes;
    aHeader.mRecordCountMod256 = aArchive.mRecordCountMod256;
    aHeader.mFolderCountMod256 = aArchive.mFolderCountMod256;
    aHeader.mReserved16 = 0u;
    aHeader.mReservedA = 0u;
    aHeader.mReservedB = 0u;

    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    if (!WriteArchiveHeaderBytes(aHeader, aHeaderBytes, sizeof(aHeaderBytes))) {
      return MakeFailure(ErrorCode::kInternal, "failed serializing archive header.", pLogger);
    }
    if (!aWrite->Write(aHeaderBytes, sizeof(aHeaderBytes))) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed writing archive header: " + aArchive.mArchivePath,
                         pLogger);
    }

    for (std::uint32_t aBlockIndex = 0u; aBlockIndex < aArchive.mBlockCount; ++aBlockIndex) {
      if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() && aStream.IsInsideFile()) {
        aStream.RequestStopAfterCurrentFile();
        if (!aLoggedCancelFinishFile) {
          pLogger.LogStatus("[Cancel] Finishing current file before stopping.");
          aLoggedCancelFinishFile = true;
        }
      }

      std::memset(aPlainBlock.Data(), 0, kBlockSizeL3);

      std::size_t aPayloadBytesWritten = 0u;
      std::uint64_t aLogicalBytesInBlock = 0u;
      std::uint64_t aFileBytesInBlock = 0u;
      std::string aFailure;
      if (!aStream.Fill(aPlainBlock.Data() + kRecoveryHeaderLength,
                        kPayloadBytesPerL3,
                        aPayloadBytesWritten,
                        aLogicalBytesInBlock,
                        aFileBytesInBlock,
                        aFailure)) {
        return MakeFailure(ErrorCode::kFileSystem, aFailure, pLogger);
      }

      aLogicalBytesWritten += aLogicalBytesInBlock;
      aFileBytesWritten += aFileBytesInBlock;

      while (aFileBytesWritten >= aNextByteLog) {
        pLogger.LogStatus("[Bundle][Flight] Processed " + FormatHumanBytes(aFileBytesWritten) +
                          " across " + std::to_string(aArchivesWritten) + " archives.");
        aNextByteLog += std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
      }

      const std::uint64_t aBlockLogicalStart =
          (static_cast<std::uint64_t>(aArchive.mArchiveOrdinal) *
               static_cast<std::uint64_t>(aArchive.mBlockCount) +
           static_cast<std::uint64_t>(aBlockIndex)) *
          static_cast<std::uint64_t>(kPayloadBytesPerL3);
      const std::uint64_t aDistanceToNextRecord =
          NextRecordDistance(pDiscovery.mRecordStartLogicalOffsets, aBlockLogicalStart);

      RecoveryHeader aRecoveryHeader{};
      if (!TryBuildSkipRecord(aDistanceToNextRecord, aArchive.mBlockCount, aRecoveryHeader.mSkip)) {
        return MakeFailure(ErrorCode::kInternal,
                           "failed converting record skip into fixed-size skip record.",
                           pLogger);
      }
      aRecoveryHeader.mChecksum = ComputeRecoveryChecksum(aPlainBlock.Data(), aRecoveryHeader.mSkip);
      if (!WriteRecoveryHeaderBytes(aRecoveryHeader, aPlainBlock.Data(), kRecoveryHeaderLength)) {
        return MakeFailure(ErrorCode::kInternal, "failed serializing recovery header.", pLogger);
      }

      if (pRequest.mUseEncryption) {
        std::string aCryptError;
        if (!pCrypt.SealData(aPlainBlock.Data(),
                             aWorkerBlock.Data(),
                             aEncryptedBlock.Data(),
                             kBlockSizeL3,
                             &aCryptError,
                             CryptMode::kNormal)) {
          return MakeFailure(ErrorCode::kCrypt,
                             aCryptError.empty() ? "encryption failed." : aCryptError,
                             pLogger);
        }
      } else {
        std::memcpy(aEncryptedBlock.Data(), aPlainBlock.Data(), kBlockSizeL3);
      }

      if (!aWrite->Write(aEncryptedBlock.Data(), kBlockSizeL3)) {
        return MakeFailure(ErrorCode::kFileSystem,
                           "failed writing archive block: " + aArchive.mArchivePath,
                           pLogger);
      }
    }

    if (!aWrite->Close()) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed closing archive file: " + aArchive.mArchivePath,
                         pLogger);
    }

    ++aArchivesWritten;
    if (aArchivesWritten % static_cast<std::size_t>(std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) == 0u ||
        aArchivesWritten == pDiscovery.mArchives.size()) {
      pLogger.LogStatus("[Bundle][Flight] Wrote " + std::to_string(aArchivesWritten) + "/" +
                        std::to_string(pDiscovery.mArchives.size()) + " archives (" +
                        FormatHumanBytes(aFileBytesWritten) + ").");
    }

    if (aStream.ReachedStopBoundary()) {
      if (!aLoggedCancelBoundary) {
        pLogger.LogStatus("[Cancel] Finished current file; finalizing active archive.");
        aLoggedCancelBoundary = true;
      }
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->LogEndingJob();
        pCancelCoordinator->LogModeCancelled("Bundle");
      }
      return MakeCanceled();
    }
  }

  if (!aStream.IsDone()) {
    return MakeFailure(ErrorCode::kInternal,
                       "bundle finished archives before logical stream terminated.",
                       pLogger);
  }

  pLogger.LogStatus("[Bundle][Flight] Writing archives DONE.");
  pLogger.LogStatus("[Bundle][Summary] Wrote " + std::to_string(aArchivesWritten) +
                    " archives and " + FormatHumanBytes(aFileBytesWritten) + ".");
  return MakeSuccess();
}

OperationResult Bundle(const BundleRequest& pRequest,
                       const std::vector<SourceEntry>& pSourceEntries,
                       FileSystem& pFileSystem,
                       const Crypt& pCrypt,
                       Logger& pLogger,
                       CancelCoordinator* pCancelCoordinator) {
  BundleDiscovery aDiscovery;
  OperationResult aResult = DiscoverBundlePlan(pRequest,
                                                pSourceEntries,
                                                pFileSystem,
                                                pLogger,
                                                aDiscovery,
                                                pCancelCoordinator);
  if (!aResult.mSucceeded) {
    return aResult;
  }
  return PerformBundleFlight(pRequest,
                             aDiscovery,
                             pFileSystem,
                             pCrypt,
                             pLogger,
                             pCancelCoordinator);
}

}  // namespace peanutbutter
