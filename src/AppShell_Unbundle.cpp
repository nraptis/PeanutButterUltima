#include "AppShell_Unbundle.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace {

struct ArchiveCandidate {
  std::string mPath;
  std::string mFileName;
  std::uint32_t mArchiveIndex = 0u;
  std::size_t mDigitWidth = 0u;
  std::size_t mFileLength = 0u;
  std::uint32_t mBlockCount = 0u;
  ArchiveHeader mHeader{};
  bool mHasReadableHeader = false;
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
  aResult.mFailureMessage = "Decode canceled by user.";
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

bool IsSafeRelativePath(const std::string& pPath) {
  if (pPath.empty() || pPath.size() > kMaxPathLength) {
    return false;
  }
  if (pPath[0] == '/' || pPath[0] == '\\') {
    return false;
  }
  if (pPath.size() > 2u &&
      std::isalpha(static_cast<unsigned char>(pPath[0])) != 0 &&
      pPath[1] == ':') {
    return false;
  }

  std::size_t aStart = 0u;
  while (aStart < pPath.size()) {
    std::size_t aEnd = pPath.find_first_of("/\\", aStart);
    if (aEnd == std::string::npos) {
      aEnd = pPath.size();
    }
    if (aEnd == aStart) {
      return false;
    }
    const std::string aPart = pPath.substr(aStart, aEnd - aStart);
    if (aPart == "." || aPart == "..") {
      return false;
    }
    for (char aChar : aPart) {
      const unsigned char aByte = static_cast<unsigned char>(aChar);
      if (aByte < 32u || aByte == 127u) {
        return false;
      }
    }
    aStart = aEnd + 1u;
  }
  return true;
}

struct DiscoverySelection {
  std::vector<ArchiveCandidate> mArchives;
  std::vector<ArchiveFileBox> mArchiveBoxes;
  std::uint32_t mMinIndex = 0u;
  std::uint32_t mMaxIndex = 0u;
  std::size_t mGapCount = 0u;
  std::size_t mClippedArchiveCount = 0u;
};

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
  if (!ParseArchiveFileTemplate(pOutCandidate.mFileName, aPrefix, aIndex, aSuffix, aDigits)) {
    return false;
  }
  pOutCandidate.mArchiveIndex = aIndex;
  pOutCandidate.mDigitWidth = aDigits;

  if (pOutCandidate.mFileLength >= kArchiveHeaderLength) {
    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    if (aRead->Read(0u, aHeaderBytes, sizeof(aHeaderBytes)) &&
        ReadArchiveHeaderBytes(aHeaderBytes, sizeof(aHeaderBytes), pOutCandidate.mHeader)) {
      pOutCandidate.mHasReadableHeader = true;
    }
  }

  if (pOutCandidate.mFileLength > kArchiveHeaderLength) {
    const std::size_t aReadablePayload = pOutCandidate.mFileLength - kArchiveHeaderLength;
    pOutCandidate.mBlockCount = static_cast<std::uint32_t>(aReadablePayload / kBlockSizeL3);
    if (pOutCandidate.mHasReadableHeader &&
        (pOutCandidate.mHeader.mPayloadLength % kBlockSizeL3) == 0u) {
      const std::uint32_t aHeaderBlocks = static_cast<std::uint32_t>(pOutCandidate.mHeader.mPayloadLength / kBlockSizeL3);
      if (aHeaderBlocks > 0u) {
        pOutCandidate.mBlockCount = std::min(pOutCandidate.mBlockCount, aHeaderBlocks);
      }
    }
  }
  return true;
}

bool SelectArchiveFamily(const std::vector<std::string>& pArchiveFileList,
                         FileSystem& pFileSystem,
                         Logger& pLogger,
                         DiscoverySelection& pOutSelection,
                         CancelCoordinator* pCancelCoordinator) {
  pOutSelection = DiscoverySelection{};

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

    if ((aIndex + 1u) % static_cast<std::size_t>(std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault)) == 0u) {
      pLogger.LogStatus("[Decode][Discovery] Scanned " + std::to_string(aIndex + 1u) + "/" +
                        std::to_string(aInput.size()) + " candidate archives (" +
                        FormatHumanBytes(aScannedBytes) + ").");
    }
  }

  if (aParsed.empty()) {
    pLogger.LogError("[Decode][Discovery] no archive candidates matched filename template parsing.");
    return false;
  }

  pLogger.LogStatus("[Decode][Discovery] Found " + std::to_string(aParsed.size()) +
                    " candidate archives (" + FormatHumanBytes(aScannedBytes) + ").");

  std::string aAnchorPrefix;
  std::string aAnchorSuffix;
  std::uint32_t aAnchorIndex = 0u;
  std::size_t aAnchorDigits = 0u;
  if (!ParseArchiveFileTemplate(aParsed.front().mFileName, aAnchorPrefix, aAnchorIndex, aAnchorSuffix, aAnchorDigits)) {
    return false;
  }

  pLogger.LogStatus("[Decode][Discovery] Filename template anchor: '" + aParsed.front().mFileName + "'.");

  std::unordered_map<std::uint32_t, ArchiveCandidate> aByIndex;
  std::size_t aIgnored = 0u;
  for (ArchiveCandidate& aCandidate : aParsed) {
    std::string aPrefix;
    std::string aSuffix;
    std::uint32_t aIndex = 0u;
    std::size_t aDigits = 0u;
    if (!ParseArchiveFileTemplate(aCandidate.mFileName, aPrefix, aIndex, aSuffix, aDigits)) {
      ++aIgnored;
      continue;
    }
    if (aPrefix != aAnchorPrefix || aSuffix != aAnchorSuffix) {
      ++aIgnored;
      continue;
    }

    auto aExisting = aByIndex.find(aIndex);
    if (aExisting == aByIndex.end()) {
      aByIndex.emplace(aIndex, std::move(aCandidate));
      continue;
    }
    if (!aExisting->second.mHasReadableHeader && aCandidate.mHasReadableHeader) {
      aExisting->second = std::move(aCandidate);
    } else if (aCandidate.mFileLength > aExisting->second.mFileLength) {
      aExisting->second = std::move(aCandidate);
    }
  }

  if (aByIndex.empty()) {
    pLogger.LogError("[Decode][Discovery] anchor template did not retain any archives.");
    return false;
  }

  std::vector<std::uint32_t> aIndexes;
  aIndexes.reserve(aByIndex.size());
  for (const auto& aPair : aByIndex) {
    aIndexes.push_back(aPair.first);
  }
  std::sort(aIndexes.begin(), aIndexes.end());

  const std::uint32_t aWindowMin = aIndexes.front();
  const std::uint64_t aWindowMax64 = static_cast<std::uint64_t>(aWindowMin) +
                                     static_cast<std::uint64_t>(kMaxArchiveCount) - 1u;
  const std::uint32_t aWindowMax =
      static_cast<std::uint32_t>(std::min<std::uint64_t>(
          aWindowMax64, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));

  pOutSelection.mMinIndex = aWindowMin;
  pOutSelection.mMaxIndex = aWindowMin;
  pOutSelection.mGapCount = 0u;
  pOutSelection.mClippedArchiveCount = 0u;

  std::uint32_t aPreviousSelected = aWindowMin;
  bool aHasPrevious = false;
  pOutSelection.mArchives.reserve(std::min<std::size_t>(aIndexes.size(), static_cast<std::size_t>(kMaxArchiveCount)));
  for (std::uint32_t aIndex : aIndexes) {
    if (aIndex < aWindowMin || aIndex > aWindowMax) {
      ++pOutSelection.mClippedArchiveCount;
      continue;
    }
    if (aHasPrevious && aIndex > aPreviousSelected + 1u) {
      pOutSelection.mGapCount += static_cast<std::size_t>(aIndex - aPreviousSelected - 1u);
    }
    aPreviousSelected = aIndex;
    aHasPrevious = true;
    pOutSelection.mMaxIndex = std::max<std::uint32_t>(pOutSelection.mMaxIndex, aIndex);
    pOutSelection.mArchives.push_back(std::move(aByIndex[aIndex]));
  }

  if (pOutSelection.mArchives.empty()) {
    pLogger.LogError("[Decode][Discovery] no archives remained after MAX_ARCHIVE_COUNT clipping.");
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
    const std::uint32_t aDeclaredMaxClamped = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        aDeclaredMax64, static_cast<std::uint64_t>(aWindowMax)));
    ++aHeaderDeclaredMaxVotes[aDeclaredMaxClamped];
  }

  std::uint32_t aHeaderDeclaredMax = pOutSelection.mMaxIndex;
  std::size_t aHeaderDeclaredMaxVoteCount = 0u;
  for (const auto& aVote : aHeaderDeclaredMaxVotes) {
    if (aVote.second > aHeaderDeclaredMaxVoteCount ||
        (aVote.second == aHeaderDeclaredMaxVoteCount && aVote.first > aHeaderDeclaredMax)) {
      aHeaderDeclaredMax = aVote.first;
      aHeaderDeclaredMaxVoteCount = aVote.second;
    }
  }
  if (aHeaderDeclaredMax > pOutSelection.mMaxIndex) {
    const std::size_t aTailGaps =
        static_cast<std::size_t>(aHeaderDeclaredMax - pOutSelection.mMaxIndex);
    pOutSelection.mGapCount += aTailGaps;
    pOutSelection.mMaxIndex = aHeaderDeclaredMax;
    pLogger.LogStatus("[Decode][Discovery] Header-declared archive count implies " +
                      std::to_string(aTailGaps) + " missing tail archive indices.");
  }

  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(kPayloadBytesPerL3);
  pOutSelection.mArchiveBoxes.reserve(
      static_cast<std::size_t>(pOutSelection.mMaxIndex - pOutSelection.mMinIndex) + 1u);
  for (std::uint32_t aSequence = pOutSelection.mMinIndex;
       aSequence <= pOutSelection.mMaxIndex;
       ++aSequence) {
    ArchiveFileBox aBox{};
    aBox.mSequenceJumber = aSequence;
    aBox.mPayloadStart = static_cast<std::uint64_t>(aSequence) * aPayloadBytesPerArchive;
    aBox.mPayloadLength = aPayloadBytesPerArchive;
    aBox.mEmpty = true;
    pOutSelection.mArchiveBoxes.push_back(aBox);
    if (aSequence == std::numeric_limits<std::uint32_t>::max()) {
      break;
    }
  }

  for (const ArchiveCandidate& aArchive : pOutSelection.mArchives) {
    const std::size_t aOffset = static_cast<std::size_t>(aArchive.mArchiveIndex - pOutSelection.mMinIndex);
    if (aOffset >= pOutSelection.mArchiveBoxes.size()) {
      continue;
    }
    pOutSelection.mArchiveBoxes[aOffset].mEmpty = false;
    pOutSelection.mArchiveBoxes[aOffset].mPayloadLength =
        static_cast<std::uint64_t>(aArchive.mBlockCount) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
  }

  pLogger.LogStatus("[Decode][Discovery] Filename template matched " +
                    std::to_string(pOutSelection.mArchives.size()) +
                    " archives with " + std::to_string(pOutSelection.mGapCount) + " index gaps.");
  if (pOutSelection.mClippedArchiveCount > 0u) {
    pLogger.LogStatus("[Decode][Discovery] Clipped " + std::to_string(pOutSelection.mClippedArchiveCount) +
                      " archives beyond MAX_ARCHIVE_COUNT (" + std::to_string(kMaxArchiveCount) + ").");
  }
  if (aIgnored > 0u) {
    pLogger.LogStatus("[Decode][Discovery] Ignored " + std::to_string(aIgnored) +
                      " candidate archives outside the filename template.");
  }
  pLogger.LogStatus("[Decode][Discovery] Chose " + std::to_string(pOutSelection.mArchives.size()) +
                    " archive candidates after filename discovery DONE.");
  return true;
}

class DecodeParser final {
 public:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
  };

  DecodeParser(const std::string& pDestinationDirectory,
               FileSystem& pFileSystem,
               Logger& pLogger)
      : mDestinationDirectory(pDestinationDirectory),
        mFileSystem(pFileSystem),
        mLogger(pLogger) {}

  bool Consume(const unsigned char* pData,
               std::size_t pStart,
               std::size_t pEnd,
               bool pRecoverMode,
               bool& pOutTerminated,
               bool& pOutParseError,
               std::string& pOutParseErrorMessage,
               bool& pOutCanceledAtBoundary,
               std::uint64_t& pOutDataBytesWritten) {
    pOutTerminated = false;
    pOutParseError = false;
    pOutParseErrorMessage.clear();
    pOutCanceledAtBoundary = false;
    pOutDataBytesWritten = 0u;

    if (pData == nullptr || pStart > pEnd || pEnd > kBlockSizeL3) {
      pOutParseError = true;
      pOutParseErrorMessage = "invalid payload span while decoding.";
      return true;
    }

    std::size_t aOffset = pStart;
    while (aOffset < pEnd) {
      switch (mStage) {
        case Stage::kPathLength: {
          while (mPathLengthBytesUsed < 2u && aOffset < pEnd) {
            mPathLengthLe[mPathLengthBytesUsed++] = pData[aOffset++];
          }
          if (mPathLengthBytesUsed < 2u) {
            return true;
          }
          mCurrentPathLength = static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(mPathLengthLe[0]) |
              (static_cast<std::uint16_t>(mPathLengthLe[1]) << 8));
          if (mCurrentPathLength == 0u) {
            pOutTerminated = true;
            return true;
          }
          if (mCurrentPathLength > kMaxPathLength) {
            pOutParseError = true;
            pOutParseErrorMessage = "path length exceeds kMaxPathLength.";
            return true;
          }
          mCurrentPath.clear();
          mCurrentPath.reserve(mCurrentPathLength);
          mPathBytesUsed = 0u;
          mStage = Stage::kPathBytes;
          break;
        }

        case Stage::kPathBytes: {
          const std::size_t aRemaining = mCurrentPathLength - mPathBytesUsed;
          const std::size_t aChunk = std::min<std::size_t>(aRemaining, pEnd - aOffset);
          mCurrentPath.append(reinterpret_cast<const char*>(pData + aOffset), aChunk);
          mPathBytesUsed += aChunk;
          aOffset += aChunk;
          if (mPathBytesUsed < mCurrentPathLength) {
            return true;
          }
          if (!IsSafeRelativePath(mCurrentPath)) {
            pOutParseError = true;
            pOutParseErrorMessage = "path payload failed safety validation.";
            return true;
          }
          mContentLengthBytesUsed = 0u;
          std::memset(mContentLengthLe, 0, sizeof(mContentLengthLe));
          mStage = Stage::kContentLength;
          break;
        }

        case Stage::kContentLength: {
          while (mContentLengthBytesUsed < 8u && aOffset < pEnd) {
            mContentLengthLe[mContentLengthBytesUsed++] = pData[aOffset++];
          }
          if (mContentLengthBytesUsed < 8u) {
            return true;
          }
          mCurrentContentLength = 0u;
          for (int aByte = 0; aByte < 8; ++aByte) {
            mCurrentContentLength |=
                static_cast<std::uint64_t>(mContentLengthLe[static_cast<std::size_t>(aByte)]) << (8 * aByte);
          }

          if (mCurrentContentLength == kDirectoryRecordContentMarker) {
            const std::string aDirPath = mFileSystem.JoinPath(mDestinationDirectory, mCurrentPath);
            if (!mFileSystem.EnsureDirectory(aDirPath)) {
              pOutParseError = true;
              pOutParseErrorMessage = "failed creating directory: " + aDirPath;
              return false;
            }
            ++mFoldersCreated;
            ResetRecordState();
            break;
          }

          if (mCurrentContentLength > (1ull << 40)) {
            pOutParseError = true;
            pOutParseErrorMessage = "content length is implausibly large for a recoverable record.";
            return true;
          }

          const std::string aOutPath = mFileSystem.JoinPath(mDestinationDirectory, mCurrentPath);
          const std::string aOutParent = mFileSystem.ParentPath(aOutPath);
          if (!aOutParent.empty() && !mFileSystem.EnsureDirectory(aOutParent)) {
            pOutParseError = true;
            pOutParseErrorMessage = "failed creating parent directory for output file.";
            return false;
          }

          mCurrentWrite = mFileSystem.OpenWriteStream(aOutPath);
          if (mCurrentWrite == nullptr || !mCurrentWrite->IsReady()) {
            pOutParseError = true;
            pOutParseErrorMessage = "failed opening output file for writing.";
            return false;
          }

          mCurrentOutputPath = aOutPath;
          mCurrentFileDisplayName = RecordDisplayName(mFileSystem, mCurrentPath);
          mContentBytesRemaining = mCurrentContentLength;

          mStage = Stage::kContentBytes;
          break;
        }

        case Stage::kContentBytes: {
          const std::size_t aChunk = static_cast<std::size_t>(
              std::min<std::uint64_t>(mContentBytesRemaining, static_cast<std::uint64_t>(pEnd - aOffset)));
          if (aChunk == 0u) {
            FinishFileRecord(pOutCanceledAtBoundary, pRecoverMode);
            if (pOutCanceledAtBoundary) {
              return true;
            }
            break;
          }

          if (!mCurrentWrite->Write(pData + aOffset, aChunk)) {
            pOutParseError = true;
            pOutParseErrorMessage = "failed writing decoded file bytes.";
            return false;
          }
          aOffset += aChunk;
          mContentBytesRemaining -= static_cast<std::uint64_t>(aChunk);
          mBytesWritten += static_cast<std::uint64_t>(aChunk);
          pOutDataBytesWritten += static_cast<std::uint64_t>(aChunk);

          if (mContentBytesRemaining == 0u) {
            FinishFileRecord(pOutCanceledAtBoundary, pRecoverMode);
            if (pOutCanceledAtBoundary) {
              return true;
            }
          }
          break;
        }
      }
    }

    return true;
  }

  bool IsAtRecordBoundary() const {
    return mStage == Stage::kPathLength && mPathLengthBytesUsed == 0u && mCurrentWrite == nullptr;
  }

  bool IsInsideFile() const {
    return mStage == Stage::kContentBytes && mCurrentWrite != nullptr;
  }

  void RequestStopAfterCurrentFile() {
    mStopAfterCurrentFile = true;
  }

  void AbortPartialFile() {
    if (mCurrentWrite != nullptr) {
      (void)mCurrentWrite->Close();
      mCurrentWrite.reset();
    }
  }

  void ResetRecordState() {
    mStage = Stage::kPathLength;
    mPathLengthBytesUsed = 0u;
    mPathBytesUsed = 0u;
    mContentLengthBytesUsed = 0u;
    mCurrentPathLength = 0u;
    mCurrentContentLength = 0u;
    mContentBytesRemaining = 0u;
    std::memset(mPathLengthLe, 0, sizeof(mPathLengthLe));
    std::memset(mContentLengthLe, 0, sizeof(mContentLengthLe));
    mCurrentPath.clear();
    mCurrentOutputPath.clear();
    mCurrentFileDisplayName.clear();
  }

  std::uint64_t FilesWritten() const { return mFilesWritten; }
  std::uint64_t FoldersCreated() const { return mFoldersCreated; }
  std::uint64_t BytesWritten() const { return mBytesWritten; }

 private:
  void FinishFileRecord(bool& pOutCanceledAtBoundary, bool) {
    if (mCurrentWrite != nullptr) {
      (void)mCurrentWrite->Close();
      mCurrentWrite.reset();
    }

    mLogger.LogStatus("[Decode][Flight] File written: [[ " + mCurrentFileDisplayName + " ]].");
    ++mFilesWritten;

    if (mStopAfterCurrentFile) {
      pOutCanceledAtBoundary = true;
      return;
    }

    ResetRecordState();
  }

 private:
  std::string mDestinationDirectory;
  FileSystem& mFileSystem;
  Logger& mLogger;

  Stage mStage = Stage::kPathLength;
  unsigned char mPathLengthLe[2] = {};
  std::size_t mPathLengthBytesUsed = 0u;
  std::uint16_t mCurrentPathLength = 0u;
  std::size_t mPathBytesUsed = 0u;
  std::string mCurrentPath;

  unsigned char mContentLengthLe[8] = {};
  std::size_t mContentLengthBytesUsed = 0u;
  std::uint64_t mCurrentContentLength = 0u;
  std::uint64_t mContentBytesRemaining = 0u;

  std::unique_ptr<FileWriteStream> mCurrentWrite;
  std::string mCurrentOutputPath;
  std::string mCurrentFileDisplayName;

  bool mStopAfterCurrentFile = false;

  std::uint64_t mFilesWritten = 0u;
  std::uint64_t mFoldersCreated = 0u;
  std::uint64_t mBytesWritten = 0u;
};

struct Cursor {
  std::uint32_t mArchiveIndex = 0u;
  std::uint32_t mBlockIndex = 0u;
  std::size_t mPayloadOffset = kRecoveryHeaderLength;
};

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
  if (pSkip.mArchiveDistance == 0u && pSkip.mBlockDistance == 0u && pSkip.mByteDistance == 0u) {
    return false;
  }

  const std::uint64_t aDeltaBlocks =
      static_cast<std::uint64_t>(pSkip.mArchiveDistance) * static_cast<std::uint64_t>(pBlocksPerArchive) +
      static_cast<std::uint64_t>(pSkip.mBlockDistance);

  const std::uint64_t aCurrentGlobalBlock =
      static_cast<std::uint64_t>(pCurrent.mArchiveIndex) * static_cast<std::uint64_t>(pBlocksPerArchive) +
      static_cast<std::uint64_t>(pCurrent.mBlockIndex);
  const std::uint64_t aTargetGlobalBlock = aCurrentGlobalBlock + aDeltaBlocks;
  const std::uint64_t aTargetPayloadOffset = static_cast<std::uint64_t>(pSkip.mByteDistance);

  const std::uint32_t aTargetArchiveIndex =
      static_cast<std::uint32_t>(aTargetGlobalBlock / static_cast<std::uint64_t>(pBlocksPerArchive));
  const std::uint32_t aTargetBlockIndex =
      static_cast<std::uint32_t>(aTargetGlobalBlock % static_cast<std::uint64_t>(pBlocksPerArchive));
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

OperationResult RunDecode(const UnbundleRequest& pRequest,
                          const std::vector<std::string>& pArchiveFileList,
                          bool pRecoverMode,
                          FileSystem& pFileSystem,
                          const Crypt& pCrypt,
                          Logger& pLogger,
                          CancelCoordinator* pCancelCoordinator) {
  if (pRequest.mDestinationDirectory.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "destination directory is required.", pLogger);
  }
  if (!pFileSystem.EnsureDirectory(pRequest.mDestinationDirectory)) {
    return MakeFailure(ErrorCode::kFileSystem, "failed to create decode destination directory.", pLogger);
  }

  DiscoverySelection aSelection;
  if (!SelectArchiveFamily(pArchiveFileList, pFileSystem, pLogger, aSelection, pCancelCoordinator)) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested()) {
      return MakeCanceled();
    }
    return MakeFailure(ErrorCode::kInvalidRequest,
                       "decode discovery could not find a usable archive family.",
                       pLogger);
  }

  if (aSelection.mArchives.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "decode plan contains no archives.", pLogger);
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
      aBlocksPerArchive = static_cast<std::uint32_t>(aCandidate.mHeader.mPayloadLength / kBlockSizeL3);
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
      static_cast<std::uint64_t>(aBlocksPerArchive) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
  for (ArchiveFileBox& aBox : aSelection.mArchiveBoxes) {
    aBox.mPayloadStart = static_cast<std::uint64_t>(aBox.mSequenceJumber) * aPayloadBytesPerArchive;
    if (aBox.mEmpty) {
      aBox.mPayloadLength = aPayloadBytesPerArchive;
      continue;
    }
    const auto aCandidateIt = aByIndex.find(aBox.mSequenceJumber);
    if (aCandidateIt != aByIndex.end()) {
      aBox.mPayloadLength =
          static_cast<std::uint64_t>(aCandidateIt->second.mBlockCount) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
    } else {
      aBox.mEmpty = true;
      aBox.mPayloadLength = aPayloadBytesPerArchive;
    }
  }

  pLogger.LogStatus("[Decode] Successfully unpacked 0 files (0B) START.");

  DecodeParser aParser(pRequest.mDestinationDirectory, pFileSystem, pLogger);
  L3BlockBuffer aEncrypted{};
  L3BlockBuffer aPlain{};
  L3BlockBuffer aWorker{};

  std::uint64_t aFailedBlocks = 0u;
  std::uint64_t aJumpCount = 0u;
  std::uint64_t aGapBoxesEncountered = 0u;
  std::uint64_t aNextByteLog = std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
  const auto aStart = std::chrono::steady_clock::now();

  Cursor aCursor{};
  aCursor.mArchiveIndex = aSelection.mMinIndex;
  aCursor.mBlockIndex = 0u;
  aCursor.mPayloadOffset = kRecoveryHeaderLength;

  while (aCursor.mArchiveIndex <= aSelection.mMaxIndex) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested()) {
      if (aParser.IsInsideFile()) {
        aParser.RequestStopAfterCurrentFile();
      } else if (pCancelCoordinator->ShouldCancelNow()) {
        return MakeCanceled();
      }
    }

    if (aCursor.mArchiveIndex < aSelection.mMinIndex || aCursor.mArchiveIndex > aSelection.mMaxIndex) {
      break;
    }
    const std::size_t aBoxOffset = static_cast<std::size_t>(aCursor.mArchiveIndex - aSelection.mMinIndex);
    if (aBoxOffset >= aSelection.mArchiveBoxes.size()) {
      break;
    }
    const ArchiveFileBox& aBox = aSelection.mArchiveBoxes[aBoxOffset];
    if (aBox.mEmpty) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kGap001,
                           "encountered empty ArchiveFileBox while decoding (sequence " +
                               std::to_string(aCursor.mArchiveIndex) + ").",
                           pLogger);
      }
      ++aGapBoxesEncountered;
      aFailedBlocks += static_cast<std::uint64_t>(aBlocksPerArchive);
      if (aGapBoxesEncountered <= 3u || (aGapBoxesEncountered % 500u) == 0u) {
        pLogger.LogStatus("[Recover] GAP_001 at sequence " +
                          std::to_string(aCursor.mArchiveIndex) + "; skipping empty ArchiveFileBox.");
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
        return MakeFailure(ErrorCode::kGap001,
                           "encountered empty ArchiveFileBox while decoding (sequence " +
                               std::to_string(aCursor.mArchiveIndex) + ").",
                           pLogger);
      }
      ++aFailedBlocks;
      ++aCursor.mArchiveIndex;
      aCursor.mBlockIndex = 0u;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const ArchiveCandidate& aArchive = aArchiveIt->second;
    if (aCursor.mBlockIndex >= aArchive.mBlockCount) {
      ++aCursor.mArchiveIndex;
      aCursor.mBlockIndex = 0u;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(aArchive.mPath);
    if (aRead == nullptr || !aRead->IsReady()) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kFileSystem,
                           "failed opening archive for decode: " + aArchive.mPath,
                           pLogger);
      }
      ++aFailedBlocks;
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const std::size_t aBlockOffset = kArchiveHeaderLength +
                                     (static_cast<std::size_t>(aCursor.mBlockIndex) * kBlockSizeL3);
    if (aBlockOffset + kBlockSizeL3 > aArchive.mFileLength) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kArchiveHeader,
                           "archive is too short or unreadable for block parsing.",
                           pLogger);
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
        return MakeFailure(ErrorCode::kFileSystem,
                           "failed reading archive block bytes.",
                           pLogger);
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    if (pRequest.mUseEncryption) {
      std::string aCryptError;
      if (!pCrypt.UnsealData(aEncrypted.Data(), aWorker.Data(), aPlain.Data(), kBlockSizeL3, &aCryptError, CryptMode::kNormal)) {
        if (!pRecoverMode) {
          return MakeFailure(ErrorCode::kCrypt,
                             aCryptError.empty() ? "decryption failed." : aCryptError,
                             pLogger);
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
    if (!ReadRecoveryHeaderBytes(aPlain.Data(), kRecoveryHeaderLength, aRecoveryHeader)) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kArchiveHeader,
                           "failed parsing recovery header.",
                           pLogger);
      }
      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();
      ++aCursor.mBlockIndex;
      aCursor.mPayloadOffset = kRecoveryHeaderLength;
      continue;
    }

    const Checksum aExpectedChecksum = ComputeRecoveryChecksum(aPlain.Data(), aRecoveryHeader.mSkip);
    if (!ChecksumsEqual(aExpectedChecksum, aRecoveryHeader.mChecksum)) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kBlockChecksum,
                           "block checksum mismatch.",
                           pLogger);
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
      return MakeFailure(ErrorCode::kFileSystem,
                         aParseErrorMessage.empty() ? "decode write failure." : aParseErrorMessage,
                         pLogger);
    }

    while (aParser.BytesWritten() >= aNextByteLog) {
      pLogger.LogStatus("[Decode] Successfully unpacked " +
                        std::to_string(aParser.FilesWritten()) +
                        " files (" + FormatHumanBytes(aParser.BytesWritten()) + ").");
      aNextByteLog += std::max<std::uint64_t>(1u, kProgressByteLogIntervalDefault);
    }

    if (aCanceledAtBoundary) {
      return MakeCanceled();
    }

    if (aTerminated) {
      pLogger.LogStatus("[Decode] Successfully unpacked " +
                        std::to_string(aParser.FilesWritten()) +
                        " files (" + FormatHumanBytes(aParser.BytesWritten()) + ") DONE.");

      if (pRecoverMode) {
        const auto aElapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - aStart);
        pLogger.LogStatus("[Recover] Summary: Successfully recovered " +
                          std::to_string(aParser.FilesWritten()) + " files (" +
                          FormatHumanBytes(aParser.BytesWritten()) + ").");
        pLogger.LogStatus("[Recover] Summary: Failed to recover " +
                          std::to_string(aFailedBlocks) + " blocks from " +
                          std::to_string(aSelection.mArchiveBoxes.size()) + " archive boxes.");
        pLogger.LogStatus("[Recover] Summary: Total time was " +
                          FormatHumanDurationSeconds(static_cast<std::uint64_t>(aElapsed.count())) + ".");
        pLogger.LogStatus("[Recover] Summary: Output directory was '" +
                          pRequest.mDestinationDirectory + "'.");
      }
      return MakeSuccess();
    }

    if (aParseError) {
      if (!pRecoverMode) {
        return MakeFailure(ErrorCode::kRecordParse,
                           aParseErrorMessage.empty() ? "record parse failed." : aParseErrorMessage,
                           pLogger);
      }

      ++aFailedBlocks;
      aParser.AbortPartialFile();
      aParser.ResetRecordState();

      Cursor aJumpTarget{};
      if (ResolveStrideJump(aCursor,
                            aRecoveryHeader.mSkip,
                            aBlocksPerArchive,
                            aByIndex,
                            aJumpTarget)) {
        ++aJumpCount;
        if (aJumpCount <= 3u || (aJumpCount % 500u) == 0u) {
          pLogger.LogStatus("[Recover] Honoring recovery stride.");
          pLogger.LogStatus("[Recover] Jumping to '" + aByIndex[aJumpTarget.mArchiveIndex].mFileName +
                            "' at byte " +
                            std::to_string(kArchiveHeaderLength +
                                           (aJumpTarget.mBlockIndex * static_cast<std::uint32_t>(kBlockSizeL3)) +
                                           static_cast<std::uint32_t>(aJumpTarget.mPayloadOffset)) + ".");
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
                      std::to_string(aSelection.mArchiveBoxes.size()) + " archive boxes.");
    pLogger.LogStatus("[Recover] Summary: Total time was " +
                      FormatHumanDurationSeconds(static_cast<std::uint64_t>(aElapsed.count())) + ".");
    pLogger.LogStatus("[Recover] Summary: Output directory was '" +
                      pRequest.mDestinationDirectory + "'.");

    if (aParser.FilesWritten() > 0u) {
      return MakeSuccess();
    }
    return MakeFailure(ErrorCode::kRecoverExhausted,
                       "recover exhausted archive space without finding a terminator.",
                       pLogger);
  }

  return MakeFailure(ErrorCode::kArchiveHeader,
                     "decode exhausted archive space before logical terminator.",
                     pLogger);
}

}  // namespace

OperationResult Unbundle(const UnbundleRequest& pRequest,
                         const std::vector<std::string>& pArchiveFileList,
                         FileSystem& pFileSystem,
                         const Crypt& pCrypt,
                         Logger& pLogger,
                         CancelCoordinator* pCancelCoordinator) {
  return RunDecode(pRequest,
                   pArchiveFileList,
                   false,
                   pFileSystem,
                   pCrypt,
                   pLogger,
                   pCancelCoordinator);
}

OperationResult Recover(const UnbundleRequest& pRequest,
                        const std::vector<std::string>& pArchiveFileList,
                        FileSystem& pFileSystem,
                        const Crypt& pCrypt,
                        Logger& pLogger,
                        CancelCoordinator* pCancelCoordinator) {
  return RunDecode(pRequest,
                   pArchiveFileList,
                   true,
                   pFileSystem,
                   pCrypt,
                   pLogger,
                   pCancelCoordinator);
}

}  // namespace peanutbutter
