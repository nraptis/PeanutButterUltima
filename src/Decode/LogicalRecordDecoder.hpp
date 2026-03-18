#ifndef PEANUT_BUTTER_ULTIMA_DECODE_LOGICAL_RECORD_DECODER_HPP_
#define PEANUT_BUTTER_ULTIMA_DECODE_LOGICAL_RECORD_DECODER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "AppShell_Common.hpp"
#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {
namespace decode_internal {

class LogicalRecordDecoder final {
 public:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
  };

  LogicalRecordDecoder(const std::string& pDestinationDirectory,
                       FileSystem& pFileSystem,
                       Logger& pLogger,
                       CancelCoordinator* pCancelCoordinator);

  bool Consume(const unsigned char* pData,
               std::size_t pStart,
               std::size_t pEnd,
               bool pRecoverMode,
               bool& pOutTerminated,
               bool& pOutParseError,
               std::string& pOutParseErrorMessage,
               bool& pOutCanceledAtBoundary,
               std::uint64_t& pOutDataBytesWritten);

  bool IsAtRecordBoundary() const;
  bool IsInsideFile() const;
  void RequestStopAfterCurrentFile();
  void AbortPartialFile();
  void ResetRecordState();

  std::uint64_t FilesWritten() const;
  std::uint64_t FoldersCreated() const;
  std::uint64_t BytesWritten() const;

 private:
  void FinishFileRecord(bool& pOutCanceledAtBoundary, bool pRecoverMode);

 private:
  std::string mDestinationDirectory;
  FileSystem& mFileSystem;
  Logger& mLogger;
  CancelCoordinator* mCancelCoordinator = nullptr;

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

  bool mStopAfterCurrentFile = false;

  std::uint64_t mFilesWritten = 0u;
  std::uint64_t mFoldersCreated = 0u;
  std::uint64_t mBytesWritten = 0u;
};

}  // namespace decode_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_DECODE_LOGICAL_RECORD_DECODER_HPP_
