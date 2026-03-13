#include "StarterKitHelpers.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_set>

#include "PeanutButter.hpp"

namespace peanutbutter {
namespace testkit {
namespace {

bool IsEmptyDirectory(const std::string& pRelativeDirectory,
                      const std::vector<DirectoryEntry>& pFiles) {
  const std::string aPrefix = pRelativeDirectory + "/";
  for (const DirectoryEntry& aFile : pFiles) {
    if (aFile.mRelativePath == pRelativeDirectory) {
      return false;
    }
    if (aFile.mRelativePath.size() > aPrefix.size() &&
        aFile.mRelativePath.compare(0u, aPrefix.size(), aPrefix) == 0) {
      return false;
    }
  }
  return true;
}

std::vector<unsigned char> ReadAllBytes(FileSystem& pFileSystem,
                                        const std::string& pPath) {
  std::vector<unsigned char> aOut;
  std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(pPath);
  if (aRead == nullptr || !aRead->IsReady()) {
    return aOut;
  }
  aOut.resize(aRead->GetLength(), 0u);
  if (!aRead->Read(0u, aOut.data(), aOut.size())) {
    aOut.clear();
  }
  return aOut;
}

}  // namespace

bool BuildSourceEntriesFromDirectory(FileSystem& pFileSystem,
                                     const std::string& pSourceDirectory,
                                     std::vector<SourceEntry>& pOutEntries) {
  pOutEntries.clear();

  if (!pFileSystem.IsDirectory(pSourceDirectory)) {
    return false;
  }

  const std::vector<DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pSourceDirectory);
  const std::vector<DirectoryEntry> aDirectories = pFileSystem.ListDirectoriesRecursive(pSourceDirectory);

  pOutEntries.reserve(aFiles.size() + aDirectories.size());

  for (const DirectoryEntry& aFile : aFiles) {
    std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(aFile.mPath);
    if (aRead == nullptr || !aRead->IsReady()) {
      return false;
    }
    SourceEntry aEntry;
    aEntry.mSourcePath = aFile.mPath;
    aEntry.mRelativePath = aFile.mRelativePath;
    aEntry.mIsDirectory = false;
    aEntry.mFileLength = static_cast<std::uint64_t>(aRead->GetLength());
    pOutEntries.push_back(aEntry);
  }

  for (const DirectoryEntry& aDirectory : aDirectories) {
    if (!IsEmptyDirectory(aDirectory.mRelativePath, aFiles)) {
      continue;
    }
    SourceEntry aEntry;
    aEntry.mSourcePath.clear();
    aEntry.mRelativePath = aDirectory.mRelativePath;
    aEntry.mIsDirectory = true;
    aEntry.mFileLength = 0u;
    pOutEntries.push_back(aEntry);
  }

  std::sort(pOutEntries.begin(),
            pOutEntries.end(),
            [](const SourceEntry& pLeft, const SourceEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });
  return true;
}

std::vector<std::string> ListSortedArchiveFiles(FileSystem& pFileSystem,
                                                const std::string& pArchiveDirectory) {
  std::vector<std::string> aPaths;
  const std::vector<DirectoryEntry> aFiles = pFileSystem.ListFiles(pArchiveDirectory);
  aPaths.reserve(aFiles.size());
  for (const DirectoryEntry& aEntry : aFiles) {
    aPaths.push_back(aEntry.mPath);
  }
  std::sort(aPaths.begin(), aPaths.end());
  return aPaths;
}

bool SnapshotTree(FileSystem& pFileSystem,
                  const std::string& pRootDirectory,
                  std::vector<TreeSnapshotEntry>& pOutEntries) {
  pOutEntries.clear();
  if (!pFileSystem.IsDirectory(pRootDirectory)) {
    return false;
  }

  const std::vector<DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pRootDirectory);
  const std::vector<DirectoryEntry> aDirectories = pFileSystem.ListDirectoriesRecursive(pRootDirectory);

  pOutEntries.reserve(aFiles.size() + aDirectories.size());
  for (const DirectoryEntry& aDirectory : aDirectories) {
    TreeSnapshotEntry aEntry;
    aEntry.mRelativePath = aDirectory.mRelativePath;
    aEntry.mIsDirectory = true;
    pOutEntries.push_back(std::move(aEntry));
  }

  for (const DirectoryEntry& aFile : aFiles) {
    TreeSnapshotEntry aEntry;
    aEntry.mRelativePath = aFile.mRelativePath;
    aEntry.mIsDirectory = false;
    aEntry.mContents = ReadAllBytes(pFileSystem, aFile.mPath);
    if (aEntry.mContents.empty() && pFileSystem.IsFile(aFile.mPath)) {
      std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(aFile.mPath);
      if (aRead == nullptr || !aRead->IsReady() || aRead->GetLength() != 0u) {
        return false;
      }
    }
    pOutEntries.push_back(std::move(aEntry));
  }

  std::sort(pOutEntries.begin(),
            pOutEntries.end(),
            [](const TreeSnapshotEntry& pLeft, const TreeSnapshotEntry& pRight) {
              if (pLeft.mRelativePath != pRight.mRelativePath) {
                return pLeft.mRelativePath < pRight.mRelativePath;
              }
              return pLeft.mIsDirectory && !pRight.mIsDirectory;
            });
  return true;
}

bool CompareSnapshots(const std::vector<TreeSnapshotEntry>& pExpected,
                     const std::vector<TreeSnapshotEntry>& pActual,
                     std::string& pOutError) {
  if (pExpected.size() != pActual.size()) {
    pOutError = "snapshot entry count mismatch";
    return false;
  }

  for (std::size_t aIndex = 0u; aIndex < pExpected.size(); ++aIndex) {
    const TreeSnapshotEntry& aExpected = pExpected[aIndex];
    const TreeSnapshotEntry& aActual = pActual[aIndex];

    if (aExpected.mRelativePath != aActual.mRelativePath) {
      pOutError = "snapshot path mismatch at index " + std::to_string(aIndex);
      return false;
    }
    if (aExpected.mIsDirectory != aActual.mIsDirectory) {
      pOutError = "snapshot entry type mismatch at " + aExpected.mRelativePath;
      return false;
    }
    if (!aExpected.mIsDirectory && aExpected.mContents != aActual.mContents) {
      pOutError = "snapshot bytes mismatch at file " + aExpected.mRelativePath;
      return false;
    }
  }

  pOutError.clear();
  return true;
}

bool InspectArchive(FileSystem& pFileSystem,
                    const std::string& pArchivePath,
                    ArchiveInspection& pOutInspection) {
  pOutInspection = ArchiveInspection{};
  pOutInspection.mArchivePath = pArchivePath;

  std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(pArchivePath);
  if (aRead == nullptr || !aRead->IsReady()) {
    return false;
  }

  pOutInspection.mArchiveLength = aRead->GetLength();
  if (pOutInspection.mArchiveLength < kArchiveHeaderLength) {
    return false;
  }

  unsigned char aArchiveHeaderBytes[kArchiveHeaderLength] = {};
  if (!aRead->Read(0u, aArchiveHeaderBytes, sizeof(aArchiveHeaderBytes))) {
    return false;
  }
  pOutInspection.mArchiveHeaderReadable =
      ReadArchiveHeaderBytes(aArchiveHeaderBytes,
                             sizeof(aArchiveHeaderBytes),
                             pOutInspection.mArchiveHeader);

  const std::size_t aPayloadBytes = pOutInspection.mArchiveLength - kArchiveHeaderLength;
  const std::size_t aBlockCount = aPayloadBytes / kBlockSizeL3;

  pOutInspection.mBlocks.reserve(aBlockCount);
  for (std::size_t aBlock = 0u; aBlock < aBlockCount; ++aBlock) {
    L3BlockBuffer aBlockBuffer{};
    const std::size_t aOffset = kArchiveHeaderLength + (aBlock * kBlockSizeL3);
    if (!aRead->Read(aOffset, aBlockBuffer.Data(), kBlockSizeL3)) {
      return false;
    }

    ArchiveBlockInspection aInspection;
    aInspection.mBlockIndex = aBlock;
    aInspection.mRecoveryHeaderReadable =
        ReadRecoveryHeaderBytes(aBlockBuffer.Data(),
                                kRecoveryHeaderLength,
                                aInspection.mRecoveryHeader);

    if (aInspection.mRecoveryHeaderReadable) {
      aInspection.mExpectedChecksum =
          ComputeRecoveryChecksum(aBlockBuffer.Data(), aInspection.mRecoveryHeader.mSkip);
      aInspection.mChecksumMatches =
          ChecksumsEqual(aInspection.mExpectedChecksum,
                         aInspection.mRecoveryHeader.mChecksum);
    }

    pOutInspection.mBlocks.push_back(aInspection);
  }

  return true;
}

bool InspectArchiveSet(FileSystem& pFileSystem,
                       const std::vector<std::string>& pArchivePaths,
                       std::vector<ArchiveInspection>& pOutInspections) {
  pOutInspections.clear();
  pOutInspections.reserve(pArchivePaths.size());
  for (const std::string& aPath : pArchivePaths) {
    ArchiveInspection aInspection;
    if (!InspectArchive(pFileSystem, aPath, aInspection)) {
      return false;
    }
    pOutInspections.push_back(std::move(aInspection));
  }
  return true;
}

void PrintOperationResult(const std::string& pLabel,
                          const OperationResult& pResult) {
  std::cout << pLabel << " => success=" << (pResult.mSucceeded ? "true" : "false")
            << ", canceled=" << (pResult.mCanceled ? "true" : "false")
            << ", code=" << ErrorCodeToString(pResult.mErrorCode)
            << ", message='" << pResult.mFailureMessage << "'\n";
}

void PrintArchiveInspectionSummary(const std::vector<ArchiveInspection>& pInspections) {
  std::cout << "Archive inspection count: " << pInspections.size() << "\n";
  for (const ArchiveInspection& aArchive : pInspections) {
    std::size_t aChecksumGood = 0u;
    for (const ArchiveBlockInspection& aBlock : aArchive.mBlocks) {
      if (aBlock.mChecksumMatches) {
        ++aChecksumGood;
      }
    }

    std::cout << "  - " << aArchive.mArchivePath
              << ": size=" << aArchive.mArchiveLength
              << ", header=" << (aArchive.mArchiveHeaderReadable ? "ok" : "bad")
              << ", blocks=" << aArchive.mBlocks.size()
              << ", checksum_ok=" << aChecksumGood << "/" << aArchive.mBlocks.size()
              << "\n";
  }
}

}  // namespace testkit
}  // namespace peanutbutter
