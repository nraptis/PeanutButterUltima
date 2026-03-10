#include "AppCore_Fences.hpp"
#include "AppCore_Helpers.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>

namespace peanutbutter::detail {
namespace {

struct DecodeSummary {
  std::uint64_t mProcessedBytes = 0;
  std::size_t mFilesProcessed = 0;
  std::size_t mEmptyDirectoriesProcessed = 0;
};

struct DecodeWorkspace {
  unsigned char mSource[kPageLength] = {};
  unsigned char mWorker[kPageLength] = {};
  unsigned char mPage[kPageLength] = {};
  unsigned char mPathBytes[peanutbutter::MAX_VALID_FILE_PATH_LENGTH] = {};
  unsigned char mLengthBytes[8] = {};
  unsigned char mChunk[kFixedIoChunkLength] = {};
  bool mBadBlocks[kPageBlockCount] = {};
};

struct ArchiveDiscoveryResult {
  std::vector<ArchiveHeaderRecord> mArchives;
  std::size_t mScannedFileCount = 0;
  std::size_t mSelectedArchiveOffset = 0;
  bool mSelectedArchiveOffsetValid = false;
};

bool DiscoverArchiveSet(FileSystem& pFileSystem,
                        Logger& pLogger,
                        const RuntimeSettings& pSettings,
                        const std::string& pJobName,
                        const std::string& pArchivePathOrDirectory,
                        const std::string& pSelectedArchiveFilePath,
                        ArchiveDiscoveryResult& pResult,
                        std::string& pErrorMessage) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, pSelectedArchiveFilePath);
  if (!aInputSelection.has_value()) {
    pErrorMessage = pJobName + " failed: archive path does not exist.";
    return false;
  }

  const std::vector<DirectoryEntry> aArchiveFiles = CollectArchiveFilesByHeaderScan(pFileSystem, aInputSelection.value());
  if (aArchiveFiles.empty()) {
    pErrorMessage = pJobName + " failed: no files were found to scan for archive headers.";
    return false;
  }

  pLogger.LogStatus("Discovered " + std::to_string(aArchiveFiles.size()) + " files, scanning archive headers.");

  std::vector<ArchiveHeaderRecord> aDecodedHeaders;
  std::map<std::uint32_t, ArchiveHeaderRecord> aByArchiveIndex;
  std::uint64_t aSelectedIdentifier = 0;
  bool aSelectedIdentifierSet = false;
  std::uint32_t aSelectedArchiveIndex = 0;
  bool aSelectedArchiveIndexSet = false;
  std::size_t aGapPayloadLengthHint = EffectiveArchivePhysicalPayloadLength(pSettings);

  pResult.mScannedFileCount = 0;
  for (const DirectoryEntry& aEntry : aArchiveFiles) {
    ++pResult.mScannedFileCount;
    if ((pResult.mScannedFileCount % kScanLogThrottleFileCount) == 0 || pResult.mScannedFileCount == aArchiveFiles.size()) {
      pLogger.LogStatus("Scanned " + std::to_string(pResult.mScannedFileCount) + " of " +
                        std::to_string(aArchiveFiles.size()) + " files...");
    }

    ArchiveHeader aHeader{};
    std::size_t aFileLength = 0;
    if (!TryReadArchiveHeader(pFileSystem, aEntry.mPath, aHeader, aFileLength)) {
      continue;
    }

    ArchiveHeaderRecord aRecord;
    aRecord.mPath = aEntry.mPath;
    aRecord.mName = pFileSystem.FileName(aEntry.mPath);
    aRecord.mHeader = aHeader;
    aRecord.mPayloadLength = static_cast<std::size_t>(aHeader.mPayloadLength);
    if (aRecord.mPayloadLength == 0 && aFileLength >= kArchiveHeaderLength) {
      aRecord.mPayloadLength = aFileLength - kArchiveHeaderLength;
    }
    if (aGapPayloadLengthHint == 0 && aRecord.mPayloadLength > 0) {
      aGapPayloadLengthHint = aRecord.mPayloadLength;
    }
    if (!aInputSelection->mSelectedFilePath.empty() && aEntry.mPath == aInputSelection->mSelectedFilePath) {
      aSelectedIdentifier = aHeader.mIdentifier;
      aSelectedIdentifierSet = true;
      aSelectedArchiveIndex = aHeader.mArchiveIndex;
      aSelectedArchiveIndexSet = true;
    }
    aDecodedHeaders.push_back(std::move(aRecord));
  }

  if (!aSelectedIdentifierSet && !aDecodedHeaders.empty()) {
    aSelectedIdentifier = aDecodedHeaders.front().mHeader.mIdentifier;
    aSelectedIdentifierSet = true;
  }
  if (!aSelectedIdentifierSet) {
    pErrorMessage = pJobName + " failed: no valid archive headers were found.";
    return false;
  }

  for (const ArchiveHeaderRecord& aRecord : aDecodedHeaders) {
    if (aRecord.mHeader.mIdentifier != aSelectedIdentifier) {
      continue;
    }
    if (aByArchiveIndex.find(aRecord.mHeader.mArchiveIndex) == aByArchiveIndex.end()) {
      aByArchiveIndex.emplace(aRecord.mHeader.mArchiveIndex, aRecord);
    }
  }

  if (!aInputSelection->mSelectedFilePath.empty()) {
    pLogger.LogStatus("Selected archive file = " + pFileSystem.FileName(aInputSelection->mSelectedFilePath) + ".");
  }

  if (aByArchiveIndex.empty()) {
    pErrorMessage = pJobName + " failed: no archive set matched selected archive header.";
    return false;
  }

  const std::uint32_t aMinArchiveIndex = aByArchiveIndex.begin()->first;
  std::uint32_t aMaxArchiveIndex = aByArchiveIndex.rbegin()->first;
  for (const auto& aPair : aByArchiveIndex) {
    const ArchiveHeaderRecord& aRecord = aPair.second;
    if (aRecord.mHeader.mArchiveCount == 0) {
      continue;
    }
    const std::uint64_t aDeclaredEnd = static_cast<std::uint64_t>(aRecord.mHeader.mArchiveIndex) +
                                       static_cast<std::uint64_t>(aRecord.mHeader.mArchiveCount - 1);
    if (aDeclaredEnd > aMaxArchiveIndex) {
      aMaxArchiveIndex = static_cast<std::uint32_t>(
          std::min<std::uint64_t>(aDeclaredEnd, std::numeric_limits<std::uint32_t>::max()));
    }
  }

  std::vector<std::pair<std::uint32_t, std::uint32_t>> aMissingRanges;
  bool aInMissingRange = false;
  std::uint32_t aMissingRangeStart = 0;
  pResult.mArchives.clear();
  for (std::uint32_t aArchiveIndex = aMinArchiveIndex; aArchiveIndex <= aMaxArchiveIndex; ++aArchiveIndex) {
    const auto aIt = aByArchiveIndex.find(aArchiveIndex);
    if (aIt != aByArchiveIndex.end()) {
      if (aInMissingRange) {
        aMissingRanges.push_back({aMissingRangeStart, static_cast<std::uint32_t>(aArchiveIndex - 1)});
        aInMissingRange = false;
      }
      pResult.mArchives.push_back(aIt->second);
      if (aSelectedArchiveIndexSet && aArchiveIndex == aSelectedArchiveIndex) {
        pResult.mSelectedArchiveOffset = pResult.mArchives.size() - 1;
        pResult.mSelectedArchiveOffsetValid = true;
      }
      continue;
    }

    if (!aInMissingRange) {
      aInMissingRange = true;
      aMissingRangeStart = aArchiveIndex;
    }
    ArchiveHeaderRecord aGapRecord;
    aGapRecord.mName = "missing_archive_" + std::to_string(aArchiveIndex);
    aGapRecord.mHeader.mIdentifier = aSelectedIdentifier;
    aGapRecord.mHeader.mArchiveIndex = aArchiveIndex;
    aGapRecord.mHeader.mPayloadLength = static_cast<std::uint32_t>(aGapPayloadLengthHint);
    aGapRecord.mPayloadLength = aGapPayloadLengthHint;
    aGapRecord.mMissingGap = true;
    pResult.mArchives.push_back(std::move(aGapRecord));
  }
  if (aInMissingRange) {
    aMissingRanges.push_back({aMissingRangeStart, aMaxArchiveIndex});
  }
  LogMissingArchiveRanges(pLogger, aMissingRanges);
  return true;
}

bool LoadArchivePage(FileSystem& pFileSystem,
                     const Crypt& pCrypt,
                     const ArchiveHeaderRecord& pArchive,
                     bool pUseEncryption,
                     std::size_t pPageStart,
                     unsigned char* pSource,
                     unsigned char* pWorker,
                     unsigned char* pDestination,
                     std::size_t& pPageLength,
                     std::string& pErrorMessage) {
  pPageLength = 0;
  std::unique_ptr<FileReadStream> aReadStream = pFileSystem.OpenReadStream(pArchive.mPath);
  if (aReadStream == nullptr || !aReadStream->IsReady() ||
      aReadStream->GetLength() < (kArchiveHeaderLength + pArchive.mPayloadLength)) {
    pErrorMessage = "could not open archive " + pArchive.mName;
    return false;
  }

  std::memset(pSource, 0, kPageLength);
  std::memset(pWorker, 0, kPageLength);
  std::memset(pDestination, 0, kPageLength);
  pPageLength = std::min(kPageLength, pArchive.mPayloadLength - pPageStart);
  if (!aReadStream->Read(kArchiveHeaderLength + pPageStart, pSource, pPageLength)) {
    pErrorMessage = "could not read archive " + pArchive.mName;
    return false;
  }

  if (!pUseEncryption) {
    std::memcpy(pDestination, pSource, pPageLength);
    return true;
  }

  if (!pCrypt.UnsealData(pSource,
                         pWorker,
                         pDestination,
                         kPageLength,
                         &pErrorMessage,
                         CryptMode::kNormal)) {
    if (pErrorMessage.empty()) {
      pErrorMessage = "could not decrypt archive " + pArchive.mName;
    } else {
      pErrorMessage = "could not decrypt archive " + pArchive.mName + ": " + pErrorMessage;
    }
    return false;
  }
  return true;
}

bool ArchiveHasNonZeroLogicalPayloadBytes(FileSystem& pFileSystem,
                                          const Crypt& pCrypt,
                                          const ArchiveHeaderRecord& pArchive,
                                          bool pUseEncryption,
                                          std::size_t pStartPhysicalOffset,
                                          bool& pHasNonZeroBytes,
                                          std::string& pErrorMessage) {
  pHasNonZeroBytes = false;
  if (pArchive.mMissingGap || pStartPhysicalOffset >= pArchive.mPayloadLength) {
    return true;
  }

  std::array<unsigned char, kPageLength> aSource = {};
  std::array<unsigned char, kPageLength> aWorker = {};
  std::array<unsigned char, kPageLength> aPage = {};

  std::size_t aPageStart = (pStartPhysicalOffset / kPageLength) * kPageLength;
  while (aPageStart < pArchive.mPayloadLength) {
    std::size_t aPageLength = 0;
    std::string aLoadError;
    if (!LoadArchivePage(pFileSystem,
                         pCrypt,
                         pArchive,
                         pUseEncryption,
                         aPageStart,
                         aSource.data(),
                         aWorker.data(),
                         aPage.data(),
                         aPageLength,
                         aLoadError)) {
      pErrorMessage = aLoadError.empty() ? ("could not inspect archive " + pArchive.mName) : aLoadError;
      return false;
    }

    const std::size_t aLocalStart = (pStartPhysicalOffset > aPageStart) ? (pStartPhysicalOffset - aPageStart) : 0;
    for (std::size_t aLocalOffset = aLocalStart; aLocalOffset < aPageLength; ++aLocalOffset) {
      const std::size_t aPayloadOffset = aPageStart + aLocalOffset;
      const std::size_t aOffsetInBlock = aPayloadOffset % kBlockLength;
      if (aOffsetInBlock < kRecoveryHeaderLength) {
        continue;
      }
      if (aPage[aLocalOffset] != 0) {
        pHasNonZeroBytes = true;
        return true;
      }
    }
    aPageStart += kPageLength;
  }
  return true;
}

enum class LogicalEndTailClassification {
  kNone,
  kDanglingBytes,
  kDanglingArchives,
};

bool ClassifyLogicalEndTail(FileSystem& pFileSystem,
                            const Crypt& pCrypt,
                            const std::vector<ArchiveHeaderRecord>& pArchives,
                            const FenceCursor& pCursor,
                            bool pUseEncryption,
                            LogicalEndTailClassification& pClassification,
                            std::string& pErrorMessage) {
  pClassification = LogicalEndTailClassification::kNone;
  if (pCursor.mArchiveIndex >= pArchives.size()) {
    return true;
  }

  for (std::size_t aArchiveIndex = pCursor.mArchiveIndex + 1; aArchiveIndex < pArchives.size(); ++aArchiveIndex) {
    if (!pArchives[aArchiveIndex].mMissingGap) {
      pClassification = LogicalEndTailClassification::kDanglingArchives;
      return true;
    }
  }

  bool aHasTrailingBytes = false;
  if (!ArchiveHasNonZeroLogicalPayloadBytes(pFileSystem,
                                            pCrypt,
                                            pArchives[pCursor.mArchiveIndex],
                                            pUseEncryption,
                                            pCursor.mPhysicalOffset,
                                            aHasTrailingBytes,
                                            pErrorMessage)) {
    return false;
  }
  if (aHasTrailingBytes) {
    pClassification = LogicalEndTailClassification::kDanglingBytes;
  }
  return true;
}

enum class ReaderFailureMode {
  kNone,
  kFatal,
  kRecoverable,
};

std::size_t BlockStartForPhysicalOffset(std::size_t pPhysicalOffset) {
  return (pPhysicalOffset / kBlockLength) * kBlockLength;
}

std::size_t NextBlockStartForPhysicalOffset(std::size_t pPhysicalOffset) {
  return BlockStartForPhysicalOffset(pPhysicalOffset) + kBlockLength;
}

void MarkBadBlocksForPage(const unsigned char* pPage, std::size_t pPageLength, bool* pBadBlocks) {
  std::memset(pBadBlocks, 0, sizeof(bool) * kPageBlockCount);
  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = aBlockIndex * kBlockLength;
    if (aBlockStart + kBlockLength > pPageLength) {
      break;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, pPage + aBlockStart, sizeof(aHeader));
    unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
    GenerateChecksum(pPage + aBlockStart, aChecksum);
    if (std::memcmp(&aHeader.mChecksum, aChecksum, peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH) != 0) {
      pBadBlocks[aBlockIndex] = true;
    }
  }
}

bool NormalizeArchiveCursor(const std::vector<ArchiveHeaderRecord>& pArchives, FenceCursor& pCursor) {
  while (pCursor.mArchiveIndex < pArchives.size()) {
    const ArchiveHeaderRecord& aArchive = pArchives[pCursor.mArchiveIndex];
    if (pCursor.mPhysicalOffset >= aArchive.mPayloadLength) {
      ++pCursor.mArchiveIndex;
      pCursor.mPhysicalOffset = 0;
      continue;
    }

    const std::size_t aOffsetInBlock = pCursor.mPhysicalOffset % kBlockLength;
    if (aOffsetInBlock < kRecoveryHeaderLength) {
      const std::size_t aSkip = kRecoveryHeaderLength - aOffsetInBlock;
      if (pCursor.mPhysicalOffset + aSkip > aArchive.mPayloadLength) {
        ++pCursor.mArchiveIndex;
        pCursor.mPhysicalOffset = 0;
        continue;
      }
      pCursor.mPhysicalOffset += aSkip;
      continue;
    }
    return true;
  }
  return true;
}

bool TryReadVerifiedSpanAt(FileSystem& pFileSystem,
                           const Crypt& pCrypt,
                           const std::vector<ArchiveHeaderRecord>& pArchives,
                           bool pUseEncryption,
                           DecodeWorkspace& pWorkspace,
                           FenceCursor& pCursor,
                           unsigned char* pDestination,
                           std::size_t pLength) {
  std::size_t aLoadedArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t aLoadedPageStart = 0;
  std::size_t aLoadedPageLength = 0;
  std::size_t aWritten = 0;

  while (aWritten < pLength) {
    if (!NormalizeArchiveCursor(pArchives, pCursor) || pCursor.mArchiveIndex >= pArchives.size()) {
      return false;
    }

    const ArchiveHeaderRecord& aArchive = pArchives[pCursor.mArchiveIndex];
    if (aArchive.mMissingGap) {
      return false;
    }

    const std::size_t aDesiredPageStart = (pCursor.mPhysicalOffset / kPageLength) * kPageLength;
    if (aLoadedArchiveIndex != pCursor.mArchiveIndex || aLoadedPageStart != aDesiredPageStart) {
      std::string aLoadError;
      if (!LoadArchivePage(pFileSystem,
                           pCrypt,
                           aArchive,
                           pUseEncryption,
                           aDesiredPageStart,
                           pWorkspace.mSource,
                           pWorkspace.mWorker,
                           pWorkspace.mPage,
                           aLoadedPageLength,
                           aLoadError)) {
        return false;
      }
      MarkBadBlocksForPage(pWorkspace.mPage, aLoadedPageLength, pWorkspace.mBadBlocks);
      aLoadedArchiveIndex = pCursor.mArchiveIndex;
      aLoadedPageStart = aDesiredPageStart;
    }

    const std::size_t aPageOffset = pCursor.mPhysicalOffset - aLoadedPageStart;
    const std::size_t aBlockIndex = aPageOffset / kBlockLength;
    if (aBlockIndex >= kPageBlockCount || pWorkspace.mBadBlocks[aBlockIndex]) {
      return false;
    }

    const std::size_t aOffsetInBlock = pCursor.mPhysicalOffset % kBlockLength;
    const std::size_t aBytesToBlockEnd = kBlockLength - aOffsetInBlock;
    const std::size_t aBytesToArchiveEnd = aArchive.mPayloadLength - pCursor.mPhysicalOffset;
    const std::size_t aSpan = std::min({pLength - aWritten, aBytesToBlockEnd, aBytesToArchiveEnd});
    std::memcpy(pDestination + aWritten, pWorkspace.mPage + aPageOffset, aSpan);
    pCursor.mPhysicalOffset += aSpan;
    aWritten += aSpan;
  }

  return true;
}

bool LooksLikeRelativePath(const unsigned char* pPathBytes, std::size_t pPathLength) {
  if (pPathLength == 0 || pPathLength > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
    return false;
  }
  if (pPathBytes[0] == '/' || pPathBytes[0] == '\\') {
    return false;
  }
  for (std::size_t aIndex = 0; aIndex < pPathLength; ++aIndex) {
    const unsigned char aByte = pPathBytes[aIndex];
    if (aByte == 0 || aByte < 32) {
      return false;
    }
  }
  return true;
}

bool ProbeRecordStart(FileSystem& pFileSystem,
                      const Crypt& pCrypt,
                      const std::vector<ArchiveHeaderRecord>& pArchives,
                      const FenceDomain& pFenceDomain,
                      bool pUseEncryption,
                      DecodeWorkspace& pWorkspace,
                      std::size_t pArchiveOffset,
                      std::size_t pPhysicalOffset,
                      bool* pIsEndMarker = nullptr) {
  FenceCursor aCursor{pArchiveOffset, pPhysicalOffset};
  unsigned char aLengthBytes[2] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aLengthBytes,
                             sizeof(aLengthBytes))) {
    return false;
  }

  const std::size_t aPathLength = static_cast<std::size_t>(ReadLeFromBytes(aLengthBytes, sizeof(aLengthBytes)));
  if (aPathLength == 0) {
    if (pIsEndMarker != nullptr) {
      *pIsEndMarker = true;
    }
    return true;
  }
  if (pIsEndMarker != nullptr) {
    *pIsEndMarker = false;
  }
  FenceProbe aPathProbe;
  aPathProbe.mRule = FenceRule::kPathLength;
  aPathProbe.mCursor = aCursor;
  aPathProbe.mValue = aPathLength;
  if (FenceCheck(pFenceDomain, aPathProbe)) {
    return false;
  }

  unsigned char aPathBytes[peanutbutter::MAX_VALID_FILE_PATH_LENGTH] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aPathBytes,
                             aPathLength) ||
      !LooksLikeRelativePath(aPathBytes, aPathLength)) {
    return false;
  }

  unsigned char aContentLengthBytes[6] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aContentLengthBytes,
                             sizeof(aContentLengthBytes))) {
    return false;
  }

  const std::uint64_t aContentLength = ReadLeFromBytes(aContentLengthBytes, sizeof(aContentLengthBytes));
  if (aContentLength == kDirectoryRecordContentMarker) {
    return true;
  }
  FenceProbe aContentProbe;
  aContentProbe.mRule = FenceRule::kFileContentLength;
  aContentProbe.mCursor = aCursor;
  aContentProbe.mValue = aContentLength;
  return !FenceCheck(pFenceDomain, aContentProbe);
}

bool ResolveRecoveryStartPosition(FileSystem& pFileSystem,
                                  const Crypt& pCrypt,
                                  const std::vector<ArchiveHeaderRecord>& pArchives,
                                  bool pUseEncryption,
                                  std::size_t& pStartArchiveOffset,
                                  std::size_t& pStartPhysicalOffset,
                                  std::string& pErrorMessage) {
  const FenceDomain aFenceDomain = BuildFenceDomain(pArchives);
  std::unique_ptr<DecodeWorkspace> aScanWorkspace = std::make_unique<DecodeWorkspace>();
  std::unique_ptr<DecodeWorkspace> aProbeWorkspace = std::make_unique<DecodeWorkspace>();
  std::string aLastFenceMessage;
  for (std::size_t aArchiveOffset = 0; aArchiveOffset < pArchives.size(); ++aArchiveOffset) {
    const ArchiveHeaderRecord& aArchive = pArchives[aArchiveOffset];
    if (aArchive.mMissingGap || aArchive.mPayloadLength < kBlockLength) {
      continue;
    }

    std::size_t aPageLength = 0;
    std::string aLoadError;
    if (!LoadArchivePage(pFileSystem,
                         pCrypt,
                         aArchive,
                         pUseEncryption,
                         0,
                         aScanWorkspace->mSource,
                         aScanWorkspace->mWorker,
                         aScanWorkspace->mPage,
                         aPageLength,
                         aLoadError)) {
      continue;
    }
    if (aPageLength < kBlockLength) {
      continue;
    }

    MarkBadBlocksForPage(aScanWorkspace->mPage, aPageLength, aScanWorkspace->mBadBlocks);
    if (aScanWorkspace->mBadBlocks[0]) {
      continue;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, aScanWorkspace->mPage, sizeof(aHeader));
    FenceProbe aRecoveryProbe;
    aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
    aRecoveryProbe.mArchiveIndex = aArchiveOffset;
    aRecoveryProbe.mBlockStartOffset = 0;
    aRecoveryProbe.mValue = aHeader.mDistanceToNextRecord;
    FenceViolation aRecoveryViolation;
    if (FenceCheck(aFenceDomain, aRecoveryProbe, &aRecoveryViolation)) {
      aLastFenceMessage = FenceDetails(aRecoveryProbe, aRecoveryViolation).mMessage;
      continue;
    }

    const std::uint64_t aCandidate64 =
        static_cast<std::uint64_t>(kRecoveryHeaderLength) + aHeader.mDistanceToNextRecord;
    const std::size_t aCandidate = static_cast<std::size_t>(aCandidate64);
    bool aIsEndMarker = false;
    if (!ProbeRecordStart(
            pFileSystem,
            pCrypt,
            pArchives,
            aFenceDomain,
            pUseEncryption,
            *aProbeWorkspace,
            aArchiveOffset,
            aCandidate,
            &aIsEndMarker)) {
      continue;
    }
    if (aIsEndMarker) {
      continue;
    }

    pStartArchiveOffset = aArchiveOffset;
    pStartPhysicalOffset = aCandidate;
    return true;
  }

  pErrorMessage = aLastFenceMessage.empty() ? "no recovery header pointed to a valid record boundary." : aLastFenceMessage;
  return false;
}

class ArchivePayloadReader {
 public:
  ArchivePayloadReader(FileSystem& pFileSystem,
                       const Crypt& pCrypt,
                       Logger& pLogger,
                       const std::vector<ArchiveHeaderRecord>& pArchives,
                       const FenceDomain& pFenceDomain,
                       std::size_t pStartPhysicalOffset,
                       bool pUseEncryption,
                       bool pRecoverMode)
      : mFileSystem(pFileSystem),
        mCrypt(pCrypt),
        mLogger(pLogger),
        mArchives(pArchives),
        mFenceDomain(pFenceDomain),
        mPhysicalOffset(pStartPhysicalOffset),
        mUseEncryption(pUseEncryption),
        mRecoverMode(pRecoverMode),
        mWorkspace(std::make_unique<DecodeWorkspace>()) {}

  bool Read(unsigned char* pDestination, std::size_t pLength, std::size_t& pBytesRead) {
    pBytesRead = 0;
    mRecoverStoppedAtEnd = false;
    while (pBytesRead < pLength) {
      if (!NormalizePosition()) {
        return false;
      }
      if (mArchiveIndex >= mArchives.size()) {
        if (mRecoverMode) {
          SetRecoverableFailure("UNP_EOF_003: recover reached the end of archive payload.", mArchives.size(), 0);
        } else {
          SetFatalFailure("UNP_EOF_003: unexpected end of archive payload.");
        }
        return false;
      }

      const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
      const std::size_t aBlockStart = BlockStartForPhysicalOffset(mPhysicalOffset);
      const std::size_t aOffsetInBlock = mPhysicalOffset % kBlockLength;
      const std::size_t aBytesToBlockEnd = kBlockLength - aOffsetInBlock;
      const std::size_t aBytesToArchiveEnd = aArchive.mPayloadLength - mPhysicalOffset;
      const std::size_t aSpan = std::min({pLength - pBytesRead, aBytesToBlockEnd, aBytesToArchiveEnd});

      if (aArchive.mMissingGap) {
        if (!mRecoverMode) {
          SetFatalFailure("attempted to read through a missing archive gap.");
        } else {
          SetRecoverableFailure("recover encountered a missing archive gap.", mArchiveIndex, aArchive.mPayloadLength);
        }
        return false;
      }

      if (!EnsurePageLoaded()) {
        return false;
      }

      const std::size_t aPageOffset = mPhysicalOffset - mPageStart;
      const std::size_t aBlockIndex = aPageOffset / kBlockLength;
      if (aBlockIndex >= kPageBlockCount || mWorkspace->mBadBlocks[aBlockIndex]) {
        if (!mRecoverMode) {
          SetFatalFailure("checksum mismatch detected while reading archive payload.");
        } else {
          SetRecoverableFailure("recover encountered a bad checksum while reading archive payload.",
                                mArchiveIndex,
                                aBlockStart + kBlockLength);
        }
        return false;
      }

      const std::size_t aBlockStartInPage = aBlockIndex * kBlockLength;
      if (mLastCheckedRecoveryArchive != mArchiveIndex || mLastCheckedRecoveryBlockStart != aBlockStart) {
        RecoveryHeader aRecoveryHeader{};
        std::memcpy(&aRecoveryHeader, mWorkspace->mPage + aBlockStartInPage, sizeof(aRecoveryHeader));
        FenceProbe aRecoveryProbe;
        aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
        aRecoveryProbe.mArchiveIndex = mArchiveIndex;
        aRecoveryProbe.mBlockStartOffset = aBlockStart;
        aRecoveryProbe.mValue = aRecoveryHeader.mDistanceToNextRecord;
        FenceViolation aRecoveryViolation;
        if (FenceCheck(mFenceDomain, aRecoveryProbe, &aRecoveryViolation)) {
          const FenceResult aFence = FenceDetails(aRecoveryProbe, aRecoveryViolation);
          if (!mRecoverMode) {
            SetFatalFailure(aFence.mMessage);
          } else {
            SetRecoverableFailure(aFence.mMessage, mArchiveIndex, aBlockStart + kBlockLength);
          }
          return false;
        }
        mLastCheckedRecoveryArchive = mArchiveIndex;
        mLastCheckedRecoveryBlockStart = aBlockStart;
      }

      std::memcpy(pDestination + pBytesRead, mWorkspace->mPage + aPageOffset, aSpan);
      mPhysicalOffset += aSpan;
      pBytesRead += aSpan;
    }

    mFailureMode = ReaderFailureMode::kNone;
    (void)NormalizePosition();
    return true;
  }

  std::size_t CompletedArchives() const {
    return mCompletedArchives;
  }

  FenceCursor Cursor() const {
    return {mArchiveIndex, mPhysicalOffset};
  }

  bool HasRecoverableFailure() const {
    return mFailureMode == ReaderFailureMode::kRecoverable;
  }

  bool RecoverStoppedAtEnd() const {
    return mRecoverStoppedAtEnd;
  }

  const std::string& ErrorMessage() const {
    return mErrorMessage;
  }

  void MarkRecoverableFenceFailure(const std::string& pMessage) {
    if (!mRecoverMode) {
      SetFatalFailure(pMessage);
      return;
    }
    if (mArchiveIndex >= mArchives.size()) {
      SetRecoverableFailure(pMessage, mArchives.size(), 0);
      return;
    }
    SetRecoverableFailure(pMessage, mArchiveIndex, NextBlockStartForPhysicalOffset(mPhysicalOffset));
  }

  bool WalkAheadToNextRecord() {
    if (!mRecoverMode) {
      SetFatalFailure("recover walk-ahead is not available during unpack.");
      return false;
    }

    FenceCursor aScanCursor{mWalkAheadArchiveIndex, mWalkAheadPhysicalOffset};
    while (aScanCursor.mArchiveIndex < mArchives.size()) {
      const ArchiveHeaderRecord& aArchive = mArchives[aScanCursor.mArchiveIndex];
      if (aScanCursor.mPhysicalOffset >= aArchive.mPayloadLength) {
        ++aScanCursor.mArchiveIndex;
        aScanCursor.mPhysicalOffset = 0;
        continue;
      }
      aScanCursor.mPhysicalOffset = BlockStartForPhysicalOffset(aScanCursor.mPhysicalOffset);
      break;
    }

    mLogger.LogStatus("Recover walk-ahead starting after " + mErrorMessage);

    std::unique_ptr<DecodeWorkspace> aScanWorkspace = std::make_unique<DecodeWorkspace>();
    std::unique_ptr<DecodeWorkspace> aProbeWorkspace = std::make_unique<DecodeWorkspace>();
    std::uint64_t aScannedBytes = 0;
    std::uint64_t aNextProgressLog = kRecoverLogStepBytes;

    const auto aMaybeLogProgress = [&]() {
      while (aScannedBytes >= aNextProgressLog) {
        mLogger.LogStatus("Recover walk-ahead scanned " + FormatBytes(aScannedBytes) +
                          " without a valid record start yet.");
        aNextProgressLog += kRecoverLogStepBytes;
      }
    };

    for (std::size_t aArchiveOffset = aScanCursor.mArchiveIndex; aArchiveOffset < mArchives.size(); ++aArchiveOffset) {
      const ArchiveHeaderRecord& aArchive = mArchives[aArchiveOffset];
      std::size_t aArchiveStart = (aArchiveOffset == aScanCursor.mArchiveIndex) ? aScanCursor.mPhysicalOffset : 0;
      if (aArchiveStart >= aArchive.mPayloadLength) {
        continue;
      }

      if (aArchive.mMissingGap || aArchive.mPayloadLength < kBlockLength) {
        aScannedBytes += static_cast<std::uint64_t>(aArchive.mPayloadLength - aArchiveStart);
        aMaybeLogProgress();
        continue;
      }

      const std::size_t aFirstPageStart = (aArchiveStart / kPageLength) * kPageLength;
      for (std::size_t aPageStart = aFirstPageStart; aPageStart < aArchive.mPayloadLength; aPageStart += kPageLength) {
        std::size_t aPageLength = 0;
        std::string aLoadError;
        if (!LoadArchivePage(mFileSystem,
                             mCrypt,
                             aArchive,
                             mUseEncryption,
                             aPageStart,
                             aScanWorkspace->mSource,
                             aScanWorkspace->mWorker,
                             aScanWorkspace->mPage,
                             aPageLength,
                             aLoadError)) {
          aScannedBytes += static_cast<std::uint64_t>(std::min(kPageLength, aArchive.mPayloadLength - aPageStart));
          aMaybeLogProgress();
          continue;
        }

        MarkBadBlocksForPage(aScanWorkspace->mPage, aPageLength, aScanWorkspace->mBadBlocks);
        const std::size_t aFirstBlockIndex =
            (aPageStart == aFirstPageStart) ? ((aArchiveStart - aPageStart) / kBlockLength) : 0;
        for (std::size_t aBlockIndex = aFirstBlockIndex; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
          const std::size_t aBlockStartInPage = aBlockIndex * kBlockLength;
          const std::size_t aBlockStart = aPageStart + aBlockStartInPage;
          if (aBlockStart + kBlockLength > aArchive.mPayloadLength || aBlockStartInPage + kBlockLength > aPageLength) {
            break;
          }

          if (aScanWorkspace->mBadBlocks[aBlockIndex]) {
            aScannedBytes += kBlockLength;
            aMaybeLogProgress();
            continue;
          }

          RecoveryHeader aHeader{};
          std::memcpy(&aHeader, aScanWorkspace->mPage + aBlockStartInPage, sizeof(aHeader));
          FenceProbe aRecoveryProbe;
          aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
          aRecoveryProbe.mArchiveIndex = aArchiveOffset;
          aRecoveryProbe.mBlockStartOffset = aBlockStart;
          aRecoveryProbe.mValue = aHeader.mDistanceToNextRecord;
          if (!FenceCheck(mFenceDomain, aRecoveryProbe)) {
            const std::uint64_t aCandidate64 =
                static_cast<std::uint64_t>(aBlockStart) + static_cast<std::uint64_t>(kRecoveryHeaderLength) +
                aHeader.mDistanceToNextRecord;
            const std::size_t aCandidate = static_cast<std::size_t>(aCandidate64);
            bool aIsEndMarker = false;
            if (ProbeRecordStart(mFileSystem,
                                 mCrypt,
                                 mArchives,
                                 mFenceDomain,
                                 mUseEncryption,
                                 *aProbeWorkspace,
                                 aArchiveOffset,
                                 aCandidate,
                                 &aIsEndMarker) &&
                !aIsEndMarker) {
              AddRecoverSkippedBytes(aScannedBytes);
              SetPosition(aArchiveOffset, aCandidate);
              mLogger.LogStatus("Recover walk-ahead resynced at " + aArchive.mName + " +" +
                                FormatBytes(aCandidate) + " after scanning " +
                                FormatBytes(aScannedBytes) + ".");
              return true;
            }
          }

          aScannedBytes += kBlockLength;
          aMaybeLogProgress();
        }
      }
    }

    AddRecoverSkippedBytes(aScannedBytes);
    mLogger.LogStatus("Recover walk-ahead reached the end of the archive set after scanning " +
                      FormatBytes(aScannedBytes) + ".");
    ResetPageState();
    mArchiveIndex = mArchives.size();
    mPhysicalOffset = 0;
    mCompletedArchives = mArchives.size();
    mFailureMode = ReaderFailureMode::kNone;
    mRecoverStoppedAtEnd = true;
    return true;
  }

  void FlushRecoverLog() {
    if (mRecoverMode && mRecoverSkippedBytes > 0) {
      mLogger.LogStatus("Recover: skipped " + FormatBytes(mRecoverSkippedBytes) +
                        " while resynchronizing damaged payload.");
    }
  }

 private:
  bool NormalizePosition() {
    while (mArchiveIndex < mArchives.size()) {
      const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
      if (mPhysicalOffset >= aArchive.mPayloadLength) {
        ++mCompletedArchives;
        ++mArchiveIndex;
        mPhysicalOffset = 0;
        ResetPageState();
        continue;
      }

      const std::size_t aOffsetInBlock = mPhysicalOffset % kBlockLength;
      if (aOffsetInBlock < kRecoveryHeaderLength) {
        const std::size_t aSkip = kRecoveryHeaderLength - aOffsetInBlock;
        if (mPhysicalOffset + aSkip > aArchive.mPayloadLength) {
          ++mCompletedArchives;
          ++mArchiveIndex;
          mPhysicalOffset = 0;
          ResetPageState();
          continue;
        }
        mPhysicalOffset += aSkip;
        continue;
      }
      return true;
    }
    return true;
  }

  void ResetPageState() {
    mPageLoaded = false;
    mPageArchiveIndex = std::numeric_limits<std::size_t>::max();
    mPageStart = 0;
    mOpenedArchiveIndex = std::numeric_limits<std::size_t>::max();
    mReadStream.reset();
  }

  void SetFatalFailure(const std::string& pMessage) {
    mFailureMode = ReaderFailureMode::kFatal;
    mErrorMessage = pMessage;
    mRecoverStoppedAtEnd = false;
  }

  void SetRecoverableFailure(const std::string& pMessage,
                             std::size_t pArchiveIndex,
                             std::size_t pPhysicalOffset) {
    mFailureMode = ReaderFailureMode::kRecoverable;
    mErrorMessage = pMessage;
    mWalkAheadArchiveIndex = pArchiveIndex;
    mWalkAheadPhysicalOffset = pPhysicalOffset;
    mRecoverStoppedAtEnd = false;
  }

  void SetPosition(std::size_t pArchiveIndex, std::size_t pPhysicalOffset) {
    mArchiveIndex = pArchiveIndex;
    mPhysicalOffset = pPhysicalOffset;
    if (mCompletedArchives < pArchiveIndex) {
      mCompletedArchives = pArchiveIndex;
    }
    mFailureMode = ReaderFailureMode::kNone;
    mErrorMessage.clear();
    mRecoverStoppedAtEnd = false;
    ResetPageState();
  }

  bool OpenCurrentArchive() {
    if (mArchiveIndex >= mArchives.size()) {
      return false;
    }
    if (mOpenedArchiveIndex == mArchiveIndex && mReadStream != nullptr) {
      return true;
    }

    const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
    mReadStream = mFileSystem.OpenReadStream(aArchive.mPath);
    if (mReadStream == nullptr || !mReadStream->IsReady() ||
        mReadStream->GetLength() < (kArchiveHeaderLength + aArchive.mPayloadLength)) {
      mErrorMessage = "could not open archive " + aArchive.mName;
      return false;
    }
    mOpenedArchiveIndex = mArchiveIndex;
    return true;
  }

  bool EnsurePageLoaded() {
    if (mArchiveIndex >= mArchives.size()) {
      return false;
    }

    const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
    const std::size_t aDesiredPageStart = (mPhysicalOffset / kPageLength) * kPageLength;
    if (mPageLoaded && mPageArchiveIndex == mArchiveIndex && mPageStart == aDesiredPageStart) {
      return true;
    }

    if (!OpenCurrentArchive()) {
      if (mRecoverMode) {
        SetRecoverableFailure(mErrorMessage.empty() ? "recover could not open an archive page."
                                                    : "recover " + mErrorMessage + ".",
                              mArchiveIndex,
                              aArchive.mPayloadLength);
      } else {
        SetFatalFailure(mErrorMessage.empty() ? "could not open archive page." : mErrorMessage);
      }
      return false;
    }

    std::memset(mWorkspace->mSource, 0, kPageLength);
    std::memset(mWorkspace->mWorker, 0, kPageLength);
    std::memset(mWorkspace->mPage, 0, kPageLength);
    std::memset(mWorkspace->mBadBlocks, 0, sizeof(mWorkspace->mBadBlocks));

    const std::size_t aPageLength = std::min(kPageLength, aArchive.mPayloadLength - aDesiredPageStart);
    if (!mReadStream->Read(kArchiveHeaderLength + aDesiredPageStart, mWorkspace->mSource, aPageLength)) {
      const std::string aMessage = "could not read archive " + aArchive.mName;
      if (mRecoverMode) {
        SetRecoverableFailure("recover encountered an unreadable archive page in " + aArchive.mName + ".",
                              mArchiveIndex,
                              aDesiredPageStart + kPageLength);
      } else {
        SetFatalFailure(aMessage);
      }
      return false;
    }

    if (mUseEncryption) {
      std::string aCryptError;
      if (!mCrypt.UnsealData(mWorkspace->mSource,
                             mWorkspace->mWorker,
                             mWorkspace->mPage,
                             kPageLength,
                             &aCryptError,
                             CryptMode::kNormal)) {
        const std::string aMessage = aCryptError.empty() ? "could not decrypt archive " + aArchive.mName
                                                         : "could not decrypt archive " + aArchive.mName + ": " + aCryptError;
        if (mRecoverMode) {
          SetRecoverableFailure("recover encountered an unreadable encrypted page in " + aArchive.mName + ".",
                                mArchiveIndex,
                                aDesiredPageStart + kPageLength);
        } else {
          SetFatalFailure(aMessage);
        }
        return false;
      }
    } else {
      std::memcpy(mWorkspace->mPage, mWorkspace->mSource, aPageLength);
    }

    MarkBadBlocksForPage(mWorkspace->mPage, aPageLength, mWorkspace->mBadBlocks);
    mPageArchiveIndex = mArchiveIndex;
    mPageStart = aDesiredPageStart;
    mPageLoaded = true;
    return true;
  }

  void AddRecoverSkippedBytes(std::uint64_t pBytes) {
    mRecoverSkippedBytes += pBytes;
  }

  FileSystem& mFileSystem;
  const Crypt& mCrypt;
  Logger& mLogger;
  const std::vector<ArchiveHeaderRecord>& mArchives;
  const FenceDomain& mFenceDomain;
  bool mUseEncryption = false;
  bool mRecoverMode = false;
  std::unique_ptr<DecodeWorkspace> mWorkspace;
  std::unique_ptr<FileReadStream> mReadStream;
  std::size_t mArchiveIndex = 0;
  std::size_t mPhysicalOffset = 0;
  std::size_t mCompletedArchives = 0;
  std::size_t mPageArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t mPageStart = 0;
  bool mPageLoaded = false;
  std::size_t mOpenedArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t mWalkAheadArchiveIndex = 0;
  std::size_t mWalkAheadPhysicalOffset = 0;
  std::size_t mLastCheckedRecoveryArchive = std::numeric_limits<std::size_t>::max();
  std::size_t mLastCheckedRecoveryBlockStart = std::numeric_limits<std::size_t>::max();
  ReaderFailureMode mFailureMode = ReaderFailureMode::kNone;
  bool mRecoverStoppedAtEnd = false;
  std::uint64_t mRecoverSkippedBytes = 0;
  std::string mErrorMessage;
};

bool DecodeArchiveSet(FileSystem& pFileSystem,
                      const Crypt& pCrypt,
                      Logger& pLogger,
                      const std::vector<ArchiveHeaderRecord>& pArchives,
                      std::size_t pStartPhysicalOffset,
                      const std::string& pDestinationDirectory,
                      bool pWriteFiles,
                      bool pUseEncryption,
                      bool pRecoverMode,
                      DecodeSummary& pSummary,
                      std::string& pErrorMessage) {
  const FenceDomain aFenceDomain = BuildFenceDomain(pArchives);
  ArchivePayloadReader aReader(
      pFileSystem, pCrypt, pLogger, pArchives, aFenceDomain, pStartPhysicalOffset, pUseEncryption, pRecoverMode);
  std::unique_ptr<DecodeWorkspace> aWorkspace = std::make_unique<DecodeWorkspace>();
  std::size_t aLastLoggedArchive = 0;

  const auto aLogArchiveProgress = [&]() {
    while (aLastLoggedArchive < aReader.CompletedArchives()) {
      ++aLastLoggedArchive;
      pLogger.LogStatus((pRecoverMode ? "Recovered archive " : "Unbundled archive ") +
                        std::to_string(aLastLoggedArchive) + " / " + std::to_string(pArchives.size()) + ", " +
                        std::to_string(pSummary.mFilesProcessed) + " files written, " +
                        FormatBytes(pSummary.mProcessedBytes) + ".");
    }
  };

  const auto aHandleRecoverFailure = [&](const std::string& pReason,
                                         std::unique_ptr<FileWriteStream>* pWriteStream,
                                         const std::string& pRelativePath,
                                         std::uint64_t pPartialBytes) -> bool {
    if (!pRecoverMode) {
      return false;
    }

    if (!pReason.empty()) {
      aReader.MarkRecoverableFenceFailure(pReason);
    }

    if (pWriteStream != nullptr && pWriteStream->get() != nullptr) {
      const std::size_t aBytesWritten = (*pWriteStream)->GetBytesWritten();
      if (!(*pWriteStream)->Close()) {
        pErrorMessage = "could not finalize partial file " + pRelativePath;
        return false;
      }
      pWriteStream->reset();
      pLogger.LogStatus("Recover: abandoned partial file " + pRelativePath + " after " +
                        FormatBytes(std::max<std::uint64_t>(pPartialBytes, aBytesWritten)) + ".");
    } else if (!pRelativePath.empty()) {
      pLogger.LogStatus("Recover: abandoned record " + pRelativePath + " before completion.");
    }

    if (!aReader.WalkAheadToNextRecord()) {
      pErrorMessage = aReader.ErrorMessage();
      return false;
    }

    aLogArchiveProgress();
    return true;
  };

  bool aDone = false;
  while (!aDone) {
    std::size_t aBytesRead = 0;
    if (!aReader.Read(aWorkspace->mLengthBytes, 2, aBytesRead)) {
      if (pRecoverMode && aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, std::string(), 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      if (!pRecoverMode && pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FNL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    const std::size_t aPathLength = static_cast<std::size_t>(ReadLeFromBytes(aWorkspace->mLengthBytes, 2));
    if (aPathLength == 0) {
      if (!pRecoverMode) {
        LogicalEndTailClassification aTailClassification = LogicalEndTailClassification::kNone;
        if (!ClassifyLogicalEndTail(
                pFileSystem, pCrypt, pArchives, aReader.Cursor(), pUseEncryption, aTailClassification, pErrorMessage)) {
          return false;
        }
        if (aTailClassification == LogicalEndTailClassification::kDanglingArchives) {
          pErrorMessage = "UNP_EOF_001: dangling archives after logical end of stream.";
          return false;
        }
        if (aTailClassification == LogicalEndTailClassification::kDanglingBytes) {
          pErrorMessage = "UNP_EOF_002: dangling bytes after logical end of stream.";
          return false;
        }
        aDone = true;
        break;
      }
      std::string aRecoverReason =
          "UNP_EOF_003: unexpected end of archive (zero-length record marker encountered during recover walk).";
      if (ReadableLogicalBytesFromCursor(aFenceDomain, aReader.Cursor()) > 0) {
        aRecoverReason =
            "UNP_EOF_003: unexpected end of archive (zero-length path marker before payload exhaustion).";
      }
      if (!aHandleRecoverFailure(aRecoverReason,
                                 nullptr,
                                 std::string(),
                                 0)) {
        return false;
      }
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    FenceProbe aPathProbe;
    aPathProbe.mRule = FenceRule::kPathLength;
    aPathProbe.mCursor = aReader.Cursor();
    aPathProbe.mValue = aPathLength;
    FenceViolation aPathViolation;
    if (FenceCheck(aFenceDomain, aPathProbe, &aPathViolation)) {
      const FenceResult aPathFence = FenceDetails(aPathProbe, aPathViolation);
      if (pRecoverMode) {
        if (!aHandleRecoverFailure(aPathFence.mMessage, nullptr, std::string(), 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aPathFence.mMessage;
      return false;
    }

    if (!aReader.Read(aWorkspace->mPathBytes, aPathLength, aBytesRead)) {
      if (pRecoverMode && aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, std::string(), 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FNL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    const std::string aRelativePath(reinterpret_cast<const char*>(aWorkspace->mPathBytes), aPathLength);
    if (!LooksLikeRelativePath(aWorkspace->mPathBytes, aPathLength)) {
      if (pRecoverMode) {
        if (!aHandleRecoverFailure("recover encountered an invalid record path fence.",
                                   nullptr,
                                   std::string(),
                                   0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = "UNP_FNL_FENCE: decoded record path failed validation.";
      return false;
    }

    if (!aReader.Read(aWorkspace->mLengthBytes, 6, aBytesRead)) {
      if (pRecoverMode && aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, aRelativePath, 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    std::uint64_t aContentLength = ReadLeFromBytes(aWorkspace->mLengthBytes, 6);
    if (aContentLength == kDirectoryRecordContentMarker) {
      if (pWriteFiles && !pFileSystem.EnsureDirectory(pFileSystem.JoinPath(pDestinationDirectory, aRelativePath))) {
        pErrorMessage = "could not create empty directory " + aRelativePath;
        return false;
      }
      ++pSummary.mEmptyDirectoriesProcessed;
      continue;
    }

    FenceProbe aContentProbe;
    aContentProbe.mRule = FenceRule::kFileContentLength;
    aContentProbe.mCursor = aReader.Cursor();
    aContentProbe.mValue = aContentLength;
    FenceViolation aContentViolation;
    if (FenceCheck(aFenceDomain, aContentProbe, &aContentViolation)) {
      const FenceResult aContentFence = FenceDetails(aContentProbe, aContentViolation);
      if (pRecoverMode) {
        if (!aHandleRecoverFailure(aContentFence.mMessage, nullptr, aRelativePath, 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aContentFence.mMessage;
      return false;
    }

    std::unique_ptr<FileWriteStream> aWriteStream;
    if (pWriteFiles) {
      const std::string aOutputPath = pFileSystem.JoinPath(pDestinationDirectory, aRelativePath);
      const std::string aParentPath = pFileSystem.ParentPath(aOutputPath);
      if (!aParentPath.empty() && aParentPath != aOutputPath && !pFileSystem.EnsureDirectory(aParentPath)) {
        pErrorMessage = "could not prepare output directory for " + aRelativePath;
        return false;
      }
      aWriteStream = pFileSystem.OpenWriteStream(aOutputPath);
      if (aWriteStream == nullptr || !aWriteStream->IsReady()) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
    }

    std::uint64_t aRecoveredFileBytes = 0;
    bool aRestarted = false;
    while (aContentLength > 0) {
      const std::size_t aChunkLength = static_cast<std::size_t>(
          std::min<std::uint64_t>(aContentLength, static_cast<std::uint64_t>(kFixedIoChunkLength)));
      aBytesRead = 0;
      if (!aReader.Read(aWorkspace->mChunk, aChunkLength, aBytesRead)) {
        if (aBytesRead > 0) {
          if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aBytesRead)) {
            pErrorMessage = "could not write " + aRelativePath;
            return false;
          }
          aRecoveredFileBytes += aBytesRead;
          aContentLength -= aBytesRead;
        }
        aLogArchiveProgress();
        if (pRecoverMode && aReader.HasRecoverableFailure()) {
          if (!aHandleRecoverFailure(std::string(), &aWriteStream, aRelativePath, aRecoveredFileBytes)) {
            return false;
          }
          aRestarted = true;
          break;
        }
        pErrorMessage = aReader.ErrorMessage();
        if (pErrorMessage.rfind("UNP_", 0) != 0) {
          pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
        }
        return false;
      }

      aLogArchiveProgress();
      if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aChunkLength)) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
      aContentLength -= aChunkLength;
      aRecoveredFileBytes += aChunkLength;
    }

    if (aRestarted) {
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    if (aWriteStream != nullptr && !aWriteStream->Close()) {
      pErrorMessage = "could not finalize " + aRelativePath;
      return false;
    }
    pSummary.mProcessedBytes += aRecoveredFileBytes;
    ++pSummary.mFilesProcessed;
  }

  aLogArchiveProgress();
  aReader.FlushRecoverLog();
  return true;
}

}  // namespace

PreflightResult CheckDecodeJob(FileSystem& pFileSystem,
                               const std::string& pJobName,
                               const std::string& pArchivePathOrDirectory,
                               const std::string& pSelectedArchiveFilePath,
                               const std::string& pDestinationDirectory) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, pSelectedArchiveFilePath);
  if (!aInputSelection.has_value()) {
    return MakeInvalid(pJobName + " Failed", pJobName + " failed: archive path does not exist.");
  }

  const std::vector<DirectoryEntry> aArchiveFiles = CollectArchiveFilesByHeaderScan(pFileSystem, aInputSelection.value());
  if (aArchiveFiles.empty()) {
    return MakeInvalid(pJobName + " Failed",
                       pJobName + " failed: no files were found to scan for archive headers.");
  }
  if (pFileSystem.DirectoryHasEntries(pDestinationDirectory)) {
    return MakeNeedsDestination(pJobName + " Destination", pJobName + " destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult RunDecodeJob(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const RuntimeSettings& pSettings,
                             const std::string& pJobName,
                             const std::string& pArchivePathOrDirectory,
                             const std::string& pSelectedArchiveFilePath,
                             const std::string& pDestinationDirectory,
                             bool pUseEncryption,
                             bool pRecoverMode,
                             DestinationAction pAction) {
  const PreflightResult aPreflight =
      CheckDecodeJob(pFileSystem, pJobName, pArchivePathOrDirectory, pSelectedArchiveFilePath, pDestinationDirectory);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, pJobName + " Canceled", pJobName + " canceled."};
  }
  if (!ApplyDestinationAction(pFileSystem, pDestinationDirectory, pAction)) {
    return MakeFailure(pLogger,
                       pJobName + " Failed",
                       pJobName + " failed: could not prepare destination directory.");
  }

  pLogger.LogStatus(pJobName + " job starting...");
  ArchiveDiscoveryResult aDiscovery;
  std::string aErrorMessage;
  if (!DiscoverArchiveSet(pFileSystem,
                          pLogger,
                          pSettings,
                          pJobName,
                          pArchivePathOrDirectory,
                          pSelectedArchiveFilePath,
                          aDiscovery,
                          aErrorMessage)) {
    return MakeFailure(pLogger, pJobName + " Failed", aErrorMessage);
  }

  if (!pRecoverMode) {
    pLogger.LogStatus("Selected archive set start = 0, count = " + std::to_string(aDiscovery.mArchives.size()) + ".");
  }

  std::vector<ArchiveHeaderRecord> aDecodeArchives = aDiscovery.mArchives;
  std::size_t aStartPhysicalOffset = 0;
  if (pRecoverMode) {
    std::size_t aStartArchiveOffset =
        aDiscovery.mSelectedArchiveOffsetValid ? aDiscovery.mSelectedArchiveOffset : 0;
    if (aStartArchiveOffset >= aDiscovery.mArchives.size()) {
      return MakeFailure(pLogger, pJobName + " Failed", pJobName + " failed: selected recovery start is outside the discovered archive set.");
    }

    aDecodeArchives.assign(aDiscovery.mArchives.begin() + static_cast<std::ptrdiff_t>(aStartArchiveOffset),
                           aDiscovery.mArchives.end());
    if (aDecodeArchives.empty()) {
      return MakeFailure(pLogger, pJobName + " Failed", pJobName + " failed: no archives were available from the selected recovery start.");
    }
    pLogger.LogStatus("Selected archive set start = " + std::to_string(aStartArchiveOffset) + ", count = " +
                      std::to_string(aDecodeArchives.size()) + ".");

    std::size_t aRecoveredArchiveOffset = 0;
    std::size_t aResolvedPhysicalOffset = 0;
    if (ResolveRecoveryStartPosition(pFileSystem,
                                     pCrypt,
                                     aDecodeArchives,
                                     pUseEncryption,
                                     aRecoveredArchiveOffset,
                                     aResolvedPhysicalOffset,
                                     aErrorMessage)) {
      pLogger.LogStatus("Recover start probe located a candidate record boundary at archive offset " +
                        std::to_string(aRecoveredArchiveOffset) + ", byte " +
                        FormatBytes(aResolvedPhysicalOffset) +
                        "; recovery walk will still process the full selected archive range.");
    } else if (!aErrorMessage.empty()) {
      pLogger.LogStatus("Recover start probe did not locate a valid boundary (" + aErrorMessage +
                        "); recovery walk will continue from the selected archive start.");
    }

    aStartPhysicalOffset = 0;
  }

  DecodeSummary aSummary;
  if (!DecodeArchiveSet(pFileSystem,
                        pCrypt,
                        pLogger,
                        aDecodeArchives,
                        aStartPhysicalOffset,
                        pDestinationDirectory,
                        true,
                        pUseEncryption,
                        pRecoverMode,
                        aSummary,
                        aErrorMessage)) {
    return MakeFailure(pLogger, pJobName + " Failed", pJobName + " failed: " + aErrorMessage);
  }

  if (aSummary.mEmptyDirectoriesProcessed > 0) {
    pLogger.LogStatus("Successfully unpacked " + std::to_string(aSummary.mEmptyDirectoriesProcessed) +
                      " empty directories.");
  }

  pLogger.LogStatus((pRecoverMode ? "Recovered archive " : "Unbundled archive ") +
                    std::to_string(aDecodeArchives.size()) + " / " +
                    std::to_string(aDecodeArchives.size()) + ", " +
                    std::to_string(aSummary.mFilesProcessed) + " files written, " +
                    FormatBytes(aSummary.mProcessedBytes) + " / " + FormatBytes(aSummary.mProcessedBytes) + ".");
  pLogger.LogStatus(pJobName + " job complete.");
  return {true, pJobName + " Complete", pJobName + " completed successfully."};
}

PreflightResult CheckUnbundleJob(FileSystem& pFileSystem, const UnbundleRequest& pRequest) {
  return CheckDecodeJob(pFileSystem,
                        "Unbundle",
                        pRequest.mArchiveDirectory,
                        std::string(),
                        pRequest.mDestinationDirectory);
}

OperationResult RunUnbundleJob(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               Logger& pLogger,
                               const RuntimeSettings& pSettings,
                               const UnbundleRequest& pRequest,
                               DestinationAction pAction) {
  return RunDecodeJob(pFileSystem,
                      pCrypt,
                      pLogger,
                      pSettings,
                      "Unbundle",
                      pRequest.mArchiveDirectory,
                      std::string(),
                      pRequest.mDestinationDirectory,
                      pRequest.mUseEncryption,
                      false,
                      pAction);
}

}  // namespace peanutbutter::detail
