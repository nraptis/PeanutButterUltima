#include "Decode/DecodeFlight.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "AppShell_ArchiveFormat.hpp"
#include "Decode/DecodeDiscovery.hpp"
#include "Decode/LogicalRecordDecoder.hpp"

namespace peanutbutter {
namespace decode_internal {
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

const char* DecodeModeName(bool pRecoverMode) {
  return pRecoverMode ? "Recover" : "Unbundle";
}

void ReportDecodeCryptStageProgress(Logger& pLogger,
                                    bool pRecoverMode,
                                    CryptGenerationStage pStage,
                                    double pStageFraction) {
  switch (pStage) {
    case CryptGenerationStage::kExpansion:
      ReportProgress(pLogger,
                     DecodeModeName(pRecoverMode),
                     ProgressProfileKind::kUnbundle,
                     ProgressPhase::kExpansion,
                     pStageFraction,
                     "Expanding crypt tables.");
      break;
    case CryptGenerationStage::kLayerCake:
      ReportProgress(pLogger,
                     DecodeModeName(pRecoverMode),
                     ProgressProfileKind::kUnbundle,
                     ProgressPhase::kLayerCake,
                     pStageFraction,
                     "Building layer cake.");
      break;
  }
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
  aResult.mFailureMessage = "Decode canceled by user.";
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

struct Cursor {
  std::uint32_t mArchiveIndex = 0u;
  std::uint32_t mBlockIndex = 0u;
  std::size_t mPayloadOffset = kRecoveryHeaderLength;
};

bool IsFirstArchiveBlock(const Cursor& pCursor) {
  return pCursor.mArchiveIndex == 0u && pCursor.mBlockIndex == 0u;
}

bool ResolveStrideJump(const Cursor& pCurrent,
                       const SkipRecord& pSkip,
                       std::uint32_t pBlocksPerArchive,
                       const std::unordered_map<std::uint32_t, ArchiveCandidate>& pByIndex,
                       Cursor& pOutTarget) {
  if (pBlocksPerArchive == 0u || pBlocksPerArchive > kMaxBlocksPerArchive) {
    return false;
  }
  if (pSkip.mBlockDistance >= pBlocksPerArchive) {
    return false;
  }
  if (static_cast<std::size_t>(pSkip.mByteDistance) >= kPayloadBytesPerL3) {
    return false;
  }
  if (pSkip.mArchiveDistance == 0u && pSkip.mBlockDistance == 0u &&
      pSkip.mByteDistance == 0u) {
    return false;
  }

  const std::uint64_t aDeltaBlocks =
      static_cast<std::uint64_t>(pSkip.mArchiveDistance) *
          static_cast<std::uint64_t>(pBlocksPerArchive) +
      static_cast<std::uint64_t>(pSkip.mBlockDistance);

  const std::uint64_t aCurrentGlobalBlock =
      static_cast<std::uint64_t>(pCurrent.mArchiveIndex) *
          static_cast<std::uint64_t>(pBlocksPerArchive) +
      static_cast<std::uint64_t>(pCurrent.mBlockIndex);
  const std::uint64_t aTargetGlobalBlock = aCurrentGlobalBlock + aDeltaBlocks;
  const std::uint64_t aTargetPayloadOffset =
      static_cast<std::uint64_t>(pSkip.mByteDistance);

  const std::uint32_t aTargetArchiveIndex = static_cast<std::uint32_t>(
      aTargetGlobalBlock / static_cast<std::uint64_t>(pBlocksPerArchive));
  const std::uint32_t aTargetBlockIndex = static_cast<std::uint32_t>(
      aTargetGlobalBlock % static_cast<std::uint64_t>(pBlocksPerArchive));
  const std::size_t aTargetOffsetInBlock =
      static_cast<std::size_t>(kRecoveryHeaderLength + aTargetPayloadOffset);
  if (aTargetOffsetInBlock >= kBlockSizeL3) {
    return false;
  }

  const auto aArchiveIt = pByIndex.find(aTargetArchiveIndex);
  if (aArchiveIt == pByIndex.end()) {
    return false;
  }
  if (aTargetBlockIndex >= aArchiveIt->second.mBlockCount) {
    return false;
  }

  pOutTarget.mArchiveIndex = aTargetArchiveIndex;
  pOutTarget.mBlockIndex = aTargetBlockIndex;
  pOutTarget.mPayloadOffset = aTargetOffsetInBlock;
  return true;
}

}  // namespace

OperationResult RunDecodeCore(const UnbundleRequest& pRequest,
                              const std::vector<std::string>& pArchiveFileList,
                              bool pRecoverMode,
                              FileSystem& pFileSystem,
                              Logger& pLogger,
                              CancelCoordinator* pCancelCoordinator) {
  const std::string aModeName = DecodeModeName(pRecoverMode);
  ReportProgress(pLogger,
                 aModeName,
                 ProgressProfileKind::kUnbundle,
                 ProgressPhase::kPreflight,
                 1.0,
                 aModeName + " preflight complete.");
  if (pRequest.mDestinationDirectory.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "destination directory is required.",
                       pLogger);
  }
  if (!pFileSystem.EnsureDirectory(pRequest.mDestinationDirectory)) {
    return MakeFailure(ErrorCode::kFileSystem,
                       "failed to create decode destination directory.",
                       pLogger);
  }

  DiscoverySelection aSelection;
  if (!SelectArchiveFamily(pArchiveFileList,
                           aModeName,
                           pRecoverMode,
                           pFileSystem,
                           pLogger,
                           aSelection,
                           pCancelCoordinator)) {
    if (pCancelCoordinator != nullptr &&
        pCancelCoordinator->IsCancelRequested()) {
      return MakeCanceled();
    }
    return MakeFailure(
        ErrorCode::kInvalidRequest,
        "decode discovery could not find a usable archive family.",
        pLogger);
  }

  if (aSelection.mArchives.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "decode plan contains no archives.",
                       pLogger);
  }

  if (!pRecoverMode) {
    for (const ArchiveCandidate& aCandidate : aSelection.mArchives) {
      if (!aCandidate.mHasReadableHeader) {
        continue;
      }
      if (aCandidate.mHeader.mDirtyType == DirtyType::kInvalid) {
        return MakeFailure(
            ErrorCode::kArchiveHeader,
            "archive header finalization did not complete; use recover for invalid archives.",
            pLogger);
      }
    }
  }

  std::unique_ptr<Crypt> aGeneratedCrypt;
  if (pRequest.mUseEncryption) {
    EncryptionStrength aEncryptionStrength = EncryptionStrength::kHigh;
    std::string aStrengthError;
    if (!ResolveSelectionEncryptionStrength(
            aSelection, aEncryptionStrength, aStrengthError)) {
      return MakeFailure(
          ErrorCode::kArchiveHeader,
          aStrengthError.empty()
              ? "failed resolving archive encryption strength."
              : aStrengthError,
          pLogger);
    }
    CryptGeneratorRequest aCryptRequest;
    aCryptRequest.mEncryptionStrength = aEncryptionStrength;
    aCryptRequest.mPasswordOne = pRequest.mPasswordOne;
    aCryptRequest.mPasswordTwo = pRequest.mPasswordTwo;
    aCryptRequest.mUseEncryption = pRequest.mUseEncryption;
    aCryptRequest.mRecoverMode = pRecoverMode;
    aCryptRequest.mLogStatus = [&pLogger](const std::string& pMessage) {
      pLogger.LogStatus(pMessage);
    };
    aCryptRequest.mReportProgress =
        [&pLogger, pRecoverMode](CryptGenerationStage pStage,
                                 double pStageFraction) {
          ReportDecodeCryptStageProgress(
              pLogger, pRecoverMode, pStage, pStageFraction);
        };
    std::string aCryptError;
    aGeneratedCrypt =
        CreateRequestedCrypt(pRequest.mCryptGenerator, aCryptRequest, &aCryptError);
    if (aGeneratedCrypt == nullptr) {
      return MakeFailure(ErrorCode::kCrypt,
                         aCryptError.empty() ? "failed creating decode crypt."
                                             : aCryptError,
                         pLogger);
    }
  } else {
    ReportProgress(pLogger,
                   aModeName,
                   ProgressProfileKind::kUnbundle,
                   ProgressPhase::kExpansion,
                   1.0,
                   "Encryption disabled.");
    ReportProgress(pLogger,
                   aModeName,
                   ProgressProfileKind::kUnbundle,
                   ProgressPhase::kLayerCake,
                   1.0,
                   "Encryption disabled.");
  }

  std::unordered_map<std::uint32_t, ArchiveCandidate> aByIndex;
  aByIndex.reserve(aSelection.mArchives.size());
  for (const ArchiveCandidate& aCandidate : aSelection.mArchives) {
    aByIndex[aCandidate.mArchiveIndex] = aCandidate;
  }

  std::uint32_t aBlocksPerArchive = 0u;
  for (const ArchiveCandidate& aCandidate : aSelection.mArchives) {
    if (aCandidate.mHasReadableHeader &&
        (aCandidate.mHeader.mPayloadLength % kBlockSizeL3) == 0u &&
        aCandidate.mHeader.mPayloadLength > 0u) {
      aBlocksPerArchive = static_cast<std::uint32_t>(
          aCandidate.mHeader.mPayloadLength / kBlockSizeL3);
      break;
    }
  }
  if (aBlocksPerArchive == 0u) {
    aBlocksPerArchive = aSelection.mArchives.front().mBlockCount;
  }
  if (aBlocksPerArchive == 0u) {
    aBlocksPerArchive = 1u;
  }
  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(aBlocksPerArchive) *
      static_cast<std::uint64_t>(kPayloadBytesPerL3);
  for (ArchiveFileBox& aBox : aSelection.mArchiveBoxes) {
    aBox.mPayloadStart =
        static_cast<std::uint64_t>(aBox.mSequenceJumber) *
        aPayloadBytesPerArchive;
    if (aBox.mEmpty) {
      aBox.mPayloadLength = aPayloadBytesPerArchive;
      continue;
    }
    const auto aCandidateIt = aByIndex.find(aBox.mSequenceJumber);
    if (aCandidateIt != aByIndex.end()) {
      aBox.mPayloadLength =
          static_cast<std::uint64_t>(aCandidateIt->second.mBlockCount) *
          static_cast<std::uint64_t>(kPayloadBytesPerL3);
    } else {
      aBox.mEmpty = true;
      aBox.mPayloadLength = aPayloadBytesPerArchive;
    }
  }

  pLogger.LogStatus("[Decode][Flight] Unpacked 0B from 0 archives into 0 items START.");

  LogicalRecordDecoder aParser(pRequest.mDestinationDirectory,
                               pFileSystem,
                               pLogger,
                               pCancelCoordinator);
  BlockBuffer aEncrypted;
  BlockBuffer aPlain;
  BlockBuffer aWorker;

  std::uint64_t aFailedBlocks = 0u;
  std::uint64_t aJumpCount = 0u;
  std::uint64_t aGapBoxesEncountered = 0u;
  std::uint64_t aNextByteLog =
      std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
  const auto aStart = std::chrono::steady_clock::now();
  bool aLoggedCancelFinishFile = false;
  bool aLoggedCancelTimeout = false;
  bool aLoggedCancelClosure = false;

  const std::string aCancelUnexpectedEndLog =
      "[Cancel] Cancel terminated, unexpected end of file.";
  const std::string aCancelDecodeErrorLog =
      "[Cancel] Cancel terminated due to decode error.";

  const auto aCleanupDecodeState = [&]() {
    aParser.AbortPartialFile();
    aParser.ResetRecordState();
  };
  const auto aLogCancelClosure = [&](const std::string& pMessage) {
    if (aLoggedCancelClosure || pCancelCoordinator == nullptr) {
      return;
    }
    if (!pMessage.empty()) {
      pLogger.LogStatus(pMessage);
    }
    pCancelCoordinator->LogEndingJob();
    pCancelCoordinator->LogModeCancelled(aModeName);
    aLoggedCancelClosure = true;
  };
  const auto aReturnCanceled =
      [&](const std::string& pStatusMessage = std::string()) -> OperationResult {
    aCleanupDecodeState();
    aLogCancelClosure(pStatusMessage);
    return MakeCanceled();
  };
  const auto aReturnFailure =
      [&](ErrorCode pCode,
          const std::string& pMessage,
          const std::string& pCancelStatusMessage =
              std::string()) -> OperationResult {
    aCleanupDecodeState();
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested()) {
      aLogCancelClosure(pCancelStatusMessage.empty()
                            ? aCancelDecodeErrorLog
                            : pCancelStatusMessage);
      return MakeCanceled();
    }
    return MakeFailure(pCode, pMessage, pLogger);
  };
  const auto aLogMissingTailFromHeaderCount = [&]() {
    if (!aSelection.mHasHeaderDeclaredMaxIndex ||
        aSelection.mHeaderDeclaredMaxIndex <= aSelection.mMaxIndex) {
      return;
    }
    const std::size_t aTailGaps = static_cast<std::size_t>(
        aSelection.mHeaderDeclaredMaxIndex - aSelection.mMaxIndex);
    pLogger.LogStatus("[Decode][Discovery] Header-declared archive count implies " +
                      std::to_string(aTailGaps) +
                      " missing tail archive indices.");
  };

  Cursor aCursor{};
  aCursor.mArchiveIndex = aSelection.mMinIndex;
  aCursor.mBlockIndex = 0u;
  aCursor.mPayloadOffset = kRecoveryHeaderLength;
  const std::uint64_t aArchiveSpanCount =
      static_cast<std::uint64_t>(aSelection.mMaxIndex - aSelection.mMinIndex) +
      1u;
  const std::uint64_t aTotalDecodeBlocksEstimate =
      aArchiveSpanCount * static_cast<std::uint64_t>(aBlocksPerArchive);
  const auto aCountArchivesScanned =
      [&](const Cursor& pProgressCursor) -> std::uint64_t {
    if (pProgressCursor.mArchiveIndex < aSelection.mMinIndex) {
      return 0u;
    }
    const std::uint64_t aCount =
        static_cast<std::uint64_t>(pProgressCursor.mArchiveIndex -
                                   aSelection.mMinIndex) +
        1u;
    return std::min<std::uint64_t>(aCount, aArchiveSpanCount);
  };
  const auto aFormatDecodeProgressPercent =
      [&](const Cursor& pProgressCursor) -> std::string {
    if (aTotalDecodeBlocksEstimate == 0u) {
      return std::string("0.000");
    }
    const std::uint64_t aArchiveDistance =
        (pProgressCursor.mArchiveIndex >= aSelection.mMinIndex)
            ? static_cast<std::uint64_t>(pProgressCursor.mArchiveIndex -
                                         aSelection.mMinIndex)
            : 0u;
    const std::uint64_t aCurrentBlocks = std::min<std::uint64_t>(
        aTotalDecodeBlocksEstimate,
        (aArchiveDistance * static_cast<std::uint64_t>(aBlocksPerArchive)) +
            static_cast<std::uint64_t>(pProgressCursor.mBlockIndex));
    return FormatPercent(aCurrentBlocks, aTotalDecodeBlocksEstimate);
  };
  const std::uint64_t aTotalPayloadBytesEstimate =
      aArchiveSpanCount * static_cast<std::uint64_t>(kPayloadBytesPerL3) *
      static_cast<std::uint64_t>(aBlocksPerArchive);
  const auto aComputeDecodePayloadProgressBytes =
      [&](const Cursor& pProgressCursor) -> std::uint64_t {
    if (pProgressCursor.mArchiveIndex < aSelection.mMinIndex) {
      return 0u;
    }
    const std::uint64_t aArchiveDistance =
        static_cast<std::uint64_t>(pProgressCursor.mArchiveIndex -
                                   aSelection.mMinIndex);
    const std::uint64_t aBlockOffset =
        static_cast<std::uint64_t>(pProgressCursor.mBlockIndex) *
        static_cast<std::uint64_t>(kPayloadBytesPerL3);
    const std::uint64_t aPayloadOffset =
        pProgressCursor.mPayloadOffset > kRecoveryHeaderLength
            ? static_cast<std::uint64_t>(pProgressCursor.mPayloadOffset -
                                         kRecoveryHeaderLength)
            : 0u;
    const std::uint64_t aProgressBytes =
        (aArchiveDistance * aPayloadBytesPerArchive) + aBlockOffset +
        aPayloadOffset;
    return std::min<std::uint64_t>(aProgressBytes, aTotalPayloadBytesEstimate);
  };
  ReportProgress(pLogger,
                 aModeName,
                 ProgressProfileKind::kUnbundle,
                 ProgressPhase::kFlight,
                 0.0,
                 "Decoding archive payloads.");

  while (aCursor.mArchiveIndex <= aSelection.mMaxIndex) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested()) {
      if (aParser.IsInsideFile()) {
        aParser.RequestStopAfterCurrentFile();
        if (!aLoggedCancelFinishFile) {
          pLogger.LogStatus("[Cancel] Finishing current file before stopping.");
          aLoggedCancelFinishFile = true;
        }
        if (pCancelCoordinator->ShouldCancelNow()) {
          if (!aLoggedCancelTimeout) {
            pLogger.LogStatus("[Cancel] Timed out waiting to finish the current file; stopping decode now.");
            aLoggedCancelTimeout = true;
          }
          return aReturnCanceled();
        }
      } else if (pCancelCoordinator->ShouldCancelNow()) {
        return aReturnCanceled();
      }
    }

    ReportProgress(
        pLogger,
        aModeName,
        ProgressProfileKind::kUnbundle,
        ProgressPhase::kFlight,
        aTotalPayloadBytesEstimate == 0u
            ? 1.0
            : (static_cast<double>(aComputeDecodePayloadProgressBytes(aCursor)) /
               static_cast<double>(aTotalPayloadBytesEstimate)),
        "Decoding archive payloads.");

    if (aCursor.mArchiveIndex < aSelection.mMinIndex ||
        aCursor.mArchiveIndex > aSelection.mMaxIndex) {
      break;
    }
    const std::size_t aBoxOffset = static_cast<std::size_t>(
        aCursor.mArchiveIndex - aSelection.mMinIndex);
    if (aBoxOffset >= aSelection.mArchiveBoxes.size()) {
      break;
    }
    const ArchiveFileBox& aBox = aSelection.mArchiveBoxes[aBoxOffset];
    if (aBox.mEmpty) {
      if (!pRecoverMode) {
        return aReturnFailure(
            ErrorCode::kGap001,
            "encountered empty ArchiveFileBox while decoding (sequence " +
                std::to_string(aCursor.mArchiveIndex) + ").",
            aCancelUnexpectedEndLog);
      }
      ++aGapBoxesEncountered;
      aFailedBlocks += static_cast<std::uint64_t>(aBlocksPerArchive);
      if (aGapBoxesEncountered <= 3u || (aGapBoxesEncountered % 500u) == 0u) {
        pLogger.LogStatus("[Recover] GAP_001 at sequence " +
                          std::to_string(aCursor.mArchiveIndex) +
                          "; skipping empty ArchiveFileBox.");
      }
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mArchiveIndex;
      aCursor.mBlockIndex = 0u;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const auto aArchiveIt = aByIndex.find(aCursor.mArchiveIndex);
    if (aArchiveIt == aByIndex.end()) {
      if (!pRecoverMode) {
        return aReturnFailure(
            ErrorCode::kGap001,
            "encountered empty ArchiveFileBox while decoding (sequence " +
                std::to_string(aCursor.mArchiveIndex) + ").",
            aCancelUnexpectedEndLog);
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mArchiveIndex;
      aCursor.mBlockIndex = 0u;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const ArchiveCandidate& aArchive = aArchiveIt->second;
    if (!pRecoverMode && aSelection.mHasExpectedFamilyId &&
        aArchive.mHasReadableHeader &&
        aArchive.mHeader.mArchiveFamilyId != aSelection.mExpectedFamilyId) {
      return aReturnFailure(
          ErrorCode::kArchiveHeader,
          "archive family id mismatch at sequence " +
              std::to_string(aCursor.mArchiveIndex) + ".");
    }
    if (aCursor.mBlockIndex >= aArchive.mBlockCount) {
      ++aCursor.mArchiveIndex;
      aCursor.mBlockIndex = 0u;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    if (pCancelCoordinator != nullptr && !aParser.IsInsideFile()) {
      pCancelCoordinator->SetReadingPath(aArchive.mPath);
    }
    std::unique_ptr<FileReadStream> aRead =
        pFileSystem.OpenReadStream(aArchive.mPath);
    if (aRead == nullptr || !aRead->IsReady()) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kFileSystem,
                              "failed opening archive for decode: " +
                                  aArchive.mPath);
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const std::size_t aBlockOffset =
        kArchiveHeaderLength +
        (static_cast<std::size_t>(aCursor.mBlockIndex) * kBlockSizeL3);
    if (aBlockOffset + kBlockSizeL3 > aArchive.mFileLength) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kArchiveHeader,
                              "archive is too short or unreadable for block parsing.",
                              aCancelUnexpectedEndLog);
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    if (!aRead->Read(aBlockOffset, aEncrypted.Data(), kBlockSizeL3)) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kFileSystem,
                              "failed reading archive block bytes.");
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }
    if (pCancelCoordinator != nullptr && !aParser.IsInsideFile()) {
      pCancelCoordinator->ClearActivity();
    }

    if (pRequest.mUseEncryption) {
      std::string aCryptError;
      if (!aGeneratedCrypt->UnsealData(aEncrypted.Data(),
                                       aWorker.Data(),
                                       aPlain.Data(),
                                       kBlockSizeL3,
                                       &aCryptError,
                                       CryptMode::kNormal)) {
        if (!pRecoverMode) {
          return aReturnFailure(ErrorCode::kCrypt,
                                aCryptError.empty() ? "decryption failed."
                                                    : aCryptError);
        }
        ++aFailedBlocks;
        aParser.AbortPartialFile();
        aParser.ResetRecordState();
        ++aCursor.mBlockIndex;
        aCursor.mPayloadOffset = kRecoveryHeaderLength;
        continue;
      }
    } else {
      std::memcpy(aPlain.Data(), aEncrypted.Data(), kBlockSizeL3);
    }

    RecoveryHeader aRecoveryHeader{};
    if (!ReadRecoveryHeaderBytes(aPlain.Data(),
                                 kRecoveryHeaderLength,
                                 aRecoveryHeader)) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kArchiveHeader,
                              "failed parsing recovery header.");
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const Checksum aExpectedChecksum =
        ComputeRecoveryChecksum(aPlain.Data(), aRecoveryHeader.mSkip);
    if (!ChecksumsEqual(aExpectedChecksum, aRecoveryHeader.mChecksum)) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kBlockChecksum,
                              "block checksum mismatch.");
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    bool aTerminated = false;
    bool aParseError = false;
    std::string aParseErrorMessage;
    bool aCanceledAtBoundary = false;
    std::uint64_t aDataBytesWritten = 0u;

    if (!aParser.Consume(aPlain.Data(),
                         aCursor.mPayloadOffset,
                         kBlockSizeL3,
                         pRecoverMode,
                         aTerminated,
                         aParseError,
                         aParseErrorMessage,
                         aCanceledAtBoundary,
                         aDataBytesWritten)) {
      return aReturnFailure(ErrorCode::kFileSystem,
                            aParseErrorMessage.empty() ? "decode write failure."
                                                       : aParseErrorMessage);
    }

    while (aParser.BytesWritten() >= aNextByteLog) {
      pLogger.LogStatus("[Decode][Flight] Unpacked " +
                        FormatHumanBytes(aParser.BytesWritten()) + " from " +
                        std::to_string(aCountArchivesScanned(aCursor)) +
                        " archives into " +
                        std::to_string(aParser.FilesWritten() +
                                       aParser.FoldersCreated()) +
                        " items (" + aFormatDecodeProgressPercent(aCursor) +
                        "%).");
      aNextByteLog +=
          std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
    }

    if (aCanceledAtBoundary) {
      return aReturnCanceled();
    }

    if (aTerminated) {
      pLogger.LogStatus("[Decode][Flight] Unpacked " +
                        FormatHumanBytes(aParser.BytesWritten()) + " from " +
                        std::to_string(aCountArchivesScanned(aCursor)) +
                        " archives into " +
                        std::to_string(aParser.FilesWritten() +
                                       aParser.FoldersCreated()) +
                        " items (100.000%) DONE.");

      if (pRecoverMode) {
        const auto aElapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - aStart);
        pLogger.LogStatus("[Recover] Summary: Successfully recovered " +
                          std::to_string(aParser.FilesWritten()) + " files (" +
                          FormatHumanBytes(aParser.BytesWritten()) + ").");
        pLogger.LogStatus("[Recover] Summary: Failed to recover " +
                          std::to_string(aFailedBlocks) + " blocks from " +
                          std::to_string(aSelection.mArchiveBoxes.size()) +
                          " archive boxes.");
        pLogger.LogStatus("[Recover] Summary: Total time was " +
                          FormatHumanDurationSeconds(
                              static_cast<std::uint64_t>(aElapsed.count())) +
                          ".");
        pLogger.LogStatus("[Recover] Summary: Output directory was '" +
                          pRequest.mDestinationDirectory + "'.");
      }
      ReportProgress(pLogger,
                     aModeName,
                     ProgressProfileKind::kUnbundle,
                     ProgressPhase::kFlight,
                     1.0,
                     aModeName + " complete.");
      return MakeSuccess();
    }

    if (aParseError) {
      if (!pRecoverMode) {
        return aReturnFailure(ErrorCode::kRecordParse,
                              aParseErrorMessage.empty() ? "record parse failed."
                                                         : aParseErrorMessage);
      }

      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();

      Cursor aJumpTarget{};
      const SkipRecord aStrideSemanticSkip =
          IsFirstArchiveBlock(aCursor) ? SkipRecord{} : aRecoveryHeader.mSkip;
      if (ResolveStrideJump(aCursor,
                            aStrideSemanticSkip,
                            aBlocksPerArchive,
                            aByIndex,
                            aJumpTarget)) {
        ++aJumpCount;
        if (aJumpCount <= 3u || (aJumpCount % 500u) == 0u) {
          pLogger.LogStatus("[Recover] Honoring recovery stride.");
          pLogger.LogStatus("[Recover] Jumping to '" +
                            aByIndex[aJumpTarget.mArchiveIndex].mFileName +
                            "' at byte " +
                            std::to_string(
                                kArchiveHeaderLength +
                                (aJumpTarget.mBlockIndex *
                                 static_cast<std::uint32_t>(kBlockSizeL3)) +
                                static_cast<std::uint32_t>(
                                    aJumpTarget.mPayloadOffset)) +
                            ".");
        }
        aCursor = aJumpTarget;
        continue;
      }
    }

    ++aCursor.mBlockIndex;
    aCursor.mPayloadOffset = kRecoveryHeaderLength;
  }

  if (pRecoverMode) {
    const auto aElapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - aStart);
    pLogger.LogStatus("[Recover] the rest of the archive is not recoverable.");
    pLogger.LogStatus("[Recover] Summary: Successfully recovered " +
                      std::to_string(aParser.FilesWritten()) + " files (" +
                      FormatHumanBytes(aParser.BytesWritten()) + ").");
    pLogger.LogStatus("[Recover] Summary: Failed to recover " +
                      std::to_string(aFailedBlocks) + " blocks from " +
                      std::to_string(aSelection.mArchiveBoxes.size()) +
                      " archive boxes.");
    pLogger.LogStatus("[Recover] Summary: Total time was " +
                      FormatHumanDurationSeconds(
                          static_cast<std::uint64_t>(aElapsed.count())) +
                      ".");
    pLogger.LogStatus("[Recover] Summary: Output directory was '" +
                      pRequest.mDestinationDirectory + "'.");

    if (aParser.FilesWritten() > 0u) {
      ReportProgress(pLogger,
                     aModeName,
                     ProgressProfileKind::kUnbundle,
                     ProgressPhase::kFlight,
                     1.0,
                     aModeName + " complete.");
      return MakeSuccess();
    }
    return aReturnFailure(ErrorCode::kRecoverExhausted,
                          "recover exhausted archive space without finding a terminator.",
                          aCancelUnexpectedEndLog);
  }

  aLogMissingTailFromHeaderCount();
  return aReturnFailure(ErrorCode::kArchiveHeader,
                        "decode exhausted archive space before logical terminator.",
                        aCancelUnexpectedEndLog);
}

}  // namespace decode_internal
}  // namespace peanutbutter
