#include "Validate_File.hpp"

#include <algorithm>

#include "Test_Utils.hpp"

namespace peanutbutter::ultima::testing {

bool CollectFiles(const peanutbutter::ultima::FileSystem& pFileSystem,
                  const std::string& pRootDirectory,
                  std::vector<TestFile>& pFiles,
                  std::string* pErrorMessage) {
  pFiles.clear();
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFilesRecursive(pRootDirectory);
  std::sort(aEntries.begin(), aEntries.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    ByteVector aBytes;
    if (!pFileSystem.ReadFile(aEntry.mPath, aBytes)) {
      return Fail("CollectFiles failed: could not read file '" + aEntry.mPath + "'.", pErrorMessage);
    }
    pFiles.emplace_back(aEntry.mRelativePath, std::move(aBytes));
  }

  return true;
}

bool Validate_File(const TestFile& pExpectedFile,
                   const TestFile& pActualFile,
                   std::string* pErrorMessage) {
  return pExpectedFile.Equals(pActualFile, pErrorMessage);
}

bool Validate_Files(const std::vector<TestFile>& pExpectedFiles,
                    const std::vector<TestFile>& pActualFiles,
                    std::string* pErrorMessage) {
  if (pExpectedFiles.size() != pActualFiles.size()) {
    return Fail("Validate_Files failed: file count mismatch expected=" + std::to_string(pExpectedFiles.size()) +
                    " actual=" + std::to_string(pActualFiles.size()),
                pErrorMessage);
  }

  for (std::size_t aIndex = 0; aIndex < pExpectedFiles.size(); ++aIndex) {
    std::string aError;
    if (!Validate_File(pExpectedFiles[aIndex], pActualFiles[aIndex], &aError)) {
      return Fail("Validate_Files failed at index " + std::to_string(aIndex) + ": " + aError, pErrorMessage);
    }
  }

  return true;
}

}  // namespace peanutbutter::ultima::testing
