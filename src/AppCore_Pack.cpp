#include "AppCore_Helpers.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace peanutbutter::detail {
namespace {

struct PlannedArchiveLayout {
  std::size_t mLogicalStart = 0;
  std::size_t mLogicalEnd = 0;
  std::size_t mUsedPayloadLength = 0;
};

struct PackWorkspace {
  unsigned char mLogicalChunk[kPageLength] = {};
  unsigned char mPageBuffer[kPageLength] = {};
  unsigned char mWorker[kPageLength] = {};
  unsigned char mDestination[kPageLength] = {};
};

std::uint64_t SerializedRecordLength(const SourceFileEntry& pEntry) {
  return 2ULL + static_cast<std::uint64_t>(pEntry.mRelativePath.size()) + 6ULL + pEntry.mContentLength;
}

std::uint64_t SerializedDirectoryRecordLength(const std::string& pPath) {
  return 2ULL + static_cast<std::uint64_t>(pPath.size()) + 6ULL;
}

std::vector<PlannedArchiveLayout> BuildArchiveLayouts(std::size_t pLogicalByteLength,
                                                      std::size_t pLogicalCapacity) {
  std::vector<PlannedArchiveLayout> aLayouts;
  if (pLogicalByteLength == 0 || pLogicalCapacity == 0) {
    return aLayouts;
  }

  const std::size_t aArchiveCount = (pLogicalByteLength + pLogicalCapacity - 1) / pLogicalCapacity;
  aLayouts.reserve(aArchiveCount);
  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aArchiveCount; ++aArchiveIndex) {
    PlannedArchiveLayout aLayout;
    aLayout.mLogicalStart = aArchiveIndex * pLogicalCapacity;
    aLayout.mLogicalEnd = std::min(pLogicalByteLength, aLayout.mLogicalStart + pLogicalCapacity);
    aLayout.mUsedPayloadLength = PhysicalLengthForLogicalLength(aLayout.mLogicalEnd - aLayout.mLogicalStart);
    aLayouts.push_back(aLayout);
  }
  return aLayouts;
}

std::vector<std::size_t> BuildRecordPhysicalOffsetsForLayout(
    const PlannedArchiveLayout& pLayout,
    const std::vector<std::size_t>& pRecordStartLogicalOffsets) {
  std::vector<std::size_t> aPhysicalOffsets;
  const auto aBegin = std::lower_bound(
      pRecordStartLogicalOffsets.begin(), pRecordStartLogicalOffsets.end(), pLayout.mLogicalStart);
  for (auto aIt = aBegin; aIt != pRecordStartLogicalOffsets.end() && *aIt < pLayout.mLogicalEnd; ++aIt) {
    aPhysicalOffsets.push_back(PhysicalOffsetForLogicalOffset(*aIt - pLayout.mLogicalStart));
  }
  return aPhysicalOffsets;
}

bool CopyLogicalBytesIntoPage(unsigned char* pPageBuffer,
                              std::size_t pPageLength,
                              std::size_t pPageLogicalOffset,
                              const unsigned char* pSourceBytes,
                              std::size_t pSourceLength) {
  std::size_t aCopied = 0;
  std::size_t aLogicalOffset = pPageLogicalOffset;
  while (aCopied < pSourceLength) {
    const std::size_t aPhysicalOffset = PhysicalOffsetForLogicalOffset(aLogicalOffset);
    if (aPhysicalOffset >= pPageLength) {
      return false;
    }

    const std::size_t aOffsetInBlockPayload = aLogicalOffset % kPayloadBytesPerBlock;
    const std::size_t aPayloadSpace = kPayloadBytesPerBlock - aOffsetInBlockPayload;
    const std::size_t aPhysicalSpace = pPageLength - aPhysicalOffset;
    const std::size_t aSpan = std::min({pSourceLength - aCopied, aPayloadSpace, aPhysicalSpace});
    std::memcpy(pPageBuffer + aPhysicalOffset, pSourceBytes + aCopied, aSpan);
    aCopied += aSpan;
    aLogicalOffset += aSpan;
  }
  return true;
}

void InitializePageRecoveryHeaders(unsigned char* pPageBuffer,
                                   std::size_t pPageLength,
                                   std::size_t pPageStartOffset,
                                   const std::vector<std::size_t>& pRecordStartPhysicalOffsets) {
  std::memset(pPageBuffer, 0, pPageLength);
  std::size_t aNextRecordOffsetIndex = std::lower_bound(
      pRecordStartPhysicalOffsets.begin(), pRecordStartPhysicalOffsets.end(), pPageStartOffset + kRecoveryHeaderLength) -
                                       pRecordStartPhysicalOffsets.begin();

  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = pPageStartOffset + (aBlockIndex * kBlockLength);
    if (aBlockStart + kRecoveryHeaderLength > pPageStartOffset + pPageLength) {
      break;
    }

    const std::size_t aRecoveryEnd = aBlockStart + kRecoveryHeaderLength;
    while (aNextRecordOffsetIndex < pRecordStartPhysicalOffsets.size() &&
           pRecordStartPhysicalOffsets[aNextRecordOffsetIndex] < aRecoveryEnd) {
      ++aNextRecordOffsetIndex;
    }

    RecoveryHeader aHeader{};
    if (aNextRecordOffsetIndex < pRecordStartPhysicalOffsets.size()) {
      aHeader.mDistanceToNextRecord =
          static_cast<std::uint64_t>(pRecordStartPhysicalOffsets[aNextRecordOffsetIndex] - aRecoveryEnd);
    }
    std::memcpy(pPageBuffer + (aBlockIndex * kBlockLength), &aHeader, sizeof(aHeader));
  }
}

void FinalizePageRecoveryHeaders(unsigned char* pPageBuffer, std::size_t pPageLength) {
  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = aBlockIndex * kBlockLength;
    if (aBlockStart + kRecoveryHeaderLength > pPageLength) {
      break;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, pPageBuffer + aBlockStart, sizeof(aHeader));
    unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
    GenerateChecksum(pPageBuffer + aBlockStart, aChecksum);
    std::memcpy(&aHeader.mChecksum, aChecksum, peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH);
    std::memcpy(pPageBuffer + aBlockStart, &aHeader, sizeof(aHeader));
  }
}

bool WriteArchivePage(FileWriteStream& pStream,
                      const Crypt& pCrypt,
                      PackWorkspace& pWorkspace,
                      std::size_t pPageLength,
                      bool pUseEncryption,
                      std::string* pErrorMessage) {
  if (!pUseEncryption) {
    return pStream.Write(pWorkspace.mPageBuffer, pPageLength);
  }

  std::memset(pWorkspace.mWorker, 0, kPageLength);
  std::memset(pWorkspace.mDestination, 0, kPageLength);
  if (!pCrypt.SealData(pWorkspace.mPageBuffer,
                       pWorkspace.mWorker,
                       pWorkspace.mDestination,
                       kPageLength,
                       pErrorMessage,
                       CryptMode::kNormal)) {
    return false;
  }
  return pStream.Write(pWorkspace.mDestination, pPageLength);
}

class LogicalRecordStreamer {
 public:
  LogicalRecordStreamer(const FileSystem& pFileSystem,
                        const std::vector<SourceFileEntry>& pFiles,
                        const std::vector<std::string>& pEmptyDirectories)
      : mFileSystem(pFileSystem),
        mFiles(pFiles),
        mEmptyDirectories(pEmptyDirectories) {}

  bool Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead) {
    pBytesRead = 0;
    while (pBytesRead < pMaxBytes) {
      if (mPhase == Phase::kPrepareRecord) {
        if (!PrepareNextRecord()) {
          return false;
        }
        continue;
      }

      if (mPhase == Phase::kPathLengthBytes) {
        const std::size_t aRemainingSource = 2 - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mLengthBytes + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == 2) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kEndMarker) {
            mPhase = Phase::kDone;
          } else {
            mPhase = Phase::kPathBytes;
          }
        }
        continue;
      }

      if (mPhase == Phase::kPathBytes) {
        const std::size_t aRemainingSource = mCurrentPath.size() - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mCurrentPath.data() + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == mCurrentPath.size()) {
          mPhase = Phase::kContentLengthBytes;
          mPhaseOffset = 0;
        }
        continue;
      }

      if (mPhase == Phase::kContentLengthBytes) {
        const std::size_t aRemainingSource = 6 - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mContentLengthBytes + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == 6) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kFile) {
            mContentOffset = 0;
            mPhase = Phase::kContentBytes;
          } else if (mCurrentKind == RecordKind::kDirectory) {
            ++mDirectoryIndex;
            mPhase = Phase::kPrepareRecord;
          } else {
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
        pBytesRead += aBytesToRead;
        mContentOffset += aBytesToRead;
        if (mContentOffset == aFile.mContentLength) {
          mReadStream.reset();
          ++mFileIndex;
          mPhase = Phase::kPrepareRecord;
        }
        continue;
      }

      if (mPhase == Phase::kDone) {
        return true;
      }
    }

    return true;
  }

 private:
  enum class RecordKind {
    kNone,
    kFile,
    kDirectory,
    kEndMarker,
  };

  enum class Phase {
    kPrepareRecord,
    kPathLengthBytes,
    kPathBytes,
    kContentLengthBytes,
    kContentBytes,
    kDone,
  };

  bool PrepareNextRecord() {
    mPhaseOffset = 0;
    mCurrentPath.clear();
    mCurrentKind = RecordKind::kNone;

    if (mFileIndex < mFiles.size()) {
      const SourceFileEntry& aFile = mFiles[mFileIndex];
      if (aFile.mRelativePath.size() > std::numeric_limits<std::uint16_t>::max() ||
          aFile.mRelativePath.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
        return false;
      }
      mCurrentKind = RecordKind::kFile;
      mCurrentPath = aFile.mRelativePath;
      WriteLeToBytes(mLengthBytes, static_cast<std::uint64_t>(mCurrentPath.size()), 2);
      WriteLeToBytes(mContentLengthBytes, aFile.mContentLength, 6);
      mReadStream = mFileSystem.OpenReadStream(aFile.mSourcePath);
      if (mReadStream == nullptr || !mReadStream->IsReady() ||
          mReadStream->GetLength() != static_cast<std::size_t>(aFile.mContentLength)) {
        return false;
      }
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (mDirectoryIndex < mEmptyDirectories.size()) {
      const std::string& aDirectory = mEmptyDirectories[mDirectoryIndex];
      if (aDirectory.size() > std::numeric_limits<std::uint16_t>::max() ||
          aDirectory.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
        return false;
      }
      mCurrentKind = RecordKind::kDirectory;
      mCurrentPath = aDirectory;
      WriteLeToBytes(mLengthBytes, static_cast<std::uint64_t>(mCurrentPath.size()), 2);
      WriteLeToBytes(mContentLengthBytes, kDirectoryRecordContentMarker, 6);
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (!mEndMarkerWritten) {
      mCurrentKind = RecordKind::kEndMarker;
      mLengthBytes[0] = 0;
      mLengthBytes[1] = 0;
      mEndMarkerWritten = true;
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    mPhase = Phase::kDone;
    return true;
  }

  const FileSystem& mFileSystem;
  const std::vector<SourceFileEntry>& mFiles;
  const std::vector<std::string>& mEmptyDirectories;
  std::size_t mFileIndex = 0;
  std::size_t mDirectoryIndex = 0;
  std::size_t mPhaseOffset = 0;
  std::uint64_t mContentOffset = 0;
  bool mEndMarkerWritten = false;
  RecordKind mCurrentKind = RecordKind::kNone;
  Phase mPhase = Phase::kPrepareRecord;
  std::string mCurrentPath;
  unsigned char mLengthBytes[2] = {};
  unsigned char mContentLengthBytes[6] = {};
  std::unique_ptr<FileReadStream> mReadStream;
};

}  // namespace

PreflightResult CheckBundleJob(FileSystem& pFileSystem,
                               const RuntimeSettings& pSettings,
                               const BundleRequest& pRequest) {
  const std::optional<BundleInputSelection> aSourceSelection =
      ResolveBundleInputSelection(pFileSystem, pRequest.mSourceDirectory);
  if (!aSourceSelection.has_value()) {
    return MakeInvalid("Bundle Failed", "Bundle failed: source file or folder does not exist.");
  }
  if (!aSourceSelection->mSingleFile &&
      pFileSystem.ListFilesRecursive(aSourceSelection->mSearchDirectory).empty() &&
      pFileSystem.ListDirectoriesRecursive(aSourceSelection->mSearchDirectory).empty()) {
    return MakeInvalid("Bundle Failed", "Bundle failed: source directory is empty.");
  }

  RuntimeSettings aBundleSettings;
  std::string aErrorMessage;
  if (!TryBuildBundleSettings(pSettings, pRequest, aBundleSettings, &aErrorMessage)) {
    return MakeInvalid("Bundle Failed", aErrorMessage);
  }
  if (EffectiveArchiveLogicalPayloadLength(aBundleSettings) == 0) {
    return MakeInvalid("Bundle Failed", "Bundle failed: archive file length is too small.");
  }
  if (pFileSystem.DirectoryHasEntries(pRequest.mDestinationDirectory)) {
    return MakeNeedsDestination("Bundle Destination", "Bundle destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult RunBundleJob(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const RuntimeSettings& pSettings,
                             const BundleRequest& pRequest,
                             DestinationAction pAction) {
  const PreflightResult aPreflight = CheckBundleJob(pFileSystem, pSettings, pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Bundle Canceled", "Bundle canceled."};
  }
  if (!ApplyDestinationAction(pFileSystem, pRequest.mDestinationDirectory, pAction)) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not prepare destination directory.");
  }

  RuntimeSettings aBundleSettings;
  std::string aSettingsError;
  if (!TryBuildBundleSettings(pSettings, pRequest, aBundleSettings, &aSettingsError)) {
    return MakeFailure(pLogger, "Bundle Failed", aSettingsError);
  }

  const std::optional<BundleInputSelection> aSourceSelection =
      ResolveBundleInputSelection(pFileSystem, pRequest.mSourceDirectory);
  if (!aSourceSelection.has_value()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: source file or folder does not exist.");
  }

  pLogger.LogStatus("Bundle job starting...");
  const std::vector<SourceFileEntry> aFiles = CollectSourceEntries(pFileSystem, aSourceSelection.value());
  const std::vector<std::string> aEmptyDirectories = CollectEmptyDirectoryEntries(pFileSystem, aSourceSelection.value());
  if (aFiles.empty() && aEmptyDirectories.empty()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not read source files.");
  }
  pLogger.LogStatus("Found " + std::to_string(aFiles.size()) + " files and " +
                    std::to_string(aEmptyDirectories.size()) + " empty folders to bundle.");

  const std::size_t aLogicalCapacity = EffectiveArchiveLogicalPayloadLength(aBundleSettings);
  if (aLogicalCapacity == 0) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: archive file length is too small.");
  }

  std::vector<std::size_t> aRecordStartLogicalOffsets;
  std::vector<std::size_t> aFileEndLogicalOffsets;
  aRecordStartLogicalOffsets.reserve(aFiles.size() + aEmptyDirectories.size() + 1);
  aFileEndLogicalOffsets.reserve(aFiles.size());

  std::size_t aTotalLogicalLength = 0;
  std::uint64_t aTotalContentBytes = 0;
  for (const SourceFileEntry& aFile : aFiles) {
    aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalLength += static_cast<std::size_t>(SerializedRecordLength(aFile));
    aFileEndLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalContentBytes += aFile.mContentLength;
  }
  for (const std::string& aDirectory : aEmptyDirectories) {
    aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalLength += static_cast<std::size_t>(SerializedDirectoryRecordLength(aDirectory));
  }
  aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
  aTotalLogicalLength += 2;

  const std::vector<PlannedArchiveLayout> aLayouts = BuildArchiveLayouts(aTotalLogicalLength, aLogicalCapacity);
  if (aLayouts.empty()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: no archive payloads were generated.");
  }

  const std::uint64_t aArchiveIdentifier = GenerateArchiveIdentifier();
  const std::string aSourceStem = pFileSystem.StemName(aSourceSelection->mSourcePath);
  std::unique_ptr<PackWorkspace> aWorkspace = std::make_unique<PackWorkspace>();
  LogicalRecordStreamer aStreamer(pFileSystem, aFiles, aEmptyDirectories);

  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aNextCompletedFileIndex = 0;
  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aLayouts.size(); ++aArchiveIndex) {
    const PlannedArchiveLayout& aLayout = aLayouts[aArchiveIndex];
    const std::vector<std::size_t> aRecordStartPhysicalOffsets =
        BuildRecordPhysicalOffsetsForLayout(aLayout, aRecordStartLogicalOffsets);
    ArchiveHeader aHeader;
    aHeader.mIdentifier = aArchiveIdentifier;
    aHeader.mArchiveIndex = static_cast<std::uint32_t>(aArchiveIndex);
    aHeader.mArchiveCount = static_cast<std::uint32_t>(aLayouts.size());
    aHeader.mPayloadLength = static_cast<std::uint32_t>(aLayout.mUsedPayloadLength);
    aHeader.mRecordCountMod256 = static_cast<std::uint8_t>(aFiles.size() & 0xFFu);
    aHeader.mFolderCountMod256 = static_cast<std::uint8_t>(aEmptyDirectories.size() & 0xFFu);

    const std::string aArchiveName =
        MakeArchiveName(aSourceStem, pRequest.mArchivePrefix, pRequest.mArchiveSuffix, aArchiveIndex + 1, aLayouts.size());
    std::unique_ptr<FileWriteStream> aArchiveStream =
        pFileSystem.OpenWriteStream(pFileSystem.JoinPath(pRequest.mDestinationDirectory, aArchiveName));
    if (aArchiveStream == nullptr || !aArchiveStream->IsReady()) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not write archive " + aArchiveName);
    }

    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    WriteArchiveHeaderBytes(aHeader, aHeaderBytes);
    if (!aArchiveStream->Write(aHeaderBytes, kArchiveHeaderLength)) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not write archive " + aArchiveName);
    }

    const std::size_t aArchiveLogicalLength = aLayout.mLogicalEnd - aLayout.mLogicalStart;
    std::size_t aArchiveLogicalWritten = 0;
    for (std::size_t aPageStart = 0; aPageStart < aLayout.mUsedPayloadLength; aPageStart += kPageLength) {
      const std::size_t aPageLength = std::min(kPageLength, aLayout.mUsedPayloadLength - aPageStart);
      InitializePageRecoveryHeaders(aWorkspace->mPageBuffer,
                                    aPageLength,
                                    aPageStart,
                                    aRecordStartPhysicalOffsets);

      const std::size_t aPageLogicalTarget =
          std::min(LogicalCapacityForPhysicalLength(aPageLength), aArchiveLogicalLength - aArchiveLogicalWritten);
      std::size_t aPageLogicalWritten = 0;
      while (aPageLogicalWritten < aPageLogicalTarget) {
        const std::size_t aChunkTarget = std::min(kPageLength, aPageLogicalTarget - aPageLogicalWritten);
        std::size_t aChunkBytesRead = 0;
        if (!aStreamer.Read(aWorkspace->mLogicalChunk, aChunkTarget, aChunkBytesRead) || aChunkBytesRead == 0) {
          return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not stream source bytes.");
        }
        if (!CopyLogicalBytesIntoPage(aWorkspace->mPageBuffer,
                                      aPageLength,
                                      aPageLogicalWritten,
                                      aWorkspace->mLogicalChunk,
                                      aChunkBytesRead)) {
          return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: page packing overflowed unexpectedly.");
        }
        aPageLogicalWritten += aChunkBytesRead;
      }

      FinalizePageRecoveryHeaders(aWorkspace->mPageBuffer, aPageLength);
      std::string aCryptError;
      if (!WriteArchivePage(*aArchiveStream,
                            pCrypt,
                            *aWorkspace,
                            aPageLength,
                            pRequest.mUseEncryption,
                            &aCryptError)) {
        return MakeFailure(pLogger,
                           "Bundle Failed",
                           aCryptError.empty() ? "Bundle failed: could not write archive " + aArchiveName
                                               : "Bundle failed: " + aCryptError);
      }
      aArchiveLogicalWritten += aPageLogicalWritten;
    }

    if (!aArchiveStream->Close()) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not finalize archive " + aArchiveName);
    }

    while (aNextCompletedFileIndex < aFileEndLogicalOffsets.size() &&
           aFileEndLogicalOffsets[aNextCompletedFileIndex] <= aLayout.mLogicalEnd) {
      ++aFilesProcessed;
      aProcessedBytes += aFiles[aNextCompletedFileIndex].mContentLength;
      ++aNextCompletedFileIndex;
    }

    pLogger.LogStatus("Bundled archive " + std::to_string(aArchiveIndex + 1) + " / " +
                      std::to_string(aLayouts.size()) + ", " + std::to_string(aFilesProcessed) +
                      " files written, " + FormatBytes(aProcessedBytes) + " / " +
                      FormatBytes(aTotalContentBytes) + ".");
  }

  pLogger.LogStatus("Bundle job complete.");
  return {true, "Bundle Complete", "Bundle completed successfully."};
}

}  // namespace peanutbutter::detail
