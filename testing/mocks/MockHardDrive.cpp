#include "MockHardDrive.hpp"

#include <algorithm>
#include <vector>

namespace peanutbutter::testing {

MockHardDrive::MockHardDrive() {
  mDirectories.insert("/");
}

std::string MockHardDrive::Normalize(const std::string& pPath) const {
  if (pPath.empty()) {
    return "/";
  }

  std::string aPath = pPath;
  for (char& aChar : aPath) {
    if (aChar == '\\') {
      aChar = '/';
    }
  }

  const bool aAbsolute = !aPath.empty() && aPath.front() == '/';
  std::vector<std::string> aParts;
  std::string aCurrent;
  for (std::size_t aIndex = 0; aIndex <= aPath.size(); ++aIndex) {
    const char aChar = aIndex < aPath.size() ? aPath[aIndex] : '/';
    if (aChar == '/') {
      if (aCurrent.empty() || aCurrent == ".") {
        aCurrent.clear();
        continue;
      }
      if (aCurrent == "..") {
        if (!aParts.empty()) {
          aParts.pop_back();
        }
        aCurrent.clear();
        continue;
      }
      aParts.push_back(aCurrent);
      aCurrent.clear();
      continue;
    }
    aCurrent.push_back(aChar);
  }

  std::string aNormalized = aAbsolute ? "/" : "";
  for (std::size_t aIndex = 0; aIndex < aParts.size(); ++aIndex) {
    if (aIndex > 0 || (!aAbsolute && aIndex == 0)) {
      aNormalized += "/";
    }
    aNormalized += aParts[aIndex];
  }
  return aNormalized.empty() ? "/" : aNormalized;
}

std::string MockHardDrive::JoinPath(const std::string& pLeft, const std::string& pRight) const {
  if (pLeft.empty()) {
    return Normalize(pRight);
  }
  if (pRight.empty()) {
    return Normalize(pLeft);
  }
  return Normalize(pLeft + "/" + pRight);
}

std::string MockHardDrive::ParentPath(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (aPath == "/") {
    return "/";
  }
  const std::size_t aSlash = aPath.find_last_of('/');
  if (aSlash == std::string::npos || aSlash == 0) {
    return "/";
  }
  return aPath.substr(0, aSlash);
}

std::string MockHardDrive::FileName(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (aPath == "/") {
    return "";
  }
  const std::size_t aSlash = aPath.find_last_of('/');
  if (aSlash == std::string::npos) {
    return aPath;
  }
  return aPath.substr(aSlash + 1);
}

std::string MockHardDrive::StemName(const std::string& pPath) const {
  const std::string aFileName = FileName(pPath);
  if (aFileName.empty()) {
    return "archive";
  }
  const std::size_t aDot = aFileName.find_last_of('.');
  if (aDot == std::string::npos || aDot == 0) {
    return aFileName;
  }
  return aFileName.substr(0, aDot);
}

void MockHardDrive::EnsureParents(const std::string& pPath) {
  const std::string aNormalized = Normalize(pPath);
  std::string aCurrent;
  std::size_t aOffset = 1;
  while (aOffset < aNormalized.size()) {
    const std::size_t aSlash = aNormalized.find('/', aOffset);
    if (aSlash == std::string::npos) {
      break;
    }
    aCurrent = aNormalized.substr(0, aSlash);
    if (aCurrent.empty()) {
      aCurrent = "/";
    }
    mDirectories.insert(aCurrent);
    aOffset = aSlash + 1;
  }
}

bool MockHardDrive::HasPath(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  return mDirectories.count(aPath) > 0 || mFiles.count(aPath) > 0;
}

bool MockHardDrive::HasDirectory(const std::string& pPath) const {
  return mDirectories.count(Normalize(pPath)) > 0;
}

bool MockHardDrive::HasFile(const std::string& pPath) const {
  return mFiles.count(Normalize(pPath)) > 0;
}

bool MockHardDrive::EnsureDirectory(const std::string& pPath) {
  EnsureParents(Normalize(pPath));
  mDirectories.insert(Normalize(pPath));
  return true;
}

bool MockHardDrive::ClearDirectory(const std::string& pPath) {
  const std::string aRoot = Normalize(pPath);
  EnsureDirectory(aRoot);
  for (auto aIt = mFiles.begin(); aIt != mFiles.end();) {
    if (aIt->first == aRoot || aIt->first.rfind(aRoot + "/", 0) == 0) {
      aIt = mFiles.erase(aIt);
    } else {
      ++aIt;
    }
  }
  for (auto aIt = mDirectories.begin(); aIt != mDirectories.end();) {
    if (*aIt != "/" && (*aIt == aRoot || aIt->rfind(aRoot + "/", 0) == 0)) {
      aIt = mDirectories.erase(aIt);
    } else {
      ++aIt;
    }
  }
  mDirectories.insert(aRoot);
  return true;
}

bool MockHardDrive::DirectoryHasEntries(const std::string& pPath) const {
  const std::string aRoot = Normalize(pPath);
  for (const auto& [aPath, aBytes] : mFiles) {
    (void)aBytes;
    if (aPath.rfind(aRoot + "/", 0) == 0) {
      return true;
    }
  }
  for (const std::string& aDirectory : mDirectories) {
    if (aDirectory != aRoot && aDirectory.rfind(aRoot + "/", 0) == 0) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> MockHardDrive::ListFilesRecursive(const std::string& pRootPath) const {
  std::vector<std::string> aPaths;
  const std::string aRoot = Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const auto& [aPath, aBytes] : mFiles) {
    (void)aBytes;
    if (aPath.rfind(aPrefix, 0) == 0) {
      aPaths.push_back(aPath);
    }
  }
  return aPaths;
}

std::vector<std::string> MockHardDrive::ListDirectoriesRecursive(const std::string& pRootPath) const {
  std::vector<std::string> aPaths;
  const std::string aRoot = Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const std::string& aPath : mDirectories) {
    if (aPath == aRoot || aPath == "/") {
      continue;
    }
    if (aPath.rfind(aPrefix, 0) == 0) {
      aPaths.push_back(aPath);
    }
  }
  return aPaths;
}

std::vector<std::string> MockHardDrive::ListFiles(const std::string& pRootPath) const {
  std::vector<std::string> aPaths;
  const std::string aRoot = Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const auto& [aPath, aBytes] : mFiles) {
    (void)aBytes;
    if (aPath.rfind(aPrefix, 0) != 0) {
      continue;
    }
    const std::string aRemainder = aPath.substr(aPrefix.size());
    if (aRemainder.find('/') == std::string::npos) {
      aPaths.push_back(aPath);
    }
  }
  return aPaths;
}

std::size_t MockHardDrive::GetFileLength(const std::string& pPath) const {
  const auto aIt = mFiles.find(Normalize(pPath));
  return aIt == mFiles.end() ? 0 : aIt->second.size();
}

bool MockHardDrive::ReadFileBytes(const std::string& pPath,
                                  std::size_t pOffset,
                                  unsigned char* pDestination,
                                  std::size_t pLength) const {
  const auto aIt = mFiles.find(Normalize(pPath));
  if (aIt == mFiles.end() || pDestination == nullptr) {
    return false;
  }
  const std::vector<unsigned char>& aBytes = aIt->second;
  if (pOffset > aBytes.size() || pLength > aBytes.size() - pOffset) {
    return false;
  }
  if (pLength == 0) {
    return true;
  }
  std::copy_n(aBytes.data() + pOffset, pLength, pDestination);
  return true;
}

bool MockHardDrive::ClearFileBytes(const std::string& pPath) {
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  mFiles[aPath].clear();
  return true;
}

bool MockHardDrive::AppendFileBytes(const std::string& pPath, const unsigned char* pData, std::size_t pLength) {
  if (pData == nullptr) {
    return false;
  }
  if (pLength == 0) {
    return true;
  }
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  std::vector<unsigned char>& aBytes = mFiles[aPath];
  aBytes.insert(aBytes.end(), pData, pData + pLength);
  return true;
}

void MockHardDrive::PutFile(const std::string& pPath, const std::vector<unsigned char>& pContents) {
  const std::string aPath = Normalize(pPath);
  EnsureParents(aPath);
  mFiles[aPath] = pContents;
}

}  // namespace peanutbutter::testing
