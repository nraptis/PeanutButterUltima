#include "MockHardDrive.hpp"

#include <algorithm>

namespace peanutbutter::testing {

namespace {

std::vector<std::string> SplitPathTokens(const std::string& pPath) {
  std::vector<std::string> aTokens;
  std::string aToken;
  for (char aCharacter : pPath) {
    const char aNormalized = (aCharacter == '\\') ? '/' : aCharacter;
    if (aNormalized == '/') {
      if (!aToken.empty()) {
        aTokens.push_back(aToken);
        aToken.clear();
      }
      continue;
    }
    aToken.push_back(aNormalized);
  }
  if (!aToken.empty()) {
    aTokens.push_back(aToken);
  }
  return aTokens;
}

bool IsPathInSubtree(const std::string& pRootPath,
                     const std::string& pPath,
                     bool pIncludeRoot) {
  if (pRootPath == "/") {
    return pIncludeRoot ? !pPath.empty() && pPath[0] == '/' : pPath != "/";
  }
  if (pPath == pRootPath) {
    return pIncludeRoot;
  }
  if (pPath.size() <= pRootPath.size()) {
    return false;
  }
  return pPath.compare(0, pRootPath.size(), pRootPath) == 0 &&
         pPath[pRootPath.size()] == '/';
}

}  // namespace

MockHardDrive::MockHardDrive() {
  mDirectories.insert("/");
}

std::string MockHardDrive::Normalize(const std::string& pPath) const {
  if (pPath.empty()) {
    return "/";
  }
  std::vector<std::string> aStack;
  for (const std::string& aToken : SplitPathTokens(pPath)) {
    if (aToken.empty() || aToken == ".") {
      continue;
    }
    if (aToken == "..") {
      if (!aStack.empty()) {
        aStack.pop_back();
      }
      continue;
    }
    aStack.push_back(aToken);
  }

  if (aStack.empty()) {
    return "/";
  }

  std::string aNormalized = "/";
  for (std::size_t aIndex = 0; aIndex < aStack.size(); ++aIndex) {
    if (aIndex > 0) {
      aNormalized.push_back('/');
    }
    aNormalized += aStack[aIndex];
  }
  return aNormalized;
}

std::string MockHardDrive::JoinPath(const std::string& pLeft,
                                    const std::string& pRight) const {
  if (pRight.empty()) {
    return Normalize(pLeft);
  }
  if (pRight[0] == '/' || pRight[0] == '\\') {
    return Normalize(pRight);
  }
  const std::string aLeft = pLeft.empty() ? "/" : Normalize(pLeft);
  return Normalize(aLeft + "/" + pRight);
}

std::string MockHardDrive::ParentPath(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (aPath == "/") {
    return "/";
  }
  const std::size_t aLastSlash = aPath.find_last_of('/');
  if (aLastSlash == 0) {
    return "/";
  }
  return aPath.substr(0, aLastSlash);
}

std::string MockHardDrive::FileName(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (aPath == "/") {
    return {};
  }
  const std::size_t aLastSlash = aPath.find_last_of('/');
  if (aLastSlash == std::string::npos) {
    return aPath;
  }
  return aPath.substr(aLastSlash + 1);
}

std::string MockHardDrive::StemName(const std::string& pPath) const {
  const std::string aName = FileName(pPath);
  if (aName.empty()) {
    return "archive";
  }
  const std::size_t aLastDot = aName.find_last_of('.');
  if (aLastDot == std::string::npos || aLastDot == 0) {
    return aName;
  }
  const std::string aStem = aName.substr(0, aLastDot);
  return aStem.empty() ? "archive" : aStem;
}

std::string MockHardDrive::Extension(const std::string& pPath) const {
  const std::string aName = FileName(pPath);
  const std::size_t aLastDot = aName.find_last_of('.');
  if (aLastDot == std::string::npos || aLastDot == 0) {
    return {};
  }
  return aName.substr(aLastDot);
}

bool MockHardDrive::HasPath(const std::string& pPath) const {
  return HasDirectory(pPath) || HasFile(pPath);
}

bool MockHardDrive::HasDirectory(const std::string& pPath) const {
  return mDirectories.find(Normalize(pPath)) != mDirectories.end();
}

bool MockHardDrive::HasFile(const std::string& pPath) const {
  return mFiles.find(Normalize(pPath)) != mFiles.end();
}

bool MockHardDrive::EnsureDirectory(const std::string& pPath) {
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  mDirectories.insert(aPath);
  return true;
}

bool MockHardDrive::ClearDirectory(const std::string& pPath) {
  const std::string aPath = Normalize(pPath);
  EnsureDirectory(aPath);

  if (aPath == "/") {
    mFiles.clear();
    mDirectories.clear();
    mDirectories.insert("/");
    return true;
  }

  for (auto aIterator = mFiles.begin(); aIterator != mFiles.end();) {
    if (IsPathInSubtree(aPath, aIterator->first, false)) {
      aIterator = mFiles.erase(aIterator);
      continue;
    }
    ++aIterator;
  }

  for (auto aIterator = mDirectories.begin(); aIterator != mDirectories.end();) {
    if (IsPathInSubtree(aPath, *aIterator, false)) {
      aIterator = mDirectories.erase(aIterator);
      continue;
    }
    ++aIterator;
  }

  return true;
}

bool MockHardDrive::DirectoryHasEntries(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (!HasDirectory(aPath)) {
    return false;
  }

  for (const std::string& aDirectory : mDirectories) {
    if (aDirectory == aPath) {
      continue;
    }
    if (ParentPath(aDirectory) == aPath) {
      return true;
    }
  }
  for (const auto& aFile : mFiles) {
    if (ParentPath(aFile.first) == aPath) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> MockHardDrive::ListFilesRecursive(const std::string& pRootPath) const {
  const std::string aRoot = Normalize(pRootPath);
  std::vector<std::string> aResult;
  if (!HasDirectory(aRoot)) {
    return aResult;
  }
  for (const auto& aFile : mFiles) {
    if (IsPathInSubtree(aRoot, aFile.first, false)) {
      aResult.push_back(aFile.first);
    }
  }
  std::sort(aResult.begin(), aResult.end());
  return aResult;
}

std::vector<std::string> MockHardDrive::ListDirectoriesRecursive(const std::string& pRootPath) const {
  const std::string aRoot = Normalize(pRootPath);
  std::vector<std::string> aResult;
  if (!HasDirectory(aRoot)) {
    return aResult;
  }
  for (const std::string& aDirectory : mDirectories) {
    if (IsPathInSubtree(aRoot, aDirectory, false)) {
      aResult.push_back(aDirectory);
    }
  }
  std::sort(aResult.begin(), aResult.end());
  return aResult;
}

std::vector<std::string> MockHardDrive::ListFiles(const std::string& pRootPath) const {
  const std::string aRoot = Normalize(pRootPath);
  std::vector<std::string> aResult;
  if (!HasDirectory(aRoot)) {
    return aResult;
  }
  for (const auto& aFile : mFiles) {
    if (ParentPath(aFile.first) == aRoot) {
      aResult.push_back(aFile.first);
    }
  }
  std::sort(aResult.begin(), aResult.end());
  return aResult;
}

std::size_t MockHardDrive::GetFileLength(const std::string& pPath) const {
  const auto aIterator = mFiles.find(Normalize(pPath));
  if (aIterator == mFiles.end()) {
    return 0;
  }
  return aIterator->second.size();
}

bool MockHardDrive::ReadFileBytes(const std::string& pPath,
                                  std::size_t pOffset,
                                  unsigned char* pDestination,
                                  std::size_t pLength) const {
  const auto aIterator = mFiles.find(Normalize(pPath));
  if (aIterator == mFiles.end()) {
    return false;
  }
  const std::vector<unsigned char>& aBytes = aIterator->second;
  if (pOffset > aBytes.size() || pLength > (aBytes.size() - pOffset)) {
    return false;
  }
  if (pLength == 0) {
    return true;
  }
  if (pDestination == nullptr) {
    return false;
  }
  std::copy(aBytes.begin() + static_cast<std::ptrdiff_t>(pOffset),
            aBytes.begin() + static_cast<std::ptrdiff_t>(pOffset + pLength),
            pDestination);
  return true;
}

bool MockHardDrive::ClearFileBytes(const std::string& pPath) {
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  mFiles[aPath].clear();
  return true;
}

bool MockHardDrive::AppendFileBytes(const std::string& pPath,
                                    const unsigned char* pData,
                                    std::size_t pLength) {
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  std::vector<unsigned char>& aBytes = mFiles[aPath];
  if (pLength == 0) {
    return true;
  }
  if (pData == nullptr) {
    return false;
  }
  aBytes.insert(aBytes.end(), pData, pData + pLength);
  return true;
}

void MockHardDrive::EnsureParents(const std::string& pPath) {
  std::string aParent = ParentPath(pPath);
  while (!aParent.empty()) {
    mDirectories.insert(aParent);
    if (aParent == "/") {
      break;
    }
    aParent = ParentPath(aParent);
  }
}

}  // namespace peanutbutter::testing
