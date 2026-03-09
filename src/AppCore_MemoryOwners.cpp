#include "AppCore_MemoryOwners.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace peanutbutter {

namespace {

constexpr std::size_t kRecoveryHeaderLength = peanutbutter::SB_RECOVERY_HEADER_LENGTH;
constexpr std::size_t kPayloadBytesPerL1 = peanutbutter::SB_PAYLOAD_SIZE;
constexpr std::size_t kL1Length = peanutbutter::SB_L1_LENGTH;
constexpr std::size_t kL3Length = peanutbutter::SB_L3_LENGTH;
constexpr std::uint64_t kSafetyMaxDecodedFileContentBytes = 64ull * 1024ull * 1024ull * 1024ull;
constexpr std::size_t kFenceMaxStateTransitionsPerChunkMultiplier = 32;
constexpr std::size_t kFenceMinStateTransitionsPerChunk = 4096;
constexpr std::size_t kFenceMaxParseReadIterations = 10000000;
constexpr unsigned char kSpecialFirstRecoveryHeader[kRecoveryHeaderLength] = {
    0x76, 0x47, 0xb2, 0x1d, 0xef, 0x8e};

void WriteLeToBuffer(unsigned char* pBytes, std::uint64_t pValue, std::size_t pWidth) {
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    pBytes[aIndex] = static_cast<unsigned char>((pValue >> (8 * aIndex)) & 0xFFU);
  }
}

std::size_t LogicalCapacityForPhysicalLength(std::size_t pPhysicalLength) {
  const std::size_t aFullBlocks = pPhysicalLength / kL1Length;
  const std::size_t aRemainder = pPhysicalLength % kL1Length;
  const std::size_t aPartialPayloadLength =
      aRemainder > kRecoveryHeaderLength ? aRemainder - kRecoveryHeaderLength : 0;
  return (aFullBlocks * kPayloadBytesPerL1) + aPartialPayloadLength;
}

std::size_t PhysicalOffsetForLogicalOffset(std::size_t pLogicalOffset) {
  const std::size_t aBlockIndex = pLogicalOffset / kPayloadBytesPerL1;
  const std::size_t aOffsetInBlock = pLogicalOffset % kPayloadBytesPerL1;
  return (aBlockIndex * kL1Length) + kRecoveryHeaderLength + aOffsetInBlock;
}

std::uint64_t ReadLeFromBuffer(const unsigned char* pBytes, std::size_t pWidth) {
  std::uint64_t aValue = 0;
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    aValue |= static_cast<std::uint64_t>(pBytes[aIndex]) << (8 * aIndex);
  }
  return aValue;
}

std::uint64_t CountLogicalBytesInPayload(std::size_t pPayloadLength, std::size_t pPayloadOffset) {
  std::uint64_t aLogicalBytes = 0;
  while (pPayloadOffset < pPayloadLength) {
    if (pPayloadOffset % kL1Length == 0) {
      if (pPayloadOffset + kRecoveryHeaderLength > pPayloadLength) {
        break;
      }
      pPayloadOffset += kRecoveryHeaderLength;
      continue;
    }
    const std::size_t aBlockRemaining = kL1Length - (pPayloadOffset % kL1Length);
    const std::size_t aPayloadRemaining = pPayloadLength - pPayloadOffset;
    const std::size_t aReadLength = std::min(aBlockRemaining, aPayloadRemaining);
    aLogicalBytes += static_cast<std::uint64_t>(aReadLength);
    pPayloadOffset += aReadLength;
  }
  return aLogicalBytes;
}

bool ContainsNonZero(const unsigned char* pBytes, std::size_t pLength) {
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    if (pBytes[aIndex] != 0) {
      return true;
    }
  }
  return false;
}

bool SafetyTryAddU64(std::uint64_t pLeft, std::uint64_t pRight, std::uint64_t& pOut) {
  if (pRight > (std::numeric_limits<std::uint64_t>::max() - pLeft)) {
    return false;
  }
  pOut = pLeft + pRight;
  return true;
}

}  // namespace

LogicalRecordStreamer::LogicalRecordStreamer(const FileSystem& pFileSystem,
                                             const std::vector<SourceFileEntry>& pFiles,
                                             const std::vector<std::string>& pEmptyDirectories)
    : mFileSystem(pFileSystem),
      mFiles(pFiles),
      mEmptyDirectories(pEmptyDirectories) {}

bool LogicalRecordStreamer::Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead) {
  pBytesRead = 0;

  while (pBytesRead < pMaxBytes) {
    if (mPhase == Phase::kPathLength) {
      if (mFileIndex >= mFiles.size()) {
        mFileTerminatorLengthBytes[0] = 0;
        mFileTerminatorLengthBytes[1] = 0;
        mPhase = Phase::kFileTerminatorLengthBytes;
        mPhaseOffset = 0;
      } else {
        const SourceFileEntry& aFile = mFiles[mFileIndex];
        if (aFile.mRelativePath.size() > std::numeric_limits<std::uint16_t>::max() ||
            aFile.mRelativePath.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
          return false;
        }
        mPathLengthBytes[0] = static_cast<unsigned char>(aFile.mRelativePath.size() & 0xFFU);
        mPathLengthBytes[1] = static_cast<unsigned char>((aFile.mRelativePath.size() >> 8) & 0xFFU);
        mPhase = Phase::kPathLengthBytes;
        mPhaseOffset = 0;
      }
      continue;
    }

    if (mPhase == Phase::kPathLengthBytes) {
      while (mPhaseOffset < 2 && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = mPathLengthBytes[mPhaseOffset++];
      }
      if (mPhaseOffset == 2) {
        mPhase = Phase::kPathBytes;
        mPhaseOffset = 0;
      }
      continue;
    }

    if (mPhase == Phase::kPathBytes) {
      const SourceFileEntry& aFile = mFiles[mFileIndex];
      while (mPhaseOffset < aFile.mRelativePath.size() && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = static_cast<unsigned char>(aFile.mRelativePath[mPhaseOffset++]);
      }
      if (mPhaseOffset == aFile.mRelativePath.size()) {
        WriteLeToBuffer(mContentLengthBytes, aFile.mContentLength, 6);
        mPhase = Phase::kContentLengthBytes;
        mPhaseOffset = 0;
      }
      continue;
    }

    if (mPhase == Phase::kContentLengthBytes) {
      while (mPhaseOffset < 6 && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = mContentLengthBytes[mPhaseOffset++];
      }
      if (mPhaseOffset == 6) {
        const SourceFileEntry& aFile = mFiles[mFileIndex];
        mPhase = Phase::kContentBytes;
        mPhaseOffset = 0;
        mContentOffset = 0;
        mReadStream = mFileSystem.OpenReadStream(aFile.mSourcePath);
        if (mReadStream == nullptr || !mReadStream->IsReady() ||
            mReadStream->GetLength() != static_cast<std::size_t>(aFile.mContentLength)) {
          return false;
        }
      }
      continue;
    }

    if (mPhase == Phase::kContentBytes) {
      const SourceFileEntry& aFile = mFiles[mFileIndex];
      const std::size_t aBytesToRead = static_cast<std::size_t>(
          std::min<std::uint64_t>(static_cast<std::uint64_t>(pMaxBytes - pBytesRead),
                                  aFile.mContentLength - mContentOffset));
      if (aBytesToRead > 0 &&
          !mReadStream->Read(static_cast<std::size_t>(mContentOffset), pBuffer + pBytesRead, aBytesToRead)) {
        return false;
      }
      mContentOffset += aBytesToRead;
      pBytesRead += aBytesToRead;
      if (mContentOffset == aFile.mContentLength) {
        mReadStream.reset();
        mPhase = Phase::kPathLength;
        mPhaseOffset = 0;
        mContentOffset = 0;
        ++mFileIndex;
      }
      continue;
    }

    if (mPhase == Phase::kFileTerminatorLengthBytes) {
      while (mPhaseOffset < 2 && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = mFileTerminatorLengthBytes[mPhaseOffset++];
      }
      if (mPhaseOffset == 2) {
        mPhaseOffset = 0;
        mPhase = Phase::kManifestEntryLengthBytes;
      }
      continue;
    }

    if (mPhase == Phase::kManifestEntryLengthBytes) {
      if (mManifestDirectoryIndex >= mEmptyDirectories.size()) {
        if (mManifestTerminatorWritten) {
          return true;
        }
        mManifestEntryLengthBytes[0] = 0;
        mManifestEntryLengthBytes[1] = 0;
        while (mPhaseOffset < 2 && pBytesRead < pMaxBytes) {
          pBuffer[pBytesRead++] = mManifestEntryLengthBytes[mPhaseOffset++];
        }
        if (mPhaseOffset == 2) {
          mManifestTerminatorWritten = true;
          mPhaseOffset = 0;
        }
        continue;
      }
      const std::string& aDirectory = mEmptyDirectories[mManifestDirectoryIndex];
      if (aDirectory.size() > std::numeric_limits<std::uint16_t>::max() ||
          aDirectory.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
        return false;
      }
      mManifestEntryLengthBytes[0] = static_cast<unsigned char>(aDirectory.size() & 0xFFU);
      mManifestEntryLengthBytes[1] = static_cast<unsigned char>((aDirectory.size() >> 8) & 0xFFU);
      while (mPhaseOffset < 2 && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = mManifestEntryLengthBytes[mPhaseOffset++];
      }
      if (mPhaseOffset == 2) {
        mPhaseOffset = 0;
        mPhase = Phase::kManifestEntryBytes;
      }
      continue;
    }

    if (mPhase == Phase::kManifestEntryBytes) {
      const std::string& aDirectory = mEmptyDirectories[mManifestDirectoryIndex];
      while (mPhaseOffset < aDirectory.size() && pBytesRead < pMaxBytes) {
        pBuffer[pBytesRead++] = static_cast<unsigned char>(aDirectory[mPhaseOffset++]);
      }
      if (mPhaseOffset == aDirectory.size()) {
        mPhaseOffset = 0;
        ++mManifestDirectoryIndex;
        mPhase = Phase::kManifestEntryLengthBytes;
      }
      continue;
    }
  }

  return true;
}

void WriterBlock::Attach(unsigned char* pBuffer, std::size_t pPhysicalLength) {
  mBuffer = pBuffer;
  mPhysicalLength = pPhysicalLength;
  mPayloadWriteOffset = 0;
}

void WriterBlock::ResetAndWriteHeader(unsigned long long pRecoveryOffset, bool pUseSpecialFirstHeader) {
  if (mBuffer == nullptr) {
    return;
  }

  std::fill(mBuffer, mBuffer + mPhysicalLength, 0);
  mPayloadWriteOffset = 0;
  if (mPhysicalLength < kRecoveryHeaderLength) {
    return;
  }

  if (pUseSpecialFirstHeader) {
    std::copy_n(kSpecialFirstRecoveryHeader, kRecoveryHeaderLength, mBuffer);
    return;
  }
  WriteLeToBuffer(mBuffer, pRecoveryOffset, kRecoveryHeaderLength);
}

std::size_t WriterBlock::PayloadCapacity() const {
  return LogicalCapacityForPhysicalLength(mPhysicalLength);
}

std::size_t WriterBlock::RemainingPayload() const {
  return PayloadCapacity() - mPayloadWriteOffset;
}

bool WriterBlock::IsFull() const {
  return mPayloadWriteOffset >= PayloadCapacity();
}

std::size_t WriterBlock::WritePayloadBytes(const unsigned char* pBytes, std::size_t pLength) {
  if (mBuffer == nullptr || pBytes == nullptr || pLength == 0) {
    return 0;
  }

  const std::size_t aBytesToWrite = std::min(pLength, RemainingPayload());
  if (aBytesToWrite == 0) {
    return 0;
  }

  const std::size_t aPayloadOffset = kRecoveryHeaderLength + mPayloadWriteOffset;
  std::memcpy(mBuffer + aPayloadOffset, pBytes, aBytesToWrite);
  mPayloadWriteOffset += aBytesToWrite;
  return aBytesToWrite;
}

WriterBlock& WriterPage::Block(std::size_t pIndex) {
  switch (pIndex) {
    case 0:
      return mBlock1;
    case 1:
      return mBlock2;
    case 2:
      return mBlock3;
    default:
      return mBlock4;
  }
}

void WriterPage::Reset(const std::vector<unsigned long long>& pArchiveHeaders,
                       std::size_t pHeaderStartIndex,
                       bool pUseSpecialFirstHeader,
                       std::size_t pPhysicalLength) {
  std::fill(std::begin(mBuffer), std::end(mBuffer), 0);
  mPayloadWriteOffset = 0;
  mPhysicalLength = std::min(pPhysicalLength, kL3Length);
  mBlockIndex = 0;

  for (std::size_t aBlockIndex = 0; aBlockIndex < 4; ++aBlockIndex) {
    const std::size_t aOffset = aBlockIndex * kL1Length;
    const std::size_t aRemainingBytes = mPhysicalLength > aOffset ? mPhysicalLength - aOffset : 0;
    const std::size_t aL1PhysicalLength = std::min<std::size_t>(kL1Length, aRemainingBytes);
    WriterBlock& aBlock = Block(aBlockIndex);
    aBlock.Attach(mBuffer + aOffset, aL1PhysicalLength);
    if (aL1PhysicalLength == 0) {
      continue;
    }

    const std::size_t aGlobalHeaderIndex = pHeaderStartIndex + aBlockIndex;
    const unsigned long long aStride =
        aGlobalHeaderIndex < pArchiveHeaders.size() ? pArchiveHeaders[aGlobalHeaderIndex] : 0ULL;
    aBlock.ResetAndWriteHeader(aStride, pUseSpecialFirstHeader && aBlockIndex == 0);
  }
}

std::size_t WriterPage::PayloadCapacity() const {
  return LogicalCapacityForPhysicalLength(mPhysicalLength);
}

std::size_t WriterPage::WritePayloadBytes(const unsigned char* pBytes, std::size_t pLength) {
  std::size_t aBytesWritten = 0;
  while (aBytesWritten < pLength && mPayloadWriteOffset < PayloadCapacity() && mBlockIndex < 4) {
    WriterBlock& aWriterBlock = Block(mBlockIndex);
    const std::size_t aChunkBytesWritten =
        aWriterBlock.WritePayloadBytes(pBytes + aBytesWritten, pLength - aBytesWritten);
    aBytesWritten += aChunkBytesWritten;
    mPayloadWriteOffset += aChunkBytesWritten;
    if (!aWriterBlock.IsFull()) {
      break;
    }
    ++mBlockIndex;
  }
  return aBytesWritten;
}

CryptWorkspaceL3::CryptWorkspaceL3()
    : mSource(std::make_unique<unsigned char[]>(kL3Length)),
      mWorker(std::make_unique<unsigned char[]>(kL3Length)),
      mDestination(std::make_unique<unsigned char[]>(kL3Length)) {}

unsigned char* CryptWorkspaceL3::Source() {
  return mSource.get();
}

unsigned char* CryptWorkspaceL3::Worker() {
  return mWorker.get();
}

unsigned char* CryptWorkspaceL3::Destination() {
  return mDestination.get();
}

WriterPageOwner::WriterPageOwner()
    : mWriter(std::make_unique<WriterPage>()) {}

WriterPage& WriterPageOwner::Writer() {
  return *mWriter;
}

const WriterPage& WriterPageOwner::Writer() const {
  return *mWriter;
}

PageReader::PageReader(const FileSystem& pFileSystem,
                       const Crypt& pCrypt,
                       const std::vector<ArchiveHeaderRecord>& pArchives,
                       std::size_t pStartArchiveIndex,
                       std::size_t pArchiveCount,
                       std::size_t pStartPhysicalOffset,
                       bool pUseEncryption)
    : mFileSystem(pFileSystem),
      mCrypt(pCrypt),
      mArchives(pArchives),
      mStartArchiveIndex(pStartArchiveIndex),
      mArchiveCount(pArchiveCount),
      mStartPhysicalOffset(pStartPhysicalOffset),
      mPhysicalOffset(pStartPhysicalOffset),
      mUseEncryption(pUseEncryption),
      mPageBuffer(kL3Length) {
  // mPageBuffer is declared after mCurrentPageData, so assign the pointer here.
  mCurrentPageData = mPageBuffer.Data();
  mArchivePayloadLengths.reserve(mArchiveCount);
  for (std::size_t aIndex = 0; aIndex < mArchiveCount; ++aIndex) {
    const ArchiveHeaderRecord& aArchive = mArchives[mStartArchiveIndex + aIndex];
    mArchivePayloadLengths.push_back(aArchive.mPayloadLength);
  }
}

void PageReader::SetFailure(UnpackIntegerFailure pCode, const std::string& pMessage) {
  if (mFailure.mCode == UnpackIntegerFailure::kNone) {
    mFailure.mCode = pCode;
    mFailure.mMessage = pMessage;
  }
}

bool PageReader::OpenArchive() {
  if (mArchiveRelativeIndex >= mArchiveCount) {
    return false;
  }

  const ArchiveHeaderRecord& aArchive = mArchives[mStartArchiveIndex + mArchiveRelativeIndex];
  mReadStream = mFileSystem.OpenReadStream(aArchive.mPath);
  if (mReadStream == nullptr || !mReadStream->IsReady() || mReadStream->GetLength() < kArchiveHeaderLength) {
    SetFailure(UnpackIntegerFailure::kUnknown, "could not open archive stream for " + aArchive.mName);
    return false;
  }

  const std::size_t aPayloadLength = mReadStream->GetLength() - kArchiveHeaderLength;
  if (aPayloadLength != mArchivePayloadLengths[mArchiveRelativeIndex]) {
    SetFailure(UnpackIntegerFailure::kUnknown, "archive length changed while opening archive stream for " + aArchive.mName);
    return false;
  }
  mPayloadLength = aPayloadLength;
  mCurrentPageStart = 0;
  mCurrentPageLength = 0;
  mPageLoaded = false;
  return true;
}

bool PageReader::AdvanceArchive() {
  ++mArchiveRelativeIndex;
  mReadStream.reset();
  mPayloadLength = 0;
  mCurrentPageStart = 0;
  mCurrentPageLength = 0;
  mPageLoaded = false;
  mPhysicalOffset = 0;

  if (mArchiveRelativeIndex >= mArchiveCount) {
    return false;
  }
  return OpenArchive();
}

bool PageReader::LoadPage() {
  if (mReadStream == nullptr && !OpenArchive()) {
    return false;
  }
  if (mPhysicalOffset >= mPayloadLength) {
    return AdvanceArchive() && LoadPage();
  }

  const std::size_t aPageStart = (mPhysicalOffset / kL3Length) * kL3Length;
  if (mPageLoaded && mCurrentPageStart == aPageStart) {
    return true;
  }

  const std::size_t aPageLength = std::min<std::size_t>(kL3Length, mPayloadLength - aPageStart);
  unsigned char* aReadBuffer = mUseEncryption ? mWorkspace.Source() : mPageBuffer.Data();
  if (!mReadStream->Read(kArchiveHeaderLength + aPageStart, aReadBuffer, aPageLength)) {
    SetFailure(UnpackIntegerFailure::kUnknown, "could not read archive page bytes.");
    return false;
  }
  mCurrentPageData = aReadBuffer;

  if (mUseEncryption) {
    std::memset(mWorkspace.Worker(), 0, kL3Length);
    std::memset(mWorkspace.Destination(), 0, kL3Length);
    std::string aCryptError;
    if (!mCrypt.UnsealData(aReadBuffer,
                           mWorkspace.Worker(),
                           mWorkspace.Destination(),
                           aPageLength,
                           &aCryptError,
                           CryptMode::kNormal)) {
      SetFailure(UnpackIntegerFailure::kUnknown,
                 aCryptError.empty() ? "could not unseal archive page bytes." : aCryptError);
      return false;
    }
    mCurrentPageData = mWorkspace.Destination();
  }

  mCurrentPageStart = aPageStart;
  mCurrentPageLength = aPageLength;
  mPageLoaded = true;
  return true;
}

bool PageReader::Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead) {
  pBytesRead = 0;
  if (pBuffer == nullptr || pMaxBytes == 0) {
    return true;
  }

  while (pBytesRead < pMaxBytes) {
    if (mReadStream == nullptr && !OpenArchive()) {
      return true;
    }
    if (mPhysicalOffset >= mPayloadLength) {
      if (!AdvanceArchive()) {
        return true;
      }
      continue;
    }
    if (!LoadPage()) {
      return false;
    }

    const std::size_t aOffsetInBlock = mPhysicalOffset % kL1Length;
    if (aOffsetInBlock < kRecoveryHeaderLength) {
      mPhysicalOffset += kRecoveryHeaderLength - aOffsetInBlock;
      continue;
    }
    if (mPhysicalOffset >= mPayloadLength) {
      continue;
    }

    const std::size_t aOffsetInPage = mPhysicalOffset - mCurrentPageStart;
    const std::size_t aPayloadBytesRemainingInBlock = kL1Length - aOffsetInBlock;
    const std::size_t aPayloadBytesRemainingInPage = mCurrentPageLength - aOffsetInPage;
    const std::size_t aPayloadBytesRemainingInArchive = mPayloadLength - mPhysicalOffset;
    const std::size_t aChunkLength =
        std::min({pMaxBytes - pBytesRead,
                  aPayloadBytesRemainingInBlock,
                  aPayloadBytesRemainingInPage,
                  aPayloadBytesRemainingInArchive});
    if (aChunkLength == 0) {
      SetFailure(UnpackIntegerFailure::kUnknown, "logical page reader stopped making forward progress.");
      return false;
    }

    std::memcpy(pBuffer + pBytesRead, mCurrentPageData + aOffsetInPage, aChunkLength);
    pBytesRead += aChunkLength;
    mPhysicalOffset += aChunkLength;
  }

  return true;
}

std::uint64_t PageReader::RemainingLogicalBytes() const {
  std::uint64_t aRemaining = 0;
  if (mArchiveRelativeIndex < mArchivePayloadLengths.size()) {
    aRemaining += CountLogicalBytesInPayload(mArchivePayloadLengths[mArchiveRelativeIndex], mPhysicalOffset);
  }
  for (std::size_t aIndex = mArchiveRelativeIndex + 1; aIndex < mArchivePayloadLengths.size(); ++aIndex) {
    aRemaining += CountLogicalBytesInPayload(mArchivePayloadLengths[aIndex], 0);
  }
  return aRemaining;
}

const UnpackFailureInfo& PageReader::Failure() const {
  return mFailure;
}

const std::vector<std::size_t>& PageReader::ArchivePayloadLengths() const {
  return mArchivePayloadLengths;
}

std::size_t PageReader::StartPhysicalOffset() const {
  return mStartPhysicalOffset;
}

RecordParser::RecordParser(FileSystem& pFileSystem, const std::string& pDestinationDirectory, bool pWriteFiles)
    : mFileSystem(pFileSystem),
      mDestinationDirectory(pDestinationDirectory),
      mWriteFiles(pWriteFiles),
      mReadBuffer(kL3Length) {}

bool RecordParser::Parse(PageReader& pReader, const std::function<void(std::uint64_t, std::size_t)>& pOnProgress) {
  InitializeCursor(pReader);
  mTrailingNonZeroInCurrentChunk = false;
  std::size_t aFenceParseReadIterations = 0;
  while (mPhase != Phase::kFinished) {
    ++aFenceParseReadIterations;
    if (aFenceParseReadIterations > kFenceMaxParseReadIterations) {
      SetFailure(UnpackIntegerFailure::kUnknown, "Fence: parse iteration guard triggered.");
      return false;
    }
    std::size_t aBytesRead = 0;
    if (!pReader.Read(mReadBuffer.Data(), mReadBuffer.Size(), aBytesRead)) {
      if (mFailure.mCode == UnpackIntegerFailure::kNone) {
        mFailure = pReader.Failure();
      }
      if (mFailure.mCode == UnpackIntegerFailure::kNone) {
        SetFailure(UnpackIntegerFailure::kUnknown, "archive payloads could not be decoded.");
      }
      return false;
    }
    if (aBytesRead == 0) {
      break;
    }
    mRemainingBytesAfterCurrentChunk = pReader.RemainingLogicalBytes();
    const std::size_t aPreviousFilesProcessed = mFilesProcessed;
    if (!Consume(mReadBuffer.Data(), aBytesRead)) {
      return false;
    }
    if (pOnProgress && mFilesProcessed != aPreviousFilesProcessed) {
      pOnProgress(mProcessedBytes, mFilesProcessed);
    }
  }

  if (mPhase == Phase::kFinished) {
    bool aHasLaterArchiveBytes = false;
    if (mCursorArchiveIndex < mArchivePayloadLengths.size()) {
      for (std::size_t aIndex = mCursorArchiveIndex + 1; aIndex < mArchivePayloadLengths.size(); ++aIndex) {
        if (CountLogicalBytesInPayload(mArchivePayloadLengths[aIndex], 0) > 0) {
          aHasLaterArchiveBytes = true;
          break;
        }
      }
    }
    bool aHasDanglingNonZeroBytes = mTrailingNonZeroInCurrentChunk;
    while (true) {
      std::size_t aBytesRead = 0;
      if (!pReader.Read(mReadBuffer.Data(), mReadBuffer.Size(), aBytesRead)) {
        if (mFailure.mCode == UnpackIntegerFailure::kNone) {
          mFailure = pReader.Failure();
        }
        if (mFailure.mCode == UnpackIntegerFailure::kNone) {
          SetFailure(UnpackIntegerFailure::kUnknown, "archive payloads could not be decoded.");
        }
        return false;
      }
      if (aBytesRead == 0) {
        break;
      }
      if (ContainsNonZero(mReadBuffer.Data(), aBytesRead)) {
        aHasDanglingNonZeroBytes = true;
      }
    }
    if (aHasLaterArchiveBytes) {
      SetFailure(UnpackIntegerFailure::kDanglingArchives, "Unexpected end of file found.");
      return false;
    }
    if (aHasDanglingNonZeroBytes) {
      SetFailure(UnpackIntegerFailure::kDanglingBytes, "Unexpected end of file found.");
      return false;
    }
  }

  if (mWriteStream != nullptr && !CloseOutputFile()) {
    return false;
  }
  if (mPhase != Phase::kFinished &&
      mPhase != Phase::kPathLengthBytes &&
      (mPhase != Phase::kManifestFolderLengthBytes || mPhaseOffset != 0)) {
    if (mFailure.mCode == UnpackIntegerFailure::kNone) {
      SetFailure(UnpackIntegerFailure::kUnknown, "archive payload ended in the middle of a file record.");
    }
    return false;
  }
  return mFilesProcessed > 0 || mPhase == Phase::kFinished || mPhase == Phase::kManifestFolderLengthBytes;
}

std::uint64_t RecordParser::ProcessedBytes() const {
  return mProcessedBytes;
}

std::size_t RecordParser::FilesProcessed() const {
  return mFilesProcessed;
}

std::size_t RecordParser::EmptyDirectoriesProcessed() const {
  return mEmptyDirectoriesProcessed;
}

const UnpackFailureInfo& RecordParser::Failure() const {
  return mFailure;
}

void RecordParser::SetFailure(UnpackIntegerFailure pCode, const std::string& pMessage) {
  if (mFailure.mCode == UnpackIntegerFailure::kNone) {
    mFailure.mCode = pCode;
    mFailure.mMessage = pMessage;
  }
}

void RecordParser::InitializeCursor(const PageReader& pReader) {
  if (mCursorInitialized) {
    return;
  }
  mArchivePayloadLengths = pReader.ArchivePayloadLengths();
  mCursorArchiveIndex = 0;
  mCursorPhysicalOffset = pReader.StartPhysicalOffset();
  NormalizeCursor();
  mCursorInitialized = true;
}

void RecordParser::NormalizeCursor() {
  while (mCursorArchiveIndex < mArchivePayloadLengths.size()) {
    const std::size_t aPayloadLength = mArchivePayloadLengths[mCursorArchiveIndex];
    if (mCursorPhysicalOffset >= aPayloadLength) {
      ++mCursorArchiveIndex;
      mCursorPhysicalOffset = 0;
      continue;
    }
    const std::size_t aOffsetInBlock = mCursorPhysicalOffset % kL1Length;
    if (aOffsetInBlock < kRecoveryHeaderLength) {
      mCursorPhysicalOffset += kRecoveryHeaderLength - aOffsetInBlock;
      continue;
    }
    break;
  }
}

bool RecordParser::AdvanceCursor(std::size_t pLogicalBytes) {
  std::size_t aRemaining = pLogicalBytes;
  while (aRemaining > 0) {
    NormalizeCursor();
    if (mCursorArchiveIndex >= mArchivePayloadLengths.size()) {
      SetFailure(UnpackIntegerFailure::kUnknown, "logical record parser cursor advanced past archive payload.");
      return false;
    }

    const std::size_t aPayloadLength = mArchivePayloadLengths[mCursorArchiveIndex];
    const std::size_t aOffsetInBlock = mCursorPhysicalOffset % kL1Length;
    const std::size_t aBytesToRecoveryBoundary = kL1Length - aOffsetInBlock;
    const std::size_t aBytesToArchiveBoundary = aPayloadLength - mCursorPhysicalOffset;
    const std::size_t aStep = std::min<std::size_t>(aRemaining, std::min(aBytesToRecoveryBoundary, aBytesToArchiveBoundary));
    if (aStep == 0) {
      SetFailure(UnpackIntegerFailure::kUnknown, "logical record parser cursor stopped making forward progress.");
      return false;
    }
    mCursorPhysicalOffset += aStep;
    aRemaining -= aStep;
  }
  NormalizeCursor();
  return true;
}

UnpackIntegerFailure RecordParser::ClassifyBoundaryForNextSpan(std::uint64_t pLogicalBytes,
                                                               UnpackIntegerFailure pRecoveryCode,
                                                               UnpackIntegerFailure pArchiveCode) const {
  (void)pLogicalBytes;
  (void)pRecoveryCode;
  (void)pArchiveCode;
  return UnpackIntegerFailure::kNone;
}

bool RecordParser::Consume(const unsigned char* pBytes, std::size_t pLength) {
  std::size_t aOffset = 0;
  std::size_t aFenceStateTransitions = 0;
  const std::size_t aFenceMaxStateTransitions =
      std::max(kFenceMinStateTransitionsPerChunk, (pLength + 1) * kFenceMaxStateTransitionsPerChunkMultiplier);
  while (aOffset < pLength && mPhase != Phase::kFinished) {
    ++aFenceStateTransitions;
    if (aFenceStateTransitions > aFenceMaxStateTransitions) {
      SetFailure(UnpackIntegerFailure::kUnknown, "Fence: decode state transition guard triggered.");
      return false;
    }
    if (mPhase == Phase::kPathLengthBytes) {
      while (mPhaseOffset < 2 && aOffset < pLength) {
        mPathLengthBytes[mPhaseOffset++] = pBytes[aOffset++];
        if (!AdvanceCursor(1)) {
          return false;
        }
      }
      if (mPhaseOffset == 2) {
        mPathLength = static_cast<std::size_t>(ReadLeFromBuffer(mPathLengthBytes, 2));
        mPhaseOffset = 0;
        if (mPathLength == 0) {
          mPhase = Phase::kManifestFolderLengthBytes;
          mPhaseOffset = 0;
          continue;
        }
        if (mPathLength > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
          SetFailure(UnpackIntegerFailure::kFileNameLengthGreaterThanMaxValidFilePathLength,
                     "Safety: decoded file name length exceeded MAX_VALID_FILE_PATH_LENGTH.");
          return false;
        }
        std::uint64_t aRemainingBytesInUnpackJob = 0;
        if (!SafetyTryAddU64(static_cast<std::uint64_t>(pLength - aOffset),
                             mRemainingBytesAfterCurrentChunk,
                             aRemainingBytesInUnpackJob)) {
          SetFailure(UnpackIntegerFailure::kUnknown, "Safety: remaining-bytes calculation overflowed.");
          return false;
        }
        if (static_cast<std::uint64_t>(mPathLength) > aRemainingBytesInUnpackJob) {
          SetFailure(UnpackIntegerFailure::kFileNameLengthGreaterThanRemainingBytesInUnpackJob,
                     "Safety: decoded file name length exceeded remaining bytes in unpack job.");
          return false;
        }
        const UnpackIntegerFailure aBoundaryFailure =
            ClassifyBoundaryForNextSpan(mPathLength,
                                        UnpackIntegerFailure::kFileNameLengthLandsInsideRecoveryHeader,
                                        UnpackIntegerFailure::kFileNameLengthLandsInsideArchiveHeader);
        if (aBoundaryFailure != UnpackIntegerFailure::kNone) {
          SetFailure(aBoundaryFailure, aBoundaryFailure == UnpackIntegerFailure::kFileNameLengthLandsInsideRecoveryHeader
                                         ? "decoded file name length landed inside a recovery header."
                                         : "decoded file name length landed inside an archive header.");
          return false;
        }
        mPhase = Phase::kPathBytes;
      }
      continue;
    }

    if (mPhase == Phase::kPathBytes) {
      const std::size_t aChunkLength = std::min(mPathLength - mPhaseOffset, pLength - aOffset);
      std::memcpy(mPathBytes + mPhaseOffset, pBytes + aOffset, aChunkLength);
      mPhaseOffset += aChunkLength;
      aOffset += aChunkLength;
      if (!AdvanceCursor(aChunkLength)) {
        return false;
      }
      if (mPhaseOffset == mPathLength) {
        mCurrentRelativePath.assign(reinterpret_cast<const char*>(mPathBytes), mPathLength);
        mPhaseOffset = 0;
        mPhase = Phase::kContentLengthBytes;
      }
      continue;
    }

    if (mPhase == Phase::kContentLengthBytes) {
      while (mPhaseOffset < 6 && aOffset < pLength) {
        mContentLengthBytes[mPhaseOffset++] = pBytes[aOffset++];
        if (!AdvanceCursor(1)) {
          return false;
        }
      }
      if (mPhaseOffset == 6) {
        mContentLength = ReadLeFromBuffer(mContentLengthBytes, 6);
        mContentOffset = 0;
        mPhaseOffset = 0;
        std::uint64_t aRemainingBytesInUnpackJob = 0;
        if (!SafetyTryAddU64(static_cast<std::uint64_t>(pLength - aOffset),
                             mRemainingBytesAfterCurrentChunk,
                             aRemainingBytesInUnpackJob)) {
          SetFailure(UnpackIntegerFailure::kUnknown, "Safety: remaining-bytes calculation overflowed.");
          return false;
        }
        if (mContentLength > kSafetyMaxDecodedFileContentBytes) {
          SetFailure(UnpackIntegerFailure::kUnknown, "Safety: decoded file data length exceeded safe maximum.");
          return false;
        }
        if (mContentLength > aRemainingBytesInUnpackJob) {
          SetFailure(UnpackIntegerFailure::kFileDataLengthGreaterThanRemainingBytesInUnpackJob,
                     "Safety: decoded file data length exceeded remaining bytes in unpack job.");
          return false;
        }
        const UnpackIntegerFailure aBoundaryFailure =
            ClassifyBoundaryForNextSpan(mContentLength,
                                        UnpackIntegerFailure::kFileDataLengthLandsInsideRecoveryHeader,
                                        UnpackIntegerFailure::kFileDataLengthLandsInsideArchiveHeader);
        if (aBoundaryFailure != UnpackIntegerFailure::kNone) {
          SetFailure(aBoundaryFailure, aBoundaryFailure == UnpackIntegerFailure::kFileDataLengthLandsInsideRecoveryHeader
                                         ? "decoded file data length landed inside a recovery header."
                                         : "decoded file data length landed inside an archive header.");
          return false;
        }
        if (!OpenOutputFile()) {
          return false;
        }
        mPhase = Phase::kContentBytes;
      }
      continue;
    }

    if (mPhase == Phase::kContentBytes) {
      const std::size_t aChunkLength = static_cast<std::size_t>(
          std::min<std::uint64_t>(mContentLength - mContentOffset, pLength - aOffset));
      if (aChunkLength == 0) {
        if (!CloseOutputFile()) {
          return false;
        }
        ++mFilesProcessed;
        mPhase = Phase::kPathLengthBytes;
        continue;
      }

      if (mWriteStream != nullptr && !mWriteStream->Write(pBytes + aOffset, aChunkLength)) {
        SetFailure(UnpackIntegerFailure::kUnknown, "could not write " + mCurrentRelativePath);
        return false;
      }

      mContentOffset += aChunkLength;
      mProcessedBytes += aChunkLength;
      aOffset += aChunkLength;
      if (!AdvanceCursor(aChunkLength)) {
        return false;
      }
      if (mContentOffset == mContentLength) {
        if (!CloseOutputFile()) {
          return false;
        }
        ++mFilesProcessed;
        mPhase = Phase::kPathLengthBytes;
      }
    }

    if (mPhase == Phase::kManifestFolderLengthBytes) {
      while (mPhaseOffset < 2 && aOffset < pLength) {
        mManifestFolderLengthBytes[mPhaseOffset++] = pBytes[aOffset++];
        if (!AdvanceCursor(1)) {
          return false;
        }
      }
      if (mPhaseOffset == 2) {
        mPathLength = static_cast<std::size_t>(ReadLeFromBuffer(mManifestFolderLengthBytes, 2));
        mPhaseOffset = 0;
        if (mPathLength == 0) {
          mTrailingNonZeroInCurrentChunk = ContainsNonZero(pBytes + aOffset, pLength - aOffset);
          if (aOffset + 3 <= pLength) {
            const std::size_t aCandidateLength = static_cast<std::size_t>(ReadLeFromBuffer(pBytes + aOffset, 2));
            const std::size_t aCandidateRemaining = pLength - (aOffset + 2);
            if (aCandidateLength > 0 &&
                aCandidateLength <= peanutbutter::MAX_VALID_FILE_PATH_LENGTH &&
                aCandidateLength <= aCandidateRemaining) {
              SetFailure(UnpackIntegerFailure::kManifestFolderLengthIsZero, "manifest folder length is zero.");
              return false;
            }
          }
          mPhase = Phase::kFinished;
          continue;
        }
        if (mPathLength > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
          SetFailure(UnpackIntegerFailure::kManifestFolderLengthGreaterThanMaxValidFilePathLength,
                     "Safety: manifest folder length exceeded MAX_VALID_FILE_PATH_LENGTH.");
          return false;
        }
        std::uint64_t aRemainingBytesInUnpackJob = 0;
        if (!SafetyTryAddU64(static_cast<std::uint64_t>(pLength - aOffset),
                             mRemainingBytesAfterCurrentChunk,
                             aRemainingBytesInUnpackJob)) {
          SetFailure(UnpackIntegerFailure::kUnknown, "Safety: remaining-bytes calculation overflowed.");
          return false;
        }
        if (static_cast<std::uint64_t>(mPathLength) > aRemainingBytesInUnpackJob) {
          SetFailure(UnpackIntegerFailure::kManifestFolderLengthGreaterThanRemainingBytesInUnpackJob,
                     "Safety: manifest folder length exceeded remaining bytes in unpack job.");
          return false;
        }
        mPhase = Phase::kManifestFolderBytes;
      }
      continue;
    }

    if (mPhase == Phase::kManifestFolderBytes) {
      const std::size_t aChunkLength = std::min(mPathLength - mPhaseOffset, pLength - aOffset);
      std::memcpy(mPathBytes + mPhaseOffset, pBytes + aOffset, aChunkLength);
      mPhaseOffset += aChunkLength;
      aOffset += aChunkLength;
      if (!AdvanceCursor(aChunkLength)) {
        return false;
      }
      if (mPhaseOffset == mPathLength) {
        mCurrentRelativePath.assign(reinterpret_cast<const char*>(mPathBytes), mPathLength);
        if (mCurrentRelativePath.empty()) {
          SetFailure(UnpackIntegerFailure::kManifestFolderLengthIsZero, "decoded manifest folder path is empty.");
          return false;
        }
        if (mWriteFiles) {
          const std::string aDestinationPath = mFileSystem.JoinPath(mDestinationDirectory, mCurrentRelativePath);
          if (!mFileSystem.EnsureDirectory(aDestinationPath)) {
            SetFailure(UnpackIntegerFailure::kUnknown, "could not create directory for " + mCurrentRelativePath);
            return false;
          }
          ++mEmptyDirectoriesProcessed;
        }
        mPhaseOffset = 0;
        mPhase = Phase::kManifestFolderLengthBytes;
      }
      continue;
    }

  }
  return true;
}

bool RecordParser::OpenOutputFile() {
  if (!mWriteFiles) {
    return true;
  }

  const std::string aDestinationPath = mFileSystem.JoinPath(mDestinationDirectory, mCurrentRelativePath);
  const std::string aParentPath = mFileSystem.ParentPath(aDestinationPath);
  if (!aParentPath.empty() && !mFileSystem.EnsureDirectory(aParentPath)) {
    SetFailure(UnpackIntegerFailure::kUnknown, "could not create directory for " + mCurrentRelativePath);
    return false;
  }

  mWriteStream = mFileSystem.OpenWriteStream(aDestinationPath);
  if (mWriteStream == nullptr || !mWriteStream->IsReady()) {
    SetFailure(UnpackIntegerFailure::kUnknown, "could not write " + mCurrentRelativePath);
    return false;
  }
  return true;
}

bool RecordParser::CloseOutputFile() {
  if (mWriteStream == nullptr) {
    return true;
  }
  if (!mWriteStream->Close()) {
    SetFailure(UnpackIntegerFailure::kUnknown, "could not finalize " + mCurrentRelativePath);
    return false;
  }
  mWriteStream.reset();
  return true;
}

}  // namespace peanutbutter
