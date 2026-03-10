#include "Test_Wrappers.hpp"

#include <algorithm>
#include <cstring>

namespace peanutbutter::testing {

std::string TestFile::DataString() const {
  return std::string(reinterpret_cast<const char*>(mData.data()), mData.size());
}

std::string TestArchive::DataString() const {
  return std::string(reinterpret_cast<const char*>(mData.data()), mData.size());
}

bool CollectTestFilesRecursive(FileSystem& pFileSystem,
                               const std::string& pDirectory,
                               std::vector<TestFile>& pFiles,
                               std::string& pErrorMessage) {
  pFiles.clear();
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFilesRecursive(pDirectory);
  std::sort(aEntries.begin(), aEntries.end(), [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });

  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    ByteVector aBytes;
    if (!pFileSystem.ReadFile(aEntry.mPath, aBytes)) {
      pErrorMessage = "could not read file: " + aEntry.mRelativePath;
      return false;
    }
    pFiles.push_back({aEntry.mRelativePath, std::move(aBytes)});
  }

  return true;
}

bool CollectTestArchives(FileSystem& pFileSystem,
                         const std::string& pArchiveDirectory,
                         std::vector<TestArchive>& pArchives,
                         std::string& pErrorMessage) {
  pArchives.clear();
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFiles(pArchiveDirectory);
  std::sort(aEntries.begin(), aEntries.end(), [&pFileSystem](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pFileSystem.FileName(pLeft.mPath) < pFileSystem.FileName(pRight.mPath);
  });

  for (const DirectoryEntry& aEntry : aEntries) {
    TestArchive aArchive;
    aArchive.mFilePath = aEntry.mPath;
    if (!pFileSystem.ReadFile(aEntry.mPath, aArchive.mData)) {
      pErrorMessage = "could not read archive file: " + pFileSystem.FileName(aEntry.mPath);
      return false;
    }
    if (aArchive.mData.size() < peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH) {
      pErrorMessage = "archive file too short to contain header: " + pFileSystem.FileName(aEntry.mPath);
      return false;
    }

    std::memcpy(&aArchive.mArchiveHeader.mData, aArchive.mData.data(), sizeof(detail::ArchiveHeader));
    const std::size_t aPayloadAvailable = aArchive.mData.size() - peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
    const std::size_t aPayloadLength =
        std::min<std::size_t>(aArchive.mArchiveHeader.mData.mPayloadLength, aPayloadAvailable);

    for (std::size_t aPageStart = 0; aPageStart < aPayloadLength; aPageStart += peanutbutter::SB_L3_LENGTH) {
      const std::size_t aPageLength = std::min(peanutbutter::SB_L3_LENGTH, aPayloadLength - aPageStart);
      TestBlockL3 aL3;
      aL3.mPageIndex = aPageStart / peanutbutter::SB_L3_LENGTH;
      const std::size_t aPageFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aPageStart;
      aL3.mData.assign(aArchive.mData.begin() + static_cast<std::ptrdiff_t>(aPageFileOffset),
                       aArchive.mData.begin() + static_cast<std::ptrdiff_t>(aPageFileOffset + aPageLength));

      for (std::size_t aBlockStart = 0; aBlockStart + peanutbutter::SB_L1_LENGTH <= aPageLength;
           aBlockStart += peanutbutter::SB_L1_LENGTH) {
        TestBlockL1 aL1;
        aL1.mBlockIndex = (aPageStart + aBlockStart) / peanutbutter::SB_L1_LENGTH;
        aL1.mData.assign(aL3.mData.begin() + static_cast<std::ptrdiff_t>(aBlockStart),
                         aL3.mData.begin() + static_cast<std::ptrdiff_t>(aBlockStart + peanutbutter::SB_L1_LENGTH));
        if (aL1.mData.size() >= peanutbutter::SB_RECOVERY_HEADER_LENGTH) {
          std::memcpy(&aL1.mRecoveryHeader.mData, aL1.mData.data(), sizeof(detail::RecoveryHeader));
        }
        aL3.mL1Blocks.push_back(std::move(aL1));
      }

      aArchive.mL3Blocks.push_back(std::move(aL3));
    }

    pArchives.push_back(std::move(aArchive));
  }

  std::sort(pArchives.begin(), pArchives.end(), [](const TestArchive& pLeft, const TestArchive& pRight) {
    if (pLeft.mArchiveHeader.mData.mArchiveIndex != pRight.mArchiveHeader.mData.mArchiveIndex) {
      return pLeft.mArchiveHeader.mData.mArchiveIndex < pRight.mArchiveHeader.mData.mArchiveIndex;
    }
    return pLeft.mFilePath < pRight.mFilePath;
  });

  return true;
}

}  // namespace peanutbutter::testing
