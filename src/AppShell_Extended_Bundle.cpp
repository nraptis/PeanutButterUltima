#include "AppShell_Extended_Bundle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "AppShell_Bundle.hpp"

namespace peanutbutter {
namespace {

struct RecordInfo {
  std::string mSourcePath;
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::uint64_t mContentLength = 0;
  std::uint64_t mStartLogicalOffset = 0;
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
  aResult.mFailureMessage = "Bundle canceled by user.";
  return aResult;
}

OperationResult MakeFailure(ErrorCode pCode, const std::string& pMessage) {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mErrorCode = pCode;
  aResult.mFailureMessage = pMessage;
  return aResult;
}

std::uint64_t NextRecordDistance(const std::vector<std::uint64_t>& pRecordStarts,
                                 std::uint64_t pBlockLogicalStart) {
  if (pRecordStarts.empty()) {
    return 0;
  }
  const auto aIt = std::lower_bound(pRecordStarts.begin(), pRecordStarts.end(), pBlockLogicalStart);
  if (aIt == pRecordStarts.end()) {
    return 0;
  }
  return (*aIt >= pBlockLogicalStart) ? (*aIt - pBlockLogicalStart) : 0;
}

bool TryBuildSkipRecord(std::uint64_t pDistanceBytes,
                        std::uint32_t pBlocksPerArchive,
                        SkipRecord& pOutSkip) {
  pOutSkip = SkipRecord{};
  if (pBlocksPerArchive == 0u || pBlocksPerArchive > kMaxBlocksPerArchive) {
    return false;
  }
  const std::uint64_t aPayloadBytesPerArchive =
      static_cast<std::uint64_t>(pBlocksPerArchive) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
  if (aPayloadBytesPerArchive == 0u) {
    return false;
  }

  const std::uint64_t aArchiveDistance = pDistanceBytes / aPayloadBytesPerArchive;
  const std::uint64_t aArchiveRemainder = pDistanceBytes % aPayloadBytesPerArchive;
  const std::uint64_t aBlockDistance = aArchiveRemainder / static_cast<std::uint64_t>(kPayloadBytesPerL3);
  const std::uint64_t aByteDistance = aArchiveRemainder % static_cast<std::uint64_t>(kPayloadBytesPerL3);

  if (aArchiveDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  if (aBlockDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  if (aByteDistance > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }

  pOutSkip.mArchiveDistance = static_cast<std::uint16_t>(aArchiveDistance);
  pOutSkip.mBlockDistance = static_cast<std::uint16_t>(aBlockDistance);
  pOutSkip.mByteDistance = static_cast<std::uint32_t>(aByteDistance);
  return true;
}

void ApplyDataMutationsToBlockPayload(unsigned char* pPayloadBuffer,
                                      std::uint64_t pBlockLogicalStart,
                                      const std::vector<DataMutation>& pDataMutations) {
  if (pPayloadBuffer == nullptr || pDataMutations.empty()) {
    return;
  }

  const std::uint64_t aBlockLogicalEnd =
      pBlockLogicalStart + static_cast<std::uint64_t>(kPayloadBytesPerL3);
  for (const DataMutation& aMutation : pDataMutations) {
    const std::uint64_t aMutationStart = aMutation.mLogicalOffset;
    const std::uint64_t aMutationLength =
        static_cast<std::uint64_t>(aMutation.mOverwriteBytes.size());
    const std::uint64_t aMutationEnd = aMutationStart + aMutationLength;
    if (aMutationEnd <= pBlockLogicalStart || aMutationStart >= aBlockLogicalEnd) {
      continue;
    }

    const std::uint64_t aOverlapStart = std::max(aMutationStart, pBlockLogicalStart);
    const std::uint64_t aOverlapEnd = std::min(aMutationEnd, aBlockLogicalEnd);
    const std::size_t aCopyLength = static_cast<std::size_t>(aOverlapEnd - aOverlapStart);
    const std::size_t aSourceOffset = static_cast<std::size_t>(aOverlapStart - aMutationStart);
    const std::size_t aDestinationOffset = static_cast<std::size_t>(aOverlapStart - pBlockLogicalStart);

    std::memcpy(pPayloadBuffer + aDestinationOffset,
                aMutation.mOverwriteBytes.data() + aSourceOffset,
                aCopyLength);
  }
}

class BundleLogicalStream final {
 public:
  BundleLogicalStream(const std::vector<RecordInfo>& pRecords, FileSystem& pFileSystem)
      : mRecords(pRecords),
        mFileSystem(pFileSystem) {
    StartNextRecord();
  }

  bool Fill(unsigned char* pDestination,
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
            pDestination[pOutBytesWritten++] = mContentLengthLe[mContentLengthBytesUsed++];
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
              std::min<std::uint64_t>(static_cast<std::uint64_t>(aWritable), mContentBytesRemaining));
          if (aChunk == 0u) {
            FinishRecord();
            break;
          }
          if (!mCurrentRead->Read(mCurrentFileReadOffset, pDestination + pOutBytesWritten, aChunk)) {
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

  bool IsDone() const {
    return mDone;
  }

  bool IsInsideFile() const {
    return mStage == Stage::kContentBytes && !mCurrentRecordIsDirectory;
  }

  void RequestStopAfterCurrentFile() {
    if (mStopAfterCurrentFileRequested) {
      return;
    }
    mStopAfterCurrentFileRequested = true;
  }

  bool ReachedStopBoundary() const {
    return mReachedStopBoundary;
  }

 private:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
  };

  void StartNextRecord() {
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

    const RecordInfo& aRecord = mRecords[mRecordIndex];
    mCurrentRecordRelativePath = aRecord.mRelativePath;
    mCurrentRecordIsDirectory = aRecord.mIsDirectory;
    mCurrentPathLength = static_cast<std::uint16_t>(mCurrentRecordRelativePath.size());
    mPathLengthLe[0] = static_cast<unsigned char>(mCurrentPathLength & 0xFFu);
    mPathLengthLe[1] = static_cast<unsigned char>((mCurrentPathLength >> 8) & 0xFFu);

    std::uint64_t aContentLengthField = aRecord.mIsDirectory ? kDirectoryRecordContentMarker : aRecord.mContentLength;
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

  void FinishRecord() {
    if (!mCurrentRecordIsDirectory && mStopAfterCurrentFileRequested) {
      mReachedStopBoundary = true;
      mDone = true;
      return;
    }

    ++mRecordIndex;
    StartNextRecord();
  }

 private:
  const std::vector<RecordInfo>& mRecords;
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

}  // namespace

OperationResult DiscoverBundlePlanWithMutations(
    const BundleRequest& pRequest,
    const std::vector<SourceEntry>& pSourceEntries,
    const std::vector<CreateFileMutation>& pCreateMutations,
    const std::vector<DeleteFileMutation>& pDeleteMutations,
    FileSystem& pFileSystem,
    BundleDiscovery& pOutDiscovery,
    CancelCoordinator* pCancelCoordinator) {
  std::vector<SourceEntry> aMutatedSourceEntries;
  OperationResult aFileMutationResult = ApplyFileMutations(pSourceEntries,
                                                           pCreateMutations,
                                                           pDeleteMutations,
                                                           aMutatedSourceEntries);
  if (!aFileMutationResult.mSucceeded) {
    return aFileMutationResult;
  }

  NullLogger aNullLogger;
  return DiscoverBundlePlan(pRequest,
                            aMutatedSourceEntries,
                            pFileSystem,
                            aNullLogger,
                            pOutDiscovery,
                            pCancelCoordinator);
}

OperationResult PerformBundleFlightWithMutations(
    const BundleRequest& pRequest,
    const BundleDiscovery& pDiscovery,
    const std::vector<DataMutation>& pDataMutations,
    FileSystem& pFileSystem,
    const Crypt& pCrypt,
    CancelCoordinator* pCancelCoordinator) {
  if (pDiscovery.mArchives.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "bundle discovery did not plan archives.");
  }
  if (!pFileSystem.EnsureDirectory(pRequest.mDestinationDirectory)) {
    return MakeFailure(ErrorCode::kFileSystem, "failed to create destination directory.");
  }

  std::uint64_t aMutableBytes = 0u;
  for (const BundleArchivePlan& aArchive : pDiscovery.mArchives) {
    const std::uint64_t aArchiveBytes =
        static_cast<std::uint64_t>(aArchive.mBlockCount) * static_cast<std::uint64_t>(kPayloadBytesPerL3);
    if (aArchiveBytes > (std::numeric_limits<std::uint64_t>::max() - aMutableBytes)) {
      return MakeFailure(ErrorCode::kInternal, "payload range overflow while validating data mutations.");
    }
    aMutableBytes += aArchiveBytes;
  }
  OperationResult aDataMutationResult = ValidateDataMutations(pDataMutations, aMutableBytes);
  if (!aDataMutationResult.mSucceeded) {
    return aDataMutationResult;
  }

  std::vector<RecordInfo> aRecords;
  aRecords.reserve(pDiscovery.mResolvedEntries.size());
  for (std::size_t aIndex = 0u; aIndex < pDiscovery.mResolvedEntries.size(); ++aIndex) {
    const SourceEntry& aEntry = pDiscovery.mResolvedEntries[aIndex];
    RecordInfo aRecord;
    aRecord.mSourcePath = aEntry.mSourcePath;
    aRecord.mRelativePath = aEntry.mRelativePath;
    aRecord.mIsDirectory = aEntry.mIsDirectory;
    aRecord.mContentLength = aEntry.mIsDirectory ? 0u : aEntry.mFileLength;
    aRecord.mStartLogicalOffset =
        (aIndex < pDiscovery.mRecordStartLogicalOffsets.size()) ? pDiscovery.mRecordStartLogicalOffsets[aIndex] : 0u;
    aRecords.push_back(std::move(aRecord));
  }

  BundleLogicalStream aStream(aRecords, pFileSystem);
  L3BlockBuffer aPlainBlock{};
  L3BlockBuffer aEncryptedBlock{};
  L3BlockBuffer aWorkerBlock{};
  std::uint64_t aCurrentBlockLogicalStart = 0u;

  for (const BundleArchivePlan& aArchive : pDiscovery.mArchives) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() && !aStream.IsInsideFile()) {
      if (pCancelCoordinator->ShouldCancelNow()) {
        return MakeCanceled();
      }
    }

    std::unique_ptr<FileWriteStream> aWrite = pFileSystem.OpenWriteStream(aArchive.mArchivePath);
    if (aWrite == nullptr || !aWrite->IsReady()) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed creating archive file: " + aArchive.mArchivePath);
    }

    ArchiveHeader aHeader{};
    aHeader.mMagic = kMagicHeaderBytes;
    aHeader.mVersionMajor = static_cast<std::uint16_t>(kMajorVersion & 0xFFFFu);
    aHeader.mVersionMinor = static_cast<std::uint16_t>(kMinorVersion & 0xFFFFu);
    aHeader.mArchiveIndex = aArchive.mArchiveIndex;
    aHeader.mArchiveCount = static_cast<std::uint32_t>(pDiscovery.mArchives.size());
    aHeader.mPayloadLength = aArchive.mPayloadBytes;
    aHeader.mRecordCountMod256 = aArchive.mRecordCountMod256;
    aHeader.mFolderCountMod256 = aArchive.mFolderCountMod256;
    aHeader.mReserved16 = 0u;
    aHeader.mReservedA = 0u;
    aHeader.mReservedB = 0u;

    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    if (!WriteArchiveHeaderBytes(aHeader, aHeaderBytes, sizeof(aHeaderBytes))) {
      return MakeFailure(ErrorCode::kInternal, "failed serializing archive header.");
    }
    if (!aWrite->Write(aHeaderBytes, sizeof(aHeaderBytes))) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed writing archive header: " + aArchive.mArchivePath);
    }

    for (std::uint32_t aBlockIndex = 0u; aBlockIndex < aArchive.mBlockCount; ++aBlockIndex) {
      if (pCancelCoordinator != nullptr && pCancelCoordinator->IsCancelRequested() && aStream.IsInsideFile()) {
        aStream.RequestStopAfterCurrentFile();
      }

      std::memset(aPlainBlock.Data(), 0, kBlockSizeL3);

      std::size_t aPayloadBytesWritten = 0u;
      std::uint64_t aLogicalBytesInBlock = 0u;
      std::uint64_t aFileBytesInBlock = 0u;
      std::string aFailure;
      if (!aStream.Fill(aPlainBlock.Data() + kRecoveryHeaderLength,
                        kPayloadBytesPerL3,
                        aPayloadBytesWritten,
                        aLogicalBytesInBlock,
                        aFileBytesInBlock,
                        aFailure)) {
        return MakeFailure(ErrorCode::kFileSystem, aFailure);
      }

      (void)aLogicalBytesInBlock;
      (void)aFileBytesInBlock;
      (void)aPayloadBytesWritten;

      ApplyDataMutationsToBlockPayload(aPlainBlock.Data() + kRecoveryHeaderLength,
                                       aCurrentBlockLogicalStart,
                                       pDataMutations);

      const std::uint64_t aDistanceToNextRecord =
          NextRecordDistance(pDiscovery.mRecordStartLogicalOffsets, aCurrentBlockLogicalStart);
      aCurrentBlockLogicalStart += static_cast<std::uint64_t>(kPayloadBytesPerL3);

      RecoveryHeader aRecoveryHeader{};
      if (!TryBuildSkipRecord(aDistanceToNextRecord, aArchive.mBlockCount, aRecoveryHeader.mSkip)) {
        return MakeFailure(ErrorCode::kInternal,
                           "failed converting record skip into fixed-size skip record.");
      }
      aRecoveryHeader.mChecksum = ComputeRecoveryChecksum(aPlainBlock.Data(), aRecoveryHeader.mSkip);
      if (!WriteRecoveryHeaderBytes(aRecoveryHeader, aPlainBlock.Data(), kRecoveryHeaderLength)) {
        return MakeFailure(ErrorCode::kInternal, "failed serializing recovery header.");
      }

      if (pRequest.mUseEncryption) {
        std::string aCryptError;
        if (!pCrypt.SealData(aPlainBlock.Data(),
                             aWorkerBlock.Data(),
                             aEncryptedBlock.Data(),
                             kBlockSizeL3,
                             &aCryptError,
                             CryptMode::kNormal)) {
          return MakeFailure(ErrorCode::kCrypt,
                             aCryptError.empty() ? "encryption failed." : aCryptError);
        }
      } else {
        std::memcpy(aEncryptedBlock.Data(), aPlainBlock.Data(), kBlockSizeL3);
      }

      if (!aWrite->Write(aEncryptedBlock.Data(), kBlockSizeL3)) {
        return MakeFailure(ErrorCode::kFileSystem,
                           "failed writing archive block: " + aArchive.mArchivePath);
      }
    }

    if (!aWrite->Close()) {
      return MakeFailure(ErrorCode::kFileSystem,
                         "failed closing archive file: " + aArchive.mArchivePath);
    }

    if (aStream.ReachedStopBoundary()) {
      return MakeCanceled();
    }
  }

  if (!aStream.IsDone()) {
    return MakeFailure(ErrorCode::kInternal,
                       "bundle finished archives before logical stream terminated.");
  }
  return MakeSuccess();
}

OperationResult BundleWithMutations(
    const BundleRequest& pRequest,
    const std::vector<SourceEntry>& pSourceEntries,
    const std::vector<DataMutation>& pDataMutations,
    const std::vector<CreateFileMutation>& pCreateMutations,
    const std::vector<DeleteFileMutation>& pDeleteMutations,
    FileSystem& pFileSystem,
    const Crypt& pCrypt,
    CancelCoordinator* pCancelCoordinator) {
  BundleDiscovery aDiscovery;
  OperationResult aDiscoveryResult = DiscoverBundlePlanWithMutations(pRequest,
                                                                     pSourceEntries,
                                                                     pCreateMutations,
                                                                     pDeleteMutations,
                                                                     pFileSystem,
                                                                     aDiscovery,
                                                                     pCancelCoordinator);
  if (!aDiscoveryResult.mSucceeded) {
    return aDiscoveryResult;
  }
  return PerformBundleFlightWithMutations(pRequest,
                                          aDiscovery,
                                          pDataMutations,
                                          pFileSystem,
                                          pCrypt,
                                          pCancelCoordinator);
}

}  // namespace peanutbutter
