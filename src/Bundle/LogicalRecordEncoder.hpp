#ifndef PEANUT_BUTTER_ULTIMA_BUNDLE_LOGICAL_RECORD_ENCODER_HPP_
#define PEANUT_BUTTER_ULTIMA_BUNDLE_LOGICAL_RECORD_ENCODER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {
namespace bundle_internal {

struct BundleRecordInfo {
  std::string mSourcePath;
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::uint64_t mContentLength = 0;
  std::uint64_t mStartLogicalOffset = 0;
};

class LogicalRecordEncoder final {
 public:
  LogicalRecordEncoder(const std::vector<BundleRecordInfo>& pRecords,
                       FileSystem& pFileSystem);

  bool Fill(unsigned char* pDestination,
            std::size_t pCapacity,
            std::size_t& pOutBytesWritten,
            std::uint64_t& pOutLogicalBytesWritten,
            std::uint64_t& pOutFileBytesWritten,
            std::string& pOutFailureMessage);

  bool IsDone() const;
  bool IsInsideFile() const;
  void RequestStopAfterCurrentFile();
  void RequestStopImmediately();
  bool ReachedStopBoundary() const;
  std::size_t PackedItemCount() const;

 private:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
  };

  void StartNextRecord();
  void FinishRecord();

 private:
  const std::vector<BundleRecordInfo>& mRecords;
  FileSystem& mFileSystem;

  std::size_t mRecordIndex = 0u;
  Stage mStage = Stage::kPathLength;
  bool mDone = false;

  std::string mCurrentRecordRelativePath;
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

}  // namespace bundle_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_BUNDLE_LOGICAL_RECORD_ENCODER_HPP_
