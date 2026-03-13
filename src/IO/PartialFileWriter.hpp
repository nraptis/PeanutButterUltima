#ifndef PEANUT_BUTTER_ULTIMA_IO_PARTIAL_FILE_WRITER_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_PARTIAL_FILE_WRITER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "IO/FileSystem.hpp"

namespace peanutbutter {

// Writes a logical file directly from slices of an existing decoded block buffer.
// This avoids staging/copying file payload bytes into another heap container.
class PartialFileWriter final {
 public:
  explicit PartialFileWriter(FileSystem& pFileSystem)
      : mFileSystem(pFileSystem) {}

  ~PartialFileWriter() {
    AbortFile();
  }

  bool HasActiveFile() const {
    return mHasActiveFile;
  }

  std::uint64_t BytesExpected() const {
    return mBytesExpected;
  }

  std::uint64_t BytesWritten() const {
    return mBytesWritten;
  }

  std::uint64_t RemainingBytes() const {
    return mBytesExpected >= mBytesWritten ? (mBytesExpected - mBytesWritten) : 0;
  }

  bool BeginFile(const std::string& pOutputPath,
                 std::uint64_t pExpectedBytes,
                 std::string* pErrorMessage) {
    if (mHasActiveFile) {
      return Fail("partial writer begin failed: file already active.", pErrorMessage);
    }
    mWriteStream = mFileSystem.OpenWriteStream(pOutputPath);
    if (mWriteStream == nullptr || !mWriteStream->IsReady()) {
      mWriteStream.reset();
      return Fail("partial writer begin failed: unable to open write stream.", pErrorMessage);
    }
    mHasActiveFile = true;
    mBytesExpected = pExpectedBytes;
    mBytesWritten = 0;
    return true;
  }

  // Writes from [pPayloadIndex, pPayloadLimit) in pDecodedBlock and advances pPayloadIndex.
  // Caller can pass pPayloadIndex = RECOVERY_HEADER_LENGTH to skip block headers.
  bool WriteFromDecodedBlock(const unsigned char* pDecodedBlock,
                             std::size_t pBlockLength,
                             std::size_t& pPayloadIndex,
                             std::size_t pPayloadLimit,
                             std::string* pErrorMessage) {
    if (!mHasActiveFile || mWriteStream == nullptr) {
      return Fail("partial writer write failed: no active file.", pErrorMessage);
    }
    if (pDecodedBlock == nullptr) {
      return Fail("partial writer write failed: null decoded block.", pErrorMessage);
    }
    if (pPayloadIndex > pPayloadLimit || pPayloadLimit > pBlockLength) {
      return Fail("partial writer write failed: invalid payload bounds.", pErrorMessage);
    }

    const std::uint64_t aRemaining = RemainingBytes();
    const std::size_t aAvailable = pPayloadLimit - pPayloadIndex;
    const std::size_t aWriteLength =
        static_cast<std::size_t>(std::min<std::uint64_t>(aRemaining, static_cast<std::uint64_t>(aAvailable)));
    if (aWriteLength == 0) {
      return true;
    }

    const unsigned char* aWriteBegin = pDecodedBlock + pPayloadIndex;
    if (!mWriteStream->Write(aWriteBegin, aWriteLength)) {
      return Fail("partial writer write failed: stream write failed.", pErrorMessage);
    }
    pPayloadIndex += aWriteLength;
    mBytesWritten += static_cast<std::uint64_t>(aWriteLength);
    return true;
  }

  bool FinishFile(std::string* pErrorMessage) {
    if (!mHasActiveFile) {
      return true;
    }
    if (mBytesWritten != mBytesExpected) {
      return Fail("partial writer finish failed: file length mismatch.", pErrorMessage);
    }
    if (mWriteStream != nullptr && !mWriteStream->Close()) {
      return Fail("partial writer finish failed: stream close failed.", pErrorMessage);
    }
    ResetState();
    return true;
  }

  void AbortFile() {
    if (mWriteStream != nullptr) {
      (void)mWriteStream->Close();
    }
    ResetState();
  }

 private:
  static bool Fail(const std::string& pMessage, std::string* pErrorMessage) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = pMessage;
    }
    return false;
  }

  void ResetState() {
    mWriteStream.reset();
    mHasActiveFile = false;
    mBytesExpected = 0;
    mBytesWritten = 0;
  }

  FileSystem& mFileSystem;
  std::unique_ptr<FileWriteStream> mWriteStream;
  bool mHasActiveFile = false;
  std::uint64_t mBytesExpected = 0;
  std::uint64_t mBytesWritten = 0;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_IO_PARTIAL_FILE_WRITER_HPP_
