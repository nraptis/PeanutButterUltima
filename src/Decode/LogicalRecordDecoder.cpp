#include "Decode/LogicalRecordDecoder.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "AppShell_ArchiveFormat.hpp"

namespace peanutbutter {
namespace decode_internal {
namespace {

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

}  // namespace

LogicalRecordDecoder::LogicalRecordDecoder(
    const std::string& pDestinationDirectory,
    FileSystem& pFileSystem,
    Logger& pLogger,
    CancelCoordinator* pCancelCoordinator)
    : mDestinationDirectory(pDestinationDirectory),
      mFileSystem(pFileSystem),
      mLogger(pLogger),
      mCancelCoordinator(pCancelCoordinator) {}

bool LogicalRecordDecoder::Consume(const unsigned char* pData,
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
        const std::size_t aChunk =
            std::min<std::size_t>(aRemaining, pEnd - aOffset);
        mCurrentPath.append(reinterpret_cast<const char*>(pData + aOffset),
                            aChunk);
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
              static_cast<std::uint64_t>(
                  mContentLengthLe[static_cast<std::size_t>(aByte)])
              << (8 * aByte);
        }

        if (mCurrentContentLength == kDirectoryRecordContentMarker) {
          const std::string aDirPath =
              mFileSystem.JoinPath(mDestinationDirectory, mCurrentPath);
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
          pOutParseErrorMessage =
              "content length is implausibly large for a recoverable record.";
          return true;
        }

        const std::string aOutPath =
            mFileSystem.JoinPath(mDestinationDirectory, mCurrentPath);
        const std::string aOutParent = mFileSystem.ParentPath(aOutPath);
        if (!aOutParent.empty() && !mFileSystem.EnsureDirectory(aOutParent)) {
          pOutParseError = true;
          pOutParseErrorMessage =
              "failed creating parent directory for output file.";
          return false;
        }

        mCurrentWrite = mFileSystem.OpenWriteStream(aOutPath);
        if (mCurrentWrite == nullptr || !mCurrentWrite->IsReady()) {
          pOutParseError = true;
          pOutParseErrorMessage =
              "failed opening output file for writing.";
          return false;
        }

        mCurrentOutputPath = aOutPath;
        if (mCancelCoordinator != nullptr) {
          mCancelCoordinator->SetWritingPath(aOutPath);
        }
        mContentBytesRemaining = mCurrentContentLength;

        mStage = Stage::kContentBytes;
        break;
      }

      case Stage::kContentBytes: {
        const std::size_t aChunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                mContentBytesRemaining,
                static_cast<std::uint64_t>(pEnd - aOffset)));
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

bool LogicalRecordDecoder::IsAtRecordBoundary() const {
  return mStage == Stage::kPathLength &&
         mPathLengthBytesUsed == 0u &&
         mCurrentWrite == nullptr;
}

bool LogicalRecordDecoder::IsInsideFile() const {
  return mStage == Stage::kContentBytes && mCurrentWrite != nullptr;
}

void LogicalRecordDecoder::RequestStopAfterCurrentFile() {
  mStopAfterCurrentFile = true;
}

void LogicalRecordDecoder::AbortPartialFile() {
  if (mCurrentWrite != nullptr) {
    (void)mCurrentWrite->Close();
    mCurrentWrite.reset();
  }
  if (mCancelCoordinator != nullptr) {
    mCancelCoordinator->ClearActivity();
  }
}

void LogicalRecordDecoder::ResetRecordState() {
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
  if (mCancelCoordinator != nullptr && mCurrentWrite == nullptr) {
    mCancelCoordinator->ClearActivity();
  }
}

std::uint64_t LogicalRecordDecoder::FilesWritten() const {
  return mFilesWritten;
}

std::uint64_t LogicalRecordDecoder::FoldersCreated() const {
  return mFoldersCreated;
}

std::uint64_t LogicalRecordDecoder::BytesWritten() const {
  return mBytesWritten;
}

void LogicalRecordDecoder::FinishFileRecord(bool& pOutCanceledAtBoundary,
                                            bool) {
  if (mCurrentWrite != nullptr) {
    (void)mCurrentWrite->Close();
    mCurrentWrite.reset();
  }
  if (mCancelCoordinator != nullptr && !mCurrentOutputPath.empty()) {
    mCancelCoordinator->NoteFinishedWriting(mCurrentOutputPath);
    mCancelCoordinator->ClearActivity();
  }

  ++mFilesWritten;

  if (mStopAfterCurrentFile) {
    pOutCanceledAtBoundary = true;
    return;
  }

  ResetRecordState();
}

}  // namespace decode_internal
}  // namespace peanutbutter
