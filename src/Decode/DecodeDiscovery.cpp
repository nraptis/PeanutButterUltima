#include "Decode/DecodeDiscovery.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace decode_internal {
namespace {

int DirtyTypeSeverity(DirtyType pDirtyType) {
  switch (pDirtyType) {
    case DirtyType::kInvalid:
      return 5;
    case DirtyType::kFinishedWithCancelAndError:
      return 4;
    case DirtyType::kFinishedWithError:
      return 3;
    case DirtyType::kFinishedWithCancel:
      return 2;
    case DirtyType::kFinished:
      return 1;
  }
  return 0;
}

void LogDiscoveryDirtyTypeStatus(Logger& pLogger,
                                 const DiscoverySelection& pSelection) {
  bool aFoundReadableHeader = false;
  DirtyType aSelectedDirtyType = DirtyType::kFinished;
  int aSelectedSeverity = 0;
  for (const ArchiveCandidate& aArchive : pSelection.mArchives) {
    if (!aArchive.mHasReadableHeader) {
      continue;
    }
    const int aSeverity = DirtyTypeSeverity(aArchive.mHeader.mDirtyType);
    if (!aFoundReadableHeader || aSeverity > aSelectedSeverity) {
      aFoundReadableHeader = true;
      aSelectedDirtyType = aArchive.mHeader.mDirtyType;
      aSelectedSeverity = aSeverity;
    }
  }
  if (!aFoundReadableHeader) {
    return;
  }

  switch (aSelectedDirtyType) {
    case DirtyType::kInvalid:
      pLogger.LogStatus("[Discovery] Warning, this archive was not finalized.");
      return;
    case DirtyType::kFinishedWithError:
      pLogger.LogStatus(
          "[Discovery] Warning, this archive was finalized with errors.");
      return;
    case DirtyType::kFinishedWithCancel:
      pLogger.LogStatus(
          "[Discovery] Warning, this archive was finalized with cancellation.");
      return;
    case DirtyType::kFinishedWithCancelAndError:
      pLogger.LogStatus(
          "[Discovery] Warning, this archive was finalized with errors and cancellation.");
      return;
    case DirtyType::kFinished:
      pLogger.LogStatus(
          "[Discovery] Looks good, this archive is from a completed pack-job.");
      return;
  }
}

void LogDiscoveryComponentVersionWarnings(Logger& pLogger,
                                          const DiscoverySelection& pSelection) {
  bool aArchiverVersionMismatch = false;
  bool aExpanderVersionMismatch = false;
  bool aCipherVersionMismatch = false;

  for (const ArchiveCandidate& aArchive : pSelection.mArchives) {
    if (!aArchive.mHasReadableHeader) {
      continue;
    }
    if (aArchive.mHeader.mArchiverVersion !=
        static_cast<std::uint8_t>(kArchiverVersion & 0xFFu)) {
      aArchiverVersionMismatch = true;
    }
    if (aArchive.mHeader.mPasswordExpanderVersion !=
        static_cast<std::uint8_t>(kPasswordExpanderVersion & 0xFFu)) {
      aExpanderVersionMismatch = true;
    }
    if (aArchive.mHeader.mCipherStackVersion !=
        static_cast<std::uint8_t>(kCipherStackVersion & 0xFFu)) {
      aCipherVersionMismatch = true;
    }
  }

  if (aArchiverVersionMismatch) {
    pLogger.LogStatus(
        "[Discovery] Warning: this archive was created with an incompatible archiver library version");
  }
  if (aExpanderVersionMismatch) {
    pLogger.LogStatus(
        "[Discovery] Warning: this archive was created with an incompatible expander library version");
  }
  if (aCipherVersionMismatch) {
    pLogger.LogStatus(
        "[Discovery] Warning: this archive was created with an incompatible cipher library version");
  }
}

bool CandidateMatchesExpectedFamily(const ArchiveCandidate& pCandidate,
                                    bool pHasExpectedFamilyId,
                                    std::uint64_t pExpectedFamilyId) {
  return pHasExpectedFamilyId && pCandidate.mHasReadableHeader &&
         pCandidate.mHeader.mArchiveFamilyId == pExpectedFamilyId;
}

bool ShouldReplaceCandidate(const ArchiveCandidate& pExisting,
                            const ArchiveCandidate& pIncoming,
                            bool pHasExpectedFamilyId,
                            std::uint64_t pExpectedFamilyId) {
  const bool aExistingMatches = CandidateMatchesExpectedFamily(
      pExisting, pHasExpectedFamilyId, pExpectedFamilyId);
  const bool aIncomingMatches = CandidateMatchesExpectedFamily(
      pIncoming, pHasExpectedFamilyId, pExpectedFamilyId);
  if (aIncomingMatches != aExistingMatches) {
    return aIncomingMatches;
  }
  if (!pExisting.mHasReadableHeader && pIncoming.mHasReadableHeader) {
    return true;
  }
  if (pExisting.mHasReadableHeader && !pIncoming.mHasReadableHeader) {
    return false;
  }
  return pIncoming.mFileLength > pExisting.mFileLength;
}

bool ReadCandidateMetadata(const std::string& pPath,
                           FileSystem& pFileSystem,
                           ArchiveCandidate& pOutCandidate) {
  pOutCandidate = ArchiveCandidate{};
  pOutCandidate.mPath = pPath;
  pOutCandidate.mFileName = pFileSystem.FileName(pPath);

  std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(pPath);
  if (aRead == nullptr || !aRead->IsReady()) {
    return false;
  }
  pOutCandidate.mFileLength = aRead->GetLength();

  std::string aPrefix;
  std::string aSuffix;
  std::uint32_t aIndex = 0u;
  std::size_t aDigits = 0u;
  if (!ParseArchiveFileTemplate(pOutCandidate.mFileName,
                                aPrefix,
                                aIndex,
                                aSuffix,
                                aDigits)) {
    return false;
  }
  pOutCandidate.mArchiveIndex = aIndex;
  pOutCandidate.mDigitWidth = aDigits;

  if (pOutCandidate.mFileLength >= kArchiveHeaderLength) {
    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    if (aRead->Read(0u, aHeaderBytes, sizeof(aHeaderBytes)) &&
        ReadArchiveHeaderBytes(aHeaderBytes,
                               sizeof(aHeaderBytes),
                               pOutCandidate.mHeader)) {
      pOutCandidate.mHasReadableHeader = true;
    }
  }

  if (pOutCandidate.mFileLength > kArchiveHeaderLength) {
    const std::size_t aReadablePayload =
        pOutCandidate.mFileLength - kArchiveHeaderLength;
    pOutCandidate.mBlockCount =
        static_cast<std::uint32_t>(aReadablePayload / kBlockSizeL3);
    if (pOutCandidate.mHasReadableHeader &&
        (pOutCandidate.mHeader.mPayloadLength % kBlockSizeL3) == 0u) {
      const std::uint32_t aHeaderBlocks = static_cast<std::uint32_t>(
          pOutCandidate.mHeader.mPayloadLength / kBlockSizeL3);
      if (aHeaderBlocks > 0u) {
        pOutCandidate.mBlockCount =
            std::min(pOutCandidate.mBlockCount, aHeaderBlocks);
      }
    }
  }
  return true;
}

}  // namespace

bool ResolveSelectionEncryptionStrength(const DiscoverySelection& pSelection,
                                        EncryptionStrength& pOutStrength,
                                        std::string& pOutErrorMessage) {
  bool aFoundReadableHeader = false;
  for (const ArchiveCandidate& aCandidate : pSelection.mArchives) {
    if (!aCandidate.mHasReadableHeader) {
      continue;
    }
    if (!aFoundReadableHeader) {
      pOutStrength = aCandidate.mHeader.mEncryptionStrength;
      aFoundReadableHeader = true;
      continue;
    }
    if (aCandidate.mHeader.mEncryptionStrength != pOutStrength) {
      pOutErrorMessage =
          "selected archive family has mismatched encryption strengths.";
      return false;
    }
  }
  if (!aFoundReadableHeader) {
    pOutErrorMessage =
        "selected archive family did not expose a readable encryption strength.";
    return false;
  }
  return true;
}

bool SelectArchiveFamily(const std::vector<std::string>& pArchiveFileList,
                         const std::string& pModeName,
                         bool pRecoverMode,
                         FileSystem& pFileSystem,
                         Logger& pLogger,
                         DiscoverySelection& pOutSelection,
                         CancelCoordinator* pCancelCoordinator) {
  pOutSelection = DiscoverySelection{};
  ReportProgress(pLogger,
                 pModeName,
                 ProgressProfileKind::kUnbundle,
                 ProgressPhase::kDiscovery,
                 0.0,
                 "Scanning archive candidates.");

  pLogger.LogStatus("[Decode][Discovery] Collecting archive candidates START.");

  std::vector<std::string> aInput = pArchiveFileList;
  std::sort(aInput.begin(), aInput.end());
  aInput.erase(std::unique(aInput.begin(), aInput.end()), aInput.end());

  std::vector<ArchiveCandidate> aParsed;
  aParsed.reserve(aInput.size());
  std::uint64_t aScannedBytes = 0u;

  for (std::size_t aIndex = 0u; aIndex < aInput.size(); ++aIndex) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return false;
    }
    ArchiveCandidate aCandidate;
    if (!ReadCandidateMetadata(aInput[aIndex], pFileSystem, aCandidate)) {
      continue;
    }
    aScannedBytes += static_cast<std::uint64_t>(aCandidate.mFileLength);
    aParsed.push_back(std::move(aCandidate));

    if ((aIndex + 1u) %
            static_cast<std::size_t>(
                std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) ==
        0u) {
      pLogger.LogStatus("[Decode][Discovery] Scanned " +
                        std::to_string(aIndex + 1u) + "/" +
                        std::to_string(aInput.size()) +
                        " candidate archives (" +
                        FormatHumanBytes(aScannedBytes) + ").");
    }
    if (((aIndex + 1u) % 128u) == 0u || (aIndex + 1u) == aInput.size()) {
      ReportProgress(
          pLogger,
          pModeName,
          ProgressProfileKind::kUnbundle,
          ProgressPhase::kDiscovery,
          aInput.empty()
              ? 1.0
              : (static_cast<double>(aIndex + 1u) /
                 static_cast<double>(aInput.size())),
          "Scanning archive candidates.");
    }
  }

  if (aParsed.empty()) {
    pLogger.LogError(
        "[Decode][Discovery] no archive candidates matched filename template parsing.");
    return false;
  }

  pLogger.LogStatus("[Decode][Discovery] Found " +
                    std::to_string(aParsed.size()) + " candidate archives (" +
                    FormatHumanBytes(aScannedBytes) + ").");

  std::string aAnchorPrefix;
  std::string aAnchorSuffix;
  std::uint32_t aAnchorIndex = 0u;
  std::size_t aAnchorDigits = 0u;
  if (!ParseArchiveFileTemplate(aParsed.front().mFileName,
                                aAnchorPrefix,
                                aAnchorIndex,
                                aAnchorSuffix,
                                aAnchorDigits)) {
    return false;
  }

  pLogger.LogStatus("[Decode][Discovery] Filename template anchor: '" +
                    aParsed.front().mFileName + "'.");

  std::unordered_map<std::uint64_t, std::size_t> aFamilyVotes;
  for (const ArchiveCandidate& aCandidate : aParsed) {
    std::string aPrefix;
    std::string aSuffix;
    std::uint32_t aIndex = 0u;
    std::size_t aDigits = 0u;
    if (!ParseArchiveFileTemplate(aCandidate.mFileName,
                                  aPrefix,
                                  aIndex,
                                  aSuffix,
                                  aDigits)) {
      continue;
    }
    if (aPrefix != aAnchorPrefix || aSuffix != aAnchorSuffix) {
      continue;
    }
    if (!aCandidate.mHasReadableHeader) {
      continue;
    }
    ++aFamilyVotes[aCandidate.mHeader.mArchiveFamilyId];
  }

  bool aHasDominantFamily = false;
  std::uint64_t aDominantFamilyId = 0u;
  std::size_t aDominantFamilyVotes = 0u;
  if (!pRecoverMode) {
    for (const auto& aVote : aFamilyVotes) {
      if (aVote.second > aDominantFamilyVotes) {
        aDominantFamilyId = aVote.first;
        aDominantFamilyVotes = aVote.second;
        aHasDominantFamily = true;
      }
    }
  }
  pOutSelection.mHasExpectedFamilyId = aHasDominantFamily;
  pOutSelection.mExpectedFamilyId = aDominantFamilyId;

  std::unordered_map<std::uint32_t, ArchiveCandidate> aByIndex;
  std::size_t aIgnored = 0u;
  std::size_t aFamilyMismatched = 0u;
  for (ArchiveCandidate& aCandidate : aParsed) {
    std::string aPrefix;
    std::string aSuffix;
    std::uint32_t aIndex = 0u;
    std::size_t aDigits = 0u;
    if (!ParseArchiveFileTemplate(aCandidate.mFileName,
                                  aPrefix,
                                  aIndex,
                                  aSuffix,
                                  aDigits)) {
      ++aIgnored;
      continue;
    }
    if (aPrefix != aAnchorPrefix || aSuffix != aAnchorSuffix) {
      ++aIgnored;
      continue;
    }
    if (aHasDominantFamily && aCandidate.mHasReadableHeader &&
        aCandidate.mHeader.mArchiveFamilyId != aDominantFamilyId) {
      ++aFamilyMismatched;
    }
    auto aExisting = aByIndex.find(aIndex);
    if (aExisting == aByIndex.end()) {
      aByIndex.emplace(aIndex, std::move(aCandidate));
      continue;
    }
    if (ShouldReplaceCandidate(aExisting->second,
                               aCandidate,
                               aHasDominantFamily,
                               aDominantFamilyId)) {
      aExisting->second = std::move(aCandidate);
    }
  }

  if (aByIndex.empty()) {
    pLogger.LogError(
        "[Decode][Discovery] anchor template did not retain any archives.");
    return false;
  }

  std::vector<std::uint32_t> aIndexes;
  aIndexes.reserve(aByIndex.size());
  for (const auto& aPair : aByIndex) {
    aIndexes.push_back(aPair.first);
  }
  std::sort(aIndexes.begin(), aIndexes.end());

  const std::uint32_t aWindowMin = aIndexes.front();
  const std::uint64_t aWindowMax64 =
      static_cast<std::uint64_t>(aWindowMin) +
      static_cast<std::uint64_t>(kMaxArchiveCount) - 1u;
  const std::uint32_t aWindowMax = static_cast<std::uint32_t>(std::min(
      aWindowMax64,
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));

  pOutSelection.mMinIndex = aWindowMin;
  pOutSelection.mMaxIndex = aWindowMin;
  pOutSelection.mGapCount = 0u;
  pOutSelection.mClippedArchiveCount = 0u;

  std::uint32_t aPreviousSelected = aWindowMin;
  bool aHasPrevious = false;
  pOutSelection.mArchives.reserve(std::min<std::size_t>(
      aIndexes.size(), static_cast<std::size_t>(kMaxArchiveCount)));
  for (std::uint32_t aIndex : aIndexes) {
    if (aIndex < aWindowMin || aIndex > aWindowMax) {
      ++pOutSelection.mClippedArchiveCount;
      continue;
    }
    if (aHasPrevious && aIndex > aPreviousSelected + 1u) {
      pOutSelection.mGapCount +=
          static_cast<std::size_t>(aIndex - aPreviousSelected - 1u);
    }
    aPreviousSelected = aIndex;
    aHasPrevious = true;
    pOutSelection.mMaxIndex =
        std::max<std::uint32_t>(pOutSelection.mMaxIndex, aIndex);
    pOutSelection.mArchives.push_back(std::move(aByIndex[aIndex]));
  }

  if (pOutSelection.mArchives.empty()) {
    pLogger.LogError(
        "[Decode][Discovery] no archives remained after MAX_ARCHIVE_COUNT clipping.");
    return false;
  }

  std::unordered_map<std::uint32_t, std::size_t> aHeaderDeclaredMaxVotes;
  for (const ArchiveCandidate& aArchive : pOutSelection.mArchives) {
    if (!aArchive.mHasReadableHeader) {
      continue;
    }
    if (aArchive.mHeader.mArchiveCount == 0u) {
      continue;
    }

    const std::uint64_t aDeclaredMax64 =
        static_cast<std::uint64_t>(aArchive.mArchiveIndex) +
        static_cast<std::uint64_t>(aArchive.mHeader.mArchiveCount) - 1u;
    const std::uint32_t aDeclaredMaxClamped = static_cast<std::uint32_t>(std::min(
        aDeclaredMax64,
        static_cast<std::uint64_t>(aWindowMax)));
    ++aHeaderDeclaredMaxVotes[aDeclaredMaxClamped];
  }

  std::uint32_t aHeaderDeclaredMax = pOutSelection.mMaxIndex;
  std::size_t aHeaderDeclaredMaxVoteCount = 0u;
  for (const auto& aVote : aHeaderDeclaredMaxVotes) {
    if (aVote.second > aHeaderDeclaredMaxVoteCount ||
        (aVote.second == aHeaderDeclaredMaxVoteCount &&
         aVote.first > aHeaderDeclaredMax)) {
      aHeaderDeclaredMax = aVote.first;
      aHeaderDeclaredMaxVoteCount = aVote.second;
    }
  }
  if (aHeaderDeclaredMaxVoteCount > 0u) {
    pOutSelection.mHasHeaderDeclaredMaxIndex = true;
    pOutSelection.mHeaderDeclaredMaxIndex = aHeaderDeclaredMax;
  }

  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(kPayloadBytesPerL3);
  pOutSelection.mArchiveBoxes.reserve(
      static_cast<std::size_t>(pOutSelection.mMaxIndex - pOutSelection.mMinIndex) +
      1u);
  for (std::uint32_t aSequence = pOutSelection.mMinIndex;
       aSequence <= pOutSelection.mMaxIndex;
       ++aSequence) {
    ArchiveFileBox aBox{};
    aBox.mSequenceJumber = aSequence;
    aBox.mPayloadStart =
        static_cast<std::uint64_t>(aSequence) * aPayloadBytesPerArchive;
    aBox.mPayloadLength = aPayloadBytesPerArchive;
    aBox.mEmpty = true;
    pOutSelection.mArchiveBoxes.push_back(aBox);
    if (aSequence == std::numeric_limits<std::uint32_t>::max()) {
      break;
    }
  }

  for (const ArchiveCandidate& aArchive : pOutSelection.mArchives) {
    const std::size_t aOffset =
        static_cast<std::size_t>(aArchive.mArchiveIndex - pOutSelection.mMinIndex);
    if (aOffset >= pOutSelection.mArchiveBoxes.size()) {
      continue;
    }
    pOutSelection.mArchiveBoxes[aOffset].mEmpty = false;
    pOutSelection.mArchiveBoxes[aOffset].mPayloadLength =
        static_cast<std::uint64_t>(aArchive.mBlockCount) *
        static_cast<std::uint64_t>(kPayloadBytesPerL3);
  }

  pLogger.LogStatus("[Decode][Discovery] Filename template matched " +
                    std::to_string(pOutSelection.mArchives.size()) +
                    " archives with " + std::to_string(pOutSelection.mGapCount) +
                    " index gaps.");
  LogDiscoveryDirtyTypeStatus(pLogger, pOutSelection);
  LogDiscoveryComponentVersionWarnings(pLogger, pOutSelection);
  if (pOutSelection.mClippedArchiveCount > 0u) {
    pLogger.LogStatus("[Decode][Discovery] Clipped " +
                      std::to_string(pOutSelection.mClippedArchiveCount) +
                      " archives beyond MAX_ARCHIVE_COUNT (" +
                      std::to_string(kMaxArchiveCount) + ").");
  }
  if (aIgnored > 0u) {
    pLogger.LogStatus("[Decode][Discovery] Ignored " + std::to_string(aIgnored) +
                      " candidate archives outside the filename template.");
  }
  if (aHasDominantFamily) {
    pLogger.LogStatus("[Decode][Discovery] Header family id selected " +
                      std::to_string(aDominantFamilyId) + " from " +
                      std::to_string(aDominantFamilyVotes) +
                      " readable headers.");
  }
  if (aFamilyMismatched > 0u) {
    pLogger.LogStatus("[Decode][Discovery] Observed " +
                      std::to_string(aFamilyMismatched) +
                      " candidate archives with mismatched header family id.");
  }
  pLogger.LogStatus("[Decode][Discovery] Chose " +
                    std::to_string(pOutSelection.mArchives.size()) +
                    " archive candidates after filename discovery DONE.");
  ReportProgress(pLogger,
                 pModeName,
                 ProgressProfileKind::kUnbundle,
                 ProgressPhase::kDiscovery,
                 1.0,
                 "Decode discovery complete.");
  return true;
}

}  // namespace decode_internal
}  // namespace peanutbutter
