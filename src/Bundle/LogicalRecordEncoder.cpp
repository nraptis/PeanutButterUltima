#include "Bundle/LogicalRecordEncoder.hpp"

#include <algorithm>
#include <cstring>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace bundle_internal {

LogicalRecordEncoder::LogicalRecordEncoder(
    const std::vector<BundleRecordInfo>& pRecords,
    FileSystem& pFileSystem)
    : mRecords(pRecords),
      mFileSystem(pFileSystem) {
  StartNextRecord();
}

bool LogicalRecordEncoder::Fill(unsigned char* pDestination,
                                std::size_t pCapacity,
                                std::size_t& pOutBytesWritten,
                                std::uint64_t& pOutLogicalBytesWritten,
                                std::uint64_t& pOutFileBytesWritten,
                                std::string& pOutFailureMessage) {
  pOutBytesWritten = 0u;
  pOutLogicalBytesWritten = 0u;
  pOutFileBytesWritten = 0u;
  pOutFailureMessage.clear();

  if (pDestination == nullptr && pCapacity > 0u) {
    pOutFailureMessage = "null destination payload buffer.";
    return false;
  }

  while (pOutBytesWritten < pCapacity && !mDone) {
    const std::size_t aWritable = pCapacity - pOutBytesWritten;
    switch (mStage) {
      case Stage::kPathLength: {
        while (mPathLengthBytesUsed < 2u && pOutBytesWritten < pCapacity) {
          pDestination[pOutBytesWritten++] = mPathLengthLe[mPathLengthBytesUsed++];
          ++pOutLogicalBytesWritten;
        }
        if (mPathLengthBytesUsed == 2u) {
          if (mCurrentPathLength == 0u) {
            mDone = true;
          } else {
            mStage = Stage::kPathBytes;
          }
        }
        break;
      }

      case Stage::kPathBytes: {
        const std::size_t aRemaining = mCurrentPathLength - mPathBytesUsed;
        const std::size_t aChunk = std::min<std::size_t>(aWritable, aRemaining);
        if (aChunk == 0u) {
          mStage = Stage::kContentLength;
          break;
        }
        std::memcpy(pDestination + pOutBytesWritten,
                    mCurrentRecordRelativePath.data() + mPathBytesUsed,
                    aChunk);
        pOutBytesWritten += aChunk;
        pOutLogicalBytesWritten += static_cast<std::uint64_t>(aChunk);
        mPathBytesUsed += aChunk;
        if (mPathBytesUsed == mCurrentPathLength) {
          mStage = Stage::kContentLength;
        }
        break;
      }

      case Stage::kContentLength: {
        while (mContentLengthBytesUsed < 8u && pOutBytesWritten < pCapacity) {
          pDestination[pOutBytesWritten++] =
              mContentLengthLe[mContentLengthBytesUsed++];
          ++pOutLogicalBytesWritten;
        }
        if (mContentLengthBytesUsed == 8u) {
          if (mCurrentRecordIsDirectory) {
            FinishRecord();
          } else {
            mStage = Stage::kContentBytes;
          }
        }
        break;
      }

      case Stage::kContentBytes: {
        if (mCurrentRead == nullptr || !mCurrentRead->IsReady()) {
          pOutFailureMessage = "source file stream is not readable.";
          return false;
        }

        const std::size_t aChunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(aWritable),
                mContentBytesRemaining));
        if (aChunk == 0u) {
          FinishRecord();
          break;
        }
        if (!mCurrentRead->Read(mCurrentFileReadOffset,
                                pDestination + pOutBytesWritten,
                                aChunk)) {
          pOutFailureMessage = "failed reading source file bytes.";
          return false;
        }

        pOutBytesWritten += aChunk;
        pOutLogicalBytesWritten += static_cast<std::uint64_t>(aChunk);
        pOutFileBytesWritten += static_cast<std::uint64_t>(aChunk);
        mCurrentFileReadOffset += aChunk;
        mContentBytesRemaining -= static_cast<std::uint64_t>(aChunk);
        if (mContentBytesRemaining == 0u) {
          FinishRecord();
        }
        break;
      }
    }
  }

  return true;
}

bool LogicalRecordEncoder::IsDone() const {
  return mDone;
}

bool LogicalRecordEncoder::IsInsideFile() const {
  return mStage == Stage::kContentBytes && !mCurrentRecordIsDirectory;
}

void LogicalRecordEncoder::RequestStopAfterCurrentFile() {
  if (mStopAfterCurrentFileRequested) {
    return;
  }
  mStopAfterCurrentFileRequested = true;
}

void LogicalRecordEncoder::RequestStopImmediately() {
  mStopAfterCurrentFileRequested = true;
  mReachedStopBoundary = true;
  mDone = true;
  mContentBytesRemaining = 0u;
  mCurrentRead.reset();
}

bool LogicalRecordEncoder::ReachedStopBoundary() const {
  return mReachedStopBoundary;
}

std::size_t LogicalRecordEncoder::PackedItemCount() const {
  return mRecordIndex;
}

void LogicalRecordEncoder::StartNextRecord() {
  mPathLengthBytesUsed = 0u;
  mPathBytesUsed = 0u;
  mContentLengthBytesUsed = 0u;
  mContentBytesRemaining = 0u;
  mCurrentFileReadOffset = 0u;
  mCurrentRead.reset();
  mCurrentRecordRelativePath.clear();

  if (mRecordIndex >= mRecords.size()) {
    mCurrentPathLength = 0u;
    mPathLengthLe[0] = 0;
    mPathLengthLe[1] = 0;
    mCurrentRecordIsDirectory = false;
    mStage = Stage::kPathLength;
    return;
  }

  const BundleRecordInfo& aRecord = mRecords[mRecordIndex];
  mCurrentRecordRelativePath = aRecord.mRelativePath;
  mCurrentRecordIsDirectory = aRecord.mIsDirectory;
  mCurrentPathLength =
      static_cast<std::uint16_t>(mCurrentRecordRelativePath.size());
  mPathLengthLe[0] = static_cast<unsigned char>(mCurrentPathLength & 0xFFu);
  mPathLengthLe[1] =
      static_cast<unsigned char>((mCurrentPathLength >> 8) & 0xFFu);

  const std::uint64_t aContentLengthField =
      aRecord.mIsDirectory ? kDirectoryRecordContentMarker : aRecord.mContentLength;
  for (int aByte = 0; aByte < 8; ++aByte) {
    mContentLengthLe[static_cast<std::size_t>(aByte)] =
        static_cast<unsigned char>((aContentLengthField >> (8 * aByte)) & 0xFFu);
  }
  mContentBytesRemaining = aRecord.mIsDirectory ? 0u : aRecord.mContentLength;

  if (!mCurrentRecordIsDirectory) {
    mCurrentRead = mFileSystem.OpenReadStream(aRecord.mSourcePath);
  }

  mStage = Stage::kPathLength;
}

void LogicalRecordEncoder::FinishRecord() {
  if (!mCurrentRecordIsDirectory && mStopAfterCurrentFileRequested) {
    mReachedStopBoundary = true;
    mDone = true;
    return;
  }

  ++mRecordIndex;
  StartNextRecord();
}

}  // namespace bundle_internal
}  // namespace peanutbutter
