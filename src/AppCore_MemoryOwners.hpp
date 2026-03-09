#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_MEMORY_OWNERS_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_MEMORY_OWNERS_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "Memory/HeapBuffer.hpp"

namespace peanutbutter::ultima {

struct SourceFileEntry {
  std::string mSourcePath;
  std::string mRelativePath;
  std::uint64_t mContentLength = 0;
};

// Streams one logical file record at a time into caller-owned fixed buffers.
// This keeps pack-side reads bounded even when source files are very large.
class LogicalRecordStreamer {
 public:
  LogicalRecordStreamer(const FileSystem& pFileSystem, const std::vector<SourceFileEntry>& pFiles);

  bool Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead);

 private:
  enum class Phase {
    kPathLength,
    kPathLengthBytes,
    kPathBytes,
    kContentLengthBytes,
    kContentBytes,
  };

  const FileSystem& mFileSystem;
  const std::vector<SourceFileEntry>& mFiles;
  std::size_t mFileIndex = 0;
  Phase mPhase = Phase::kPathLength;
  std::size_t mPhaseOffset = 0;
  std::uint64_t mContentOffset = 0;
  unsigned char mPathLengthBytes[2] = {};
  unsigned char mContentLengthBytes[6] = {};
  std::unique_ptr<FileReadStream> mReadStream;
};

// Owns one block window over storage that belongs to WriterPage.
// A block contains its recovery header plus one contiguous payload span.
class WriterBlock {
 public:
  void Attach(unsigned char* pBuffer, std::size_t pPhysicalLength);
  void ResetAndWriteHeader(unsigned long long pRecoveryOffset, bool pUseSpecialFirstHeader);

  std::size_t PayloadCapacity() const;
  std::size_t RemainingPayload() const;
  bool IsFull() const;
  std::size_t WritePayloadBytes(const unsigned char* pBytes, std::size_t pLength);

 private:
  unsigned char* mBuffer = nullptr;
  std::size_t mPhysicalLength = 0;
  std::size_t mPayloadWriteOffset = 0;
};

// Owns one page of archive payload storage.
// Archive headers live outside the page; a page is always 4 blocks.
class WriterPage {
 public:
  void Reset(const std::vector<unsigned long long>& pArchiveHeaders,
             std::size_t pHeaderStartIndex,
             bool pUseSpecialFirstHeader,
             std::size_t pPhysicalLength);

  std::size_t PayloadCapacity() const;
  std::size_t WritePayloadBytes(const unsigned char* pBytes, std::size_t pLength);

  WriterBlock& Block(std::size_t pIndex);

  unsigned char mBuffer[peanutbutter::SB_L3_LENGTH] = {};
  WriterBlock mBlock1;
  WriterBlock mBlock2;
  WriterBlock mBlock3;
  WriterBlock mBlock4;
  std::size_t mPayloadWriteOffset = 0;
  std::size_t mPhysicalLength = peanutbutter::SB_L3_LENGTH;
  std::size_t mBlockIndex = 0;
};

// Owns the L3-sized crypt scratch buffers so they do not live on the stack.
// Production block sizes are too large for repeated local arrays here.
class CryptWorkspaceL3 {
 public:
  CryptWorkspaceL3();

  unsigned char* Source();
  unsigned char* Worker();
  unsigned char* Destination();

 private:
  std::unique_ptr<unsigned char[]> mSource;
  std::unique_ptr<unsigned char[]> mWorker;
  std::unique_ptr<unsigned char[]> mDestination;
};

// Owns the page writer on the heap so pack flow does not reserve a large
// fixed page in the RunBundle stack frame.
class WriterPageOwner {
 public:
  WriterPageOwner();

  WriterPage& Writer();
  const WriterPage& Writer() const;

 private:
  std::unique_ptr<WriterPage> mWriter;
};

// Streams logical payload bytes across one archive set page-by-page.
// Archive headers stay outside the reader; only page payload bytes are exposed.
class PageReader {
 public:
  PageReader(const FileSystem& pFileSystem,
             const Crypt& pCrypt,
             const std::vector<ArchiveHeaderRecord>& pArchives,
             std::size_t pStartArchiveIndex,
             std::size_t pArchiveCount,
             std::size_t pStartPhysicalOffset,
             bool pUseEncryption);

  bool Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead);
  std::uint64_t RemainingLogicalBytes() const;
  const UnpackFailureInfo& Failure() const;
  const std::vector<std::size_t>& ArchivePayloadLengths() const;
  std::size_t StartPhysicalOffset() const;

 private:
  bool OpenArchive();
  bool LoadPage();
  bool AdvanceArchive();
  void SetFailure(UnpackIntegerFailure pCode, const std::string& pMessage);

  const FileSystem& mFileSystem;
  const Crypt& mCrypt;
  const std::vector<ArchiveHeaderRecord>& mArchives;
  std::vector<std::size_t> mArchivePayloadLengths;
  std::size_t mStartArchiveIndex = 0;
  std::size_t mArchiveCount = 0;
  std::size_t mStartPhysicalOffset = 0;
  std::size_t mArchiveRelativeIndex = 0;
  std::size_t mPayloadLength = 0;
  std::size_t mPhysicalOffset = 0;
  std::size_t mCurrentPageStart = 0;
  std::size_t mCurrentPageLength = 0;
  bool mPageLoaded = false;
  bool mUseEncryption = false;
  std::unique_ptr<FileReadStream> mReadStream;
  CryptWorkspaceL3 mWorkspace;
  HeapBuffer mPageBuffer;
  UnpackFailureInfo mFailure;
};

// Parses logical record bytes and optionally writes files incrementally.
// This keeps unbundle/recover bounded without materializing decoded file vectors.
class RecordParser {
 public:
  RecordParser(FileSystem& pFileSystem, const std::string& pDestinationDirectory, bool pWriteFiles);

  bool Parse(PageReader& pReader);
  std::uint64_t ProcessedBytes() const;
  std::size_t FilesProcessed() const;
  const UnpackFailureInfo& Failure() const;

 private:
  enum class Phase {
    kPathLengthBytes,
    kPathBytes,
    kContentLengthBytes,
    kContentBytes,
    kFinished,
  };

  bool Consume(const unsigned char* pBytes, std::size_t pLength);
  bool OpenOutputFile();
  bool CloseOutputFile();
  void SetFailure(UnpackIntegerFailure pCode, const std::string& pMessage);
  void InitializeCursor(const PageReader& pReader);
  void NormalizeCursor();
  bool AdvanceCursor(std::size_t pLogicalBytes);
  UnpackIntegerFailure ClassifyBoundaryForNextSpan(std::uint64_t pLogicalBytes,
                                                   UnpackIntegerFailure pRecoveryCode,
                                                   UnpackIntegerFailure pArchiveCode) const;

  FileSystem& mFileSystem;
  std::string mDestinationDirectory;
  bool mWriteFiles = false;
  Phase mPhase = Phase::kPathLengthBytes;
  std::size_t mPhaseOffset = 0;
  std::size_t mPathLength = 0;
  std::uint64_t mContentLength = 0;
  std::uint64_t mContentOffset = 0;
  std::uint64_t mProcessedBytes = 0;
  std::size_t mFilesProcessed = 0;
  std::uint64_t mRemainingBytesAfterCurrentChunk = 0;
  UnpackFailureInfo mFailure;
  std::string mCurrentRelativePath;
  std::vector<std::size_t> mArchivePayloadLengths;
  std::size_t mCursorArchiveIndex = 0;
  std::size_t mCursorPhysicalOffset = 0;
  bool mCursorInitialized = false;
  unsigned char mPathLengthBytes[2] = {};
  unsigned char mContentLengthBytes[6] = {};
  unsigned char mPathBytes[peanutbutter::MAX_VALID_FILE_PATH_LENGTH] = {};
  HeapBuffer mReadBuffer;
  std::unique_ptr<FileWriteStream> mWriteStream;
};

}  // namespace peanutbutter::ultima

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_MEMORY_OWNERS_HPP_
