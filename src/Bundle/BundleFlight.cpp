#include "Bundle/BundleFlight.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "Archive/ArchiveFinalize.hpp"
#include "Archive/ArchiveSkip.hpp"
#include "Bundle/LogicalRecordEncoder.hpp"

namespace peanutbutter {
namespace bundle_internal {
namespace {

std::unique_ptr<Crypt> CreateRequestedCrypt(const CryptGenerator& pGenerator,
                                            const CryptGeneratorRequest& pRequest,
                                            std::string* pErrorMessage) {
  if (!pGenerator) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "crypt generator is not configured.";
    }
    return {};
  }
  std::unique_ptr<Crypt> aCrypt = pGenerator(pRequest, pErrorMessage);
  if (aCrypt == nullptr && pErrorMessage != nullptr && pErrorMessage->empty()) {
    *pErrorMessage = "crypt generator returned no crypt.";
  }
  return aCrypt;
}

void ReportBundleCryptStageProgress(Logger& pLogger,
                                    CryptGenerationStage pStage,
                                    double pStageFraction) {
  switch (pStage) {
    case CryptGenerationStage::kExpansion:
      ReportProgress(pLogger,
                     "Bundle",
                     ProgressProfileKind::kBundle,
                     ProgressPhase::kExpansion,
                     pStageFraction,
                     "Expanding crypt tables.");
      break;
    case CryptGenerationStage::kLayerCake:
      ReportProgress(pLogger,
                     "Bundle",
                     ProgressProfileKind::kBundle,
                     ProgressPhase::kLayerCake,
                     pStageFraction,
                     "Building layer cake.");
      break;
  }
}

std::string AppendWriteStreamError(const std::string& pBaseMessage,
                                   const FileWriteStream* pWriteStream) {
  if (pWriteStream == nullptr) {
    return pBaseMessage;
  }
  const std::string aDetail = pWriteStream->LastErrorMessage();
  if (aDetail.empty()) {
    return pBaseMessage;
  }
  return pBaseMessage + " (" + aDetail + ")";
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

std::uint64_t NextRecordDistance(const std::vector<std::uint64_t>& pRecordStarts,
                                 std::uint64_t pBlockLogicalStart) {
  if (pRecordStarts.empty()) {
    return 0;
  }
  const auto aIt = std::lower_bound(pRecordStarts.begin(),
                                    pRecordStarts.end(),
                                    pBlockLogicalStart);
  if (aIt == pRecordStarts.end()) {
    return 0;
  }
  return (*aIt >= pBlockLogicalStart) ? (*aIt - pBlockLogicalStart) : 0;
}

std::uint64_t FirstBlockSpoofDistance(
    const std::vector<std::uint64_t>& pRecordStarts,
    std::uint64_t pFallbackDistance) {
  for (const std::uint64_t aRecordStart : pRecordStarts) {
    if (aRecordStart > 0u) {
      return aRecordStart;
    }
  }
  return pFallbackDistance;
}

}  // namespace

OperationResult PerformBundleFlightCore(const BundleRequest& pRequest,
                                        const BundleDiscovery& pDiscovery,
                                        FileSystem& pFileSystem,
                                        Logger& pLogger,
                                        CancelCoordinator* pCancelCoordinator) {
  if (pDiscovery.mArchives.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "bundle discovery did not plan archives.",
                       pLogger);
  }

  if (!pFileSystem.EnsureDirectory(pRequest.mDestinationDirectory)) {
    return MakeFailure(ErrorCode::kFileSystem,
                       "failed to create destination directory.",
                       pLogger);
  }

  pLogger.LogStatus("[Bundle][Flight] Writing " +
                    std::to_string(pDiscovery.mArchives.size()) +
                    " archives to '" + pRequest.mDestinationDirectory +
                    "' START.");

  std::unique_ptr<Crypt> aGeneratedCrypt;
  if (pRequest.mUseEncryption) {
    CryptGeneratorRequest aCryptRequest;
    aCryptRequest.mEncryptionStrength = pRequest.mEncryptionStrength;
    aCryptRequest.mPasswordOne = pRequest.mPasswordOne;
    aCryptRequest.mPasswordTwo = pRequest.mPasswordTwo;
    aCryptRequest.mUseEncryption = pRequest.mUseEncryption;
    aCryptRequest.mLogStatus = [&pLogger](const std::string& pMessage) {
      pLogger.LogStatus(pMessage);
    };
    aCryptRequest.mReportProgress =
        [&pLogger](CryptGenerationStage pStage, double pStageFraction) {
          ReportBundleCryptStageProgress(pLogger, pStage, pStageFraction);
        };
    std::string aCryptError;
    aGeneratedCrypt =
        CreateRequestedCrypt(pRequest.mCryptGenerator, aCryptRequest, &aCryptError);
    if (aGeneratedCrypt == nullptr) {
      return MakeFailure(ErrorCode::kCrypt,
                         aCryptError.empty() ? "failed creating bundle crypt."
                                             : aCryptError,
                         pLogger);
    }
  } else {
    ReportProgress(pLogger,
                   "Bundle",
                   ProgressProfileKind::kBundle,
                   ProgressPhase::kExpansion,
                   1.0,
                   "Encryption disabled.");
    ReportProgress(pLogger,
                   "Bundle",
                   ProgressProfileKind::kBundle,
                   ProgressPhase::kLayerCake,
                   1.0,
                   "Encryption disabled.");
  }

  std::vector<BundleRecordInfo> aRecords;
  aRecords.reserve(pDiscovery.mResolvedEntries.size());
  for (std::size_t aIndex = 0u; aIndex < pDiscovery.mResolvedEntries.size();
       ++aIndex) {
    const SourceEntry& aEntry = pDiscovery.mResolvedEntries[aIndex];
    BundleRecordInfo aRecord;
    aRecord.mSourcePath = aEntry.mSourcePath;
    aRecord.mRelativePath = aEntry.mRelativePath;
    aRecord.mIsDirectory = aEntry.mIsDirectory;
    aRecord.mContentLength = aEntry.mIsDirectory ? 0u : aEntry.mFileLength;
    aRecord.mStartLogicalOffset =
        (aIndex < pDiscovery.mRecordStartLogicalOffsets.size())
            ? pDiscovery.mRecordStartLogicalOffsets[aIndex]
            : 0u;
    aRecords.push_back(std::move(aRecord));
  }

  LogicalRecordEncoder aStream(aRecords, pFileSystem);
  BlockBuffer aPlainBlock;
  BlockBuffer aEncryptedBlock;
  BlockBuffer aWorkerBlock;

  std::uint64_t aLogicalBytesWritten = 0u;
  std::uint64_t aFileBytesWritten = 0u;
  std::uint64_t aNextByteLog =
      std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
  std::size_t aArchivesWritten = 0u;
  std::size_t aArchivesExisting = 0u;
  bool aLoggedCancelFinishFile = false;
  bool aLoggedArchiveFinalize = false;
  bool aHadCancel = false;
  bool aHadError = false;
  ErrorCode aFinalErrorCode = ErrorCode::kNone;
  std::string aFinalFailureMessage;

  const auto aRecordFailure = [&](ErrorCode pCode,
                                  const std::string& pMessage) {
    aHadError = true;
    aFinalErrorCode = pCode;
    aFinalFailureMessage = pMessage;
  };

  const auto aBuildInitialHeader =
      [&](const BundleArchivePlan& pArchive) -> ArchiveHeader {
    return BuildArchiveHeaderForPlan(
        pRequest,
        pDiscovery,
        pArchive,
        static_cast<std::uint32_t>(pDiscovery.mArchives.size()),
        pArchive.mPayloadBytes,
        DirtyType::kInvalid);
  };

  ReportProgress(pLogger,
                 "Bundle",
                 ProgressProfileKind::kBundle,
                 ProgressPhase::kFlight,
                 0.0,
                 "Writing archive payloads.");

  for (const BundleArchivePlan& aArchive : pDiscovery.mArchives) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() &&
        !aStream.IsInsideFile()) {
      if (pCancelCoordinator->ShouldCancelNow()) {
        aHadCancel = true;
        break;
      }
    }

    std::unique_ptr<FileWriteStream> aWrite =
        pFileSystem.OpenWriteStream(aArchive.mArchivePath);
    if (aWrite == nullptr || !aWrite->IsReady()) {
      aRecordFailure(
          ErrorCode::kFileSystem,
          AppendWriteStreamError("failed creating archive file: " +
                                     aArchive.mArchivePath,
                                 aWrite.get()));
      break;
    }
    aArchivesExisting = std::max<std::size_t>(aArchivesExisting,
                                              aArchive.mArchiveOrdinal + 1u);
    if (pCancelCoordinator != nullptr) {
      pCancelCoordinator->SetWritingPath(aArchive.mArchivePath);
    }

    const ArchiveHeader aHeader = aBuildInitialHeader(aArchive);
    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    if (!WriteArchiveHeaderBytes(aHeader, aHeaderBytes, sizeof(aHeaderBytes))) {
      aRecordFailure(ErrorCode::kInternal,
                     "failed serializing archive header.");
      (void)aWrite->Close();
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->ClearActivity();
      }
      break;
    }
    if (!aWrite->Write(aHeaderBytes, sizeof(aHeaderBytes))) {
      aRecordFailure(
          ErrorCode::kFileSystem,
          AppendWriteStreamError("failed writing archive header: " +
                                     aArchive.mArchivePath,
                                 aWrite.get()));
      (void)aWrite->Close();
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->ClearActivity();
      }
      break;
    }

    bool aFinalizeArchiveAfterCancel = false;
    bool aArchiveHadError = false;
    for (std::uint32_t aBlockIndex = 0u; aBlockIndex < aArchive.mBlockCount;
         ++aBlockIndex) {
      if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() &&
          aStream.IsInsideFile()) {
        aHadCancel = true;
        aStream.RequestStopAfterCurrentFile();
        if (!aLoggedCancelFinishFile) {
          pLogger.LogStatus("[Cancel] Finishing current file before stopping.");
          aLoggedCancelFinishFile = true;
        }
        if (pCancelCoordinator->ShouldCancelNow()) {
          aStream.RequestStopImmediately();
          aFinalizeArchiveAfterCancel = true;
          if (!aLoggedArchiveFinalize) {
            pLogger.LogStatus(
                "[Bundle][Flight] Cancel timer expired, finalizing archive file.");
            aLoggedArchiveFinalize = true;
          }
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
        aRecordFailure(ErrorCode::kFileSystem, aFailure);
        aArchiveHadError = true;
        break;
      }
      if (aStream.ReachedStopBoundary()) {
        aHadCancel = true;
        aFinalizeArchiveAfterCancel = true;
        if (!aLoggedArchiveFinalize) {
          pLogger.LogStatus(
              "[Bundle][Flight] Finished writing file, finalizing archive file.");
          aLoggedArchiveFinalize = true;
        }
      }

      aLogicalBytesWritten += aLogicalBytesInBlock;
      aFileBytesWritten += aFileBytesInBlock;

      while (aFileBytesWritten >= aNextByteLog) {
        pLogger.LogStatus("[Bundle][Flight] Packed " +
                          FormatHumanBytes(aFileBytesWritten) + " from " +
                          std::to_string(aStream.PackedItemCount()) +
                          " items into " +
                          std::to_string(aArchive.mArchiveOrdinal + 1u) +
                          " archives (" +
                          FormatPercent(aFileBytesWritten,
                                        pDiscovery.mTotalFileBytes) +
                          "%).");
        aNextByteLog +=
            std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
      }

      const std::uint64_t aBlockLogicalStart =
          (static_cast<std::uint64_t>(aArchive.mArchiveOrdinal) *
               static_cast<std::uint64_t>(aArchive.mBlockCount) +
           static_cast<std::uint64_t>(aBlockIndex)) *
          static_cast<std::uint64_t>(kPayloadBytesPerL3);
      const std::uint64_t aDistanceToNextRecord =
          NextRecordDistance(pDiscovery.mRecordStartLogicalOffsets,
                             aBlockLogicalStart);
      const std::uint64_t aStoredDistance =
          aFinalizeArchiveAfterCancel
              ? 0u
              : ((aArchive.mArchiveOrdinal == 0u && aBlockIndex == 0u)
              ? FirstBlockSpoofDistance(pDiscovery.mRecordStartLogicalOffsets,
                                        aDistanceToNextRecord)
              : aDistanceToNextRecord);

      RecoveryHeader aRecoveryHeader{};
      if (!TryBuildSkipRecord(aStoredDistance,
                              aArchive.mBlockCount,
                              aRecoveryHeader.mSkip)) {
        aRecordFailure(ErrorCode::kInternal,
                       "failed converting record skip into fixed-size skip record.");
        aArchiveHadError = true;
        break;
      }
      aRecoveryHeader.mChecksum =
          ComputeRecoveryChecksum(aPlainBlock.Data(), aRecoveryHeader.mSkip);
      if (!WriteRecoveryHeaderBytes(aRecoveryHeader,
                                    aPlainBlock.Data(),
                                    kRecoveryHeaderLength)) {
        aRecordFailure(ErrorCode::kInternal,
                       "failed serializing recovery header.");
        aArchiveHadError = true;
        break;
      }

      if (pRequest.mUseEncryption) {
        std::string aCryptError;
        if (!aGeneratedCrypt->SealData(aPlainBlock.Data(),
                                       aWorkerBlock.Data(),
                                       aEncryptedBlock.Data(),
                                       kBlockSizeL3,
                                       &aCryptError,
                                       CryptMode::kNormal)) {
          aRecordFailure(ErrorCode::kCrypt,
                         aCryptError.empty() ? "encryption failed."
                                             : aCryptError);
          aArchiveHadError = true;
          break;
        }
      } else {
        std::memcpy(aEncryptedBlock.Data(), aPlainBlock.Data(), kBlockSizeL3);
      }

      if (!aWrite->Write(aEncryptedBlock.Data(), kBlockSizeL3)) {
        aRecordFailure(
            ErrorCode::kFileSystem,
            AppendWriteStreamError("failed writing archive block: " +
                                       aArchive.mArchivePath,
                                   aWrite.get()));
        aArchiveHadError = true;
        break;
      }

      ReportProgress(
          pLogger,
          "Bundle",
          ProgressProfileKind::kBundle,
          ProgressPhase::kFlight,
          pDiscovery.mTotalFileBytes == 0u
              ? 1.0
              : (static_cast<double>(aFileBytesWritten) /
                 static_cast<double>(pDiscovery.mTotalFileBytes)),
          "Writing archive payloads.");
    }

    if (aArchiveHadError) {
      (void)aWrite->Close();
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->ClearActivity();
      }
      break;
    }

    if (!aWrite->Close()) {
      aRecordFailure(
          ErrorCode::kFileSystem,
          AppendWriteStreamError("failed closing archive file: " +
                                     aArchive.mArchivePath,
                                 aWrite.get()));
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->ClearActivity();
      }
      break;
    }
    if (pCancelCoordinator != nullptr) {
      pCancelCoordinator->NoteFinishedWriting(aArchive.mArchivePath);
      pCancelCoordinator->ClearActivity();
    }

    ++aArchivesWritten;
    if (aArchivesWritten %
                static_cast<std::size_t>(
                    std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) ==
            0u ||
        aArchivesWritten == pDiscovery.mArchives.size()) {
      pLogger.LogStatus("[Bundle][Flight] Wrote " +
                        std::to_string(aArchivesWritten) + "/" +
                        std::to_string(pDiscovery.mArchives.size()) +
                        " archives (" + FormatHumanBytes(aFileBytesWritten) +
                        ", " +
                        FormatPercent(aFileBytesWritten,
                                      pDiscovery.mTotalFileBytes) +
                        "%).");
    }

    if (aStream.ReachedStopBoundary()) {
      pLogger.LogStatus(
          "[Bundle][Flight] Archive has been sealed, exiting bundle job.");
      break;
    }
  }

  if (!aHadError && !aHadCancel && !aStream.IsDone()) {
    aRecordFailure(
        ErrorCode::kInternal,
        "bundle finished archives before logical stream terminated.");
  }

  if (!aHadError && !aHadCancel) {
    pLogger.LogStatus("[Bundle][Flight] Writing archives DONE.");
  }

  if (aArchivesExisting > 0u) {
    DirtyType aDirtyType = DirtyType::kFinished;
    if (aHadCancel && aHadError) {
      aDirtyType = DirtyType::kFinishedWithCancelAndError;
    } else if (aHadCancel) {
      aDirtyType = DirtyType::kFinishedWithCancel;
    } else if (aHadError) {
      aDirtyType = DirtyType::kFinishedWithError;
    }

    OperationResult aFinalizationResult = FinalizeArchiveHeaders(
        pDiscovery,
        aArchivesExisting,
        aDirtyType,
        pFileSystem,
        &pLogger,
        pCancelCoordinator);
    if (!aFinalizationResult.mSucceeded) {
      return aFinalizationResult;
    }
  }

  if (aHadCancel) {
    if (pCancelCoordinator != nullptr) {
      pCancelCoordinator->LogEndingJob();
      pCancelCoordinator->LogModeCancelled("Bundle");
    }
    return MakeCanceled();
  }

  if (aHadError) {
    return MakeFailure(aFinalErrorCode, aFinalFailureMessage, pLogger);
  }

  pLogger.LogStatus("[Bundle][Summary] Packed " +
                    std::to_string(pDiscovery.mFileCount) + " files and " +
                    std::to_string(pDiscovery.mFolderCount) + " folders into " +
                    std::to_string(aArchivesWritten) + " archives (" +
                    FormatHumanBytes(aFileBytesWritten) + ").");
  ReportProgress(pLogger,
                 "Bundle",
                 ProgressProfileKind::kBundle,
                 ProgressPhase::kFlight,
                 1.0,
                 "Bundle complete.");
  return MakeSuccess();
}

}  // namespace bundle_internal
}  // namespace peanutbutter
