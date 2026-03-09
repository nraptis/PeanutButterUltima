#include "Validate_Manifest.hpp"

#include <algorithm>
#include <vector>

#include "Test_Utils.hpp"

namespace peanutbutter::testing {

namespace {

std::vector<std::string> CollectEmptyDirectories(const peanutbutter::FileSystem& pFileSystem,
                                                 const std::string& pRootDirectory) {
  std::vector<std::string> aEmptyDirectories;
  std::vector<peanutbutter::DirectoryEntry> aDirectories = pFileSystem.ListDirectoriesRecursive(pRootDirectory);
  for (const peanutbutter::DirectoryEntry& aDirectory : aDirectories) {
    if (aDirectory.mRelativePath.empty() || pFileSystem.DirectoryHasEntries(aDirectory.mPath)) {
      continue;
    }
    aEmptyDirectories.push_back(aDirectory.mRelativePath);
  }
  std::sort(aEmptyDirectories.begin(), aEmptyDirectories.end());
  return aEmptyDirectories;
}

}  // namespace

bool Validate_Manifest(const peanutbutter::FileSystem& pFileSystem,
                       const std::string& pInputDirectory,
                       const std::string& pOutputDirectory,
                       std::string* pErrorMessage) {
  const std::vector<std::string> aInputEmptyDirectories = CollectEmptyDirectories(pFileSystem, pInputDirectory);
  const std::vector<std::string> aOutputEmptyDirectories = CollectEmptyDirectories(pFileSystem, pOutputDirectory);
  if (aInputEmptyDirectories != aOutputEmptyDirectories) {
    return Fail("Validate_Manifest failed: empty-directory manifest mismatch.", pErrorMessage);
  }
  return true;
}

}  // namespace peanutbutter::testing
