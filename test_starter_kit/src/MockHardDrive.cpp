#include "MockHardDrive.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace peanutbutter {
namespace testkit {
namespace {

bool IsAbsolutePath(const std::string& pPath) {
  if (pPath.empty()) {
    return false;
  }
  if (pPath[0] == '/' || pPath[0] == '\\') {
    return true;
  }
  return (pPath.size() > 2u &&
          std::isalpha(static_cast<unsigned char>(pPath[0])) != 0 &&
          pPath[1] == ':' &&
          (pPath[2] == '/' || pPath[2] == '\\'));
}

std::vector<std::string> SplitPathSegments(const std::string& pPath) {
  std::vector<std::string> aSegments;
  std::size_t aIndex = 0u;
  while (aIndex < pPath.size()) {
    while (aIndex < pPath.size() && pPath[aIndex] == '/') {
      ++aIndex;
    }
    if (aIndex >= pPath.size()) {
      break;
    }
    const std::size_t aStart = aIndex;
    while (aIndex < pPath.size() && pPath[aIndex] != '/') {
      ++aIndex;
    }
    aSegments.push_back(pPath.substr(aStart, aIndex - aStart));
  }
  return aSegments;
}

}  // namespace

MockHardDrive::MockHardDrive() {
  mDirectories.insert("/");
  mDirectories.insert("");
}

std::string MockHardDrive::NormalizePath(const std::string& pPath) const {
  if (pPath.empty()) {
    return std::string();
  }

  std::string aWork = pPath;
  std::replace(aWork.begin(), aWork.end(), '\\', '/');

  const bool aAbsolute = IsAbsolutePath(aWork);
  std::vector<std::string> aSegments = SplitPathSegments(aWork);
  std::vector<std::string> aNormalized;
  aNormalized.reserve(aSegments.size());

  for (const std::string& aSegment : aSegments) {
    if (aSegment.empty() || aSegment == ".") {
      continue;
    }
    if (aSegment == "..") {
      if (!aNormalized.empty() && aNormalized.back() != "..") {
        aNormalized.pop_back();
      } else if (!aAbsolute) {
        aNormalized.push_back(aSegment);
      }
      continue;
    }
    aNormalized.push_back(aSegment);
  }

  std::string aOut = aAbsolute ? "/" : "";
  for (std::size_t aIndex = 0u; aIndex < aNormalized.size(); ++aIndex) {
    if (!aOut.empty() && aOut.back() != '/') {
      aOut.push_back('/');
    }
    aOut += aNormalized[aIndex];
  }

  if (aOut.empty()) {
    return aAbsolute ? std::string("/") : std::string();
  }
  return aOut;
}

bool MockHardDrive::Exists(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aNormalized = NormalizePath(pPath);
  return mFiles.find(aNormalized) != mFiles.end() ||
         mDirectories.find(aNormalized) != mDirectories.end();
}

bool MockHardDrive::IsDirectory(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aNormalized = NormalizePath(pPath);
  return mDirectories.find(aNormalized) != mDirectories.end();
}

bool MockHardDrive::IsFile(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aNormalized = NormalizePath(pPath);
  return mFiles.find(aNormalized) != mFiles.end();
}

bool MockHardDrive::EnsureDirectory(const std::string& pPath) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aNormalized = NormalizePath(pPath);
  if (HasWriteFaultUnlocked(aNormalized)) {
    return false;
  }
  return EnsureDirectoryLocked(aNormalized);
}

bool MockHardDrive::ClearDirectory(const std::string& pPath) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aRoot = NormalizePath(pPath);
  if (HasWriteFaultUnlocked(aRoot)) {
    return false;
  }
  if (mFiles.find(aRoot) != mFiles.end()) {
    return false;
  }
  if (!EnsureDirectoryLocked(aRoot)) {
    return false;
  }

  for (auto aIt = mFiles.begin(); aIt != mFiles.end();) {
    if (IsPathUnder(aIt->first, aRoot)) {
      aIt = mFiles.erase(aIt);
    } else {
      ++aIt;
    }
  }

  for (auto aIt = mDirectories.begin(); aIt != mDirectories.end();) {
    if (aIt->empty() || *aIt == "/" || *aIt == aRoot) {
      ++aIt;
      continue;
    }
    if (IsPathUnder(*aIt, aRoot)) {
      aIt = mDirectories.erase(aIt);
    } else {
      ++aIt;
    }
  }
  return true;
}

bool MockHardDrive::DirectoryHasEntries(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aRoot = NormalizePath(pPath);
  if (mDirectories.find(aRoot) == mDirectories.end()) {
    return false;
  }

  for (const auto& aFile : mFiles) {
    if (IsDirectChildOf(aFile.first, aRoot)) {
      return true;
    }
  }
  for (const std::string& aDirectory : mDirectories) {
    if (aDirectory.empty() || aDirectory == "/" || aDirectory == aRoot) {
      continue;
    }
    if (IsDirectChildOf(aDirectory, aRoot)) {
      return true;
    }
  }
  return false;
}

bool MockHardDrive::DeletePath(const std::string& pPath) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aTarget = NormalizePath(pPath);
  if (HasWriteFaultUnlocked(aTarget)) {
    return false;
  }

  auto aFileIt = mFiles.find(aTarget);
  if (aFileIt != mFiles.end()) {
    mFiles.erase(aFileIt);
    return true;
  }

  if (mDirectories.find(aTarget) == mDirectories.end()) {
    return false;
  }

  for (auto aIt = mFiles.begin(); aIt != mFiles.end();) {
    if (IsPathUnder(aIt->first, aTarget)) {
      aIt = mFiles.erase(aIt);
    } else {
      ++aIt;
    }
  }

  for (auto aIt = mDirectories.begin(); aIt != mDirectories.end();) {
    if (aIt->empty() || *aIt == "/") {
      ++aIt;
      continue;
    }
    if (*aIt == aTarget || IsPathUnder(*aIt, aTarget)) {
      aIt = mDirectories.erase(aIt);
    } else {
      ++aIt;
    }
  }

  if (aTarget == "/") {
    mDirectories.insert("/");
    mDirectories.insert("");
  }
  return true;
}

bool MockHardDrive::TruncateFile(const std::string& pPath, std::size_t pLength) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  if (HasWriteFaultUnlocked(aPath)) {
    return false;
  }
  auto aIt = mFiles.find(aPath);
  if (aIt == mFiles.end()) {
    return false;
  }
  aIt->second.resize(pLength, 0u);
  return true;
}

bool MockHardDrive::SetFileBytes(const std::string& pPath,
                                 const unsigned char* pData,
                                 std::size_t pLength) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  if (aPath.empty() || aPath == "/" || HasWriteFaultUnlocked(aPath)) {
    return false;
  }
  const std::string aParent = ParentPathOf(aPath);
  if (!EnsureDirectoryLocked(aParent)) {
    return false;
  }

  std::vector<unsigned char> aBytes;
  aBytes.resize(pLength, 0u);
  if (pLength > 0u) {
    if (pData == nullptr) {
      return false;
    }
    std::copy(pData, pData + pLength, aBytes.begin());
  }

  mFiles[aPath] = std::move(aBytes);
  mDirectories.erase(aPath);
  return true;
}

bool MockHardDrive::AppendFileBytes(const std::string& pPath,
                                    const unsigned char* pData,
                                    std::size_t pLength) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  if (aPath.empty() || aPath == "/" || HasWriteFaultUnlocked(aPath)) {
    return false;
  }
  const std::string aParent = ParentPathOf(aPath);
  if (!EnsureDirectoryLocked(aParent)) {
    return false;
  }
  if (pLength > 0u && pData == nullptr) {
    return false;
  }

  std::vector<unsigned char>& aBytes = mFiles[aPath];
  if (pLength > 0u) {
    aBytes.insert(aBytes.end(), pData, pData + pLength);
  }
  return true;
}

bool MockHardDrive::ReadFileBytes(const std::string& pPath,
                                  std::size_t pOffset,
                                  unsigned char* pDestination,
                                  std::size_t pLength) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  if (HasReadFaultUnlocked(aPath)) {
    return false;
  }

  const auto aIt = mFiles.find(aPath);
  if (aIt == mFiles.end()) {
    return false;
  }

  const std::vector<unsigned char>& aBytes = aIt->second;
  if (pOffset > aBytes.size() || pLength > aBytes.size() - pOffset) {
    return false;
  }
  if (pLength == 0u) {
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

std::size_t MockHardDrive::GetFileLength(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  const auto aIt = mFiles.find(aPath);
  if (aIt == mFiles.end()) {
    return 0u;
  }
  return aIt->second.size();
}

bool MockHardDrive::GetFileBytes(const std::string& pPath,
                                 std::vector<unsigned char>& pOutBytes) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  const auto aIt = mFiles.find(aPath);
  if (aIt == mFiles.end()) {
    pOutBytes.clear();
    return false;
  }
  pOutBytes = aIt->second;
  return true;
}

std::vector<std::string> MockHardDrive::ListFilePaths(const std::string& pRootPath,
                                                      bool pRecursive) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aRoot = NormalizePath(pRootPath);
  std::vector<std::string> aOut;
  for (const auto& aPair : mFiles) {
    const std::string& aPath = aPair.first;
    if (!IsPathUnder(aPath, aRoot)) {
      continue;
    }
    if (!pRecursive && !IsDirectChildOf(aPath, aRoot)) {
      continue;
    }
    aOut.push_back(aPath);
  }
  std::sort(aOut.begin(), aOut.end());
  return aOut;
}

std::vector<std::string> MockHardDrive::ListDirectoryPaths(const std::string& pRootPath,
                                                           bool pRecursive) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aRoot = NormalizePath(pRootPath);
  std::vector<std::string> aOut;
  for (const std::string& aPath : mDirectories) {
    if (aPath.empty() || aPath == "/" || aPath == aRoot) {
      continue;
    }
    if (!IsPathUnder(aPath, aRoot)) {
      continue;
    }
    if (!pRecursive && !IsDirectChildOf(aPath, aRoot)) {
      continue;
    }
    aOut.push_back(aPath);
  }
  std::sort(aOut.begin(), aOut.end());
  return aOut;
}

void MockHardDrive::AddReadFault(const std::string& pPath) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  mReadFaultPaths.insert(NormalizePath(pPath));
}

void MockHardDrive::AddWriteFault(const std::string& pPath) {
  const std::lock_guard<std::mutex> aLock(mMutex);
  mWriteFaultPaths.insert(NormalizePath(pPath));
}

void MockHardDrive::ClearFaults() {
  const std::lock_guard<std::mutex> aLock(mMutex);
  mReadFaultPaths.clear();
  mWriteFaultPaths.clear();
}

bool MockHardDrive::HasReadFault(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  return HasReadFaultUnlocked(aPath);
}

bool MockHardDrive::HasWriteFault(const std::string& pPath) const {
  const std::lock_guard<std::mutex> aLock(mMutex);
  const std::string aPath = NormalizePath(pPath);
  return HasWriteFaultUnlocked(aPath);
}

bool MockHardDrive::IsPathUnder(const std::string& pPath,
                                const std::string& pRootPath) const {
  if (pRootPath.empty()) {
    return !pPath.empty() && pPath[0] != '/';
  }
  if (pRootPath == "/") {
    return !pPath.empty() && pPath[0] == '/' && pPath != "/";
  }
  if (pPath.size() <= pRootPath.size()) {
    return false;
  }
  if (pPath.compare(0u, pRootPath.size(), pRootPath) != 0) {
    return false;
  }
  return pPath[pRootPath.size()] == '/';
}

bool MockHardDrive::IsDirectChildOf(const std::string& pPath,
                                    const std::string& pRootPath) const {
  if (!IsPathUnder(pPath, pRootPath)) {
    return false;
  }

  const std::size_t aStart = (pRootPath == "/") ? 1u : pRootPath.size() + 1u;
  return pPath.find('/', aStart) == std::string::npos;
}

std::string MockHardDrive::ParentPathOf(const std::string& pPath) const {
  if (pPath.empty()) {
    return std::string();
  }
  if (pPath == "/") {
    return std::string("/");
  }
  const std::size_t aSlash = pPath.find_last_of('/');
  if (aSlash == std::string::npos) {
    return std::string();
  }
  if (aSlash == 0u) {
    return std::string("/");
  }
  return pPath.substr(0u, aSlash);
}

bool MockHardDrive::HasReadFaultUnlocked(const std::string& pNormalizedPath) const {
  return mReadFaultPaths.find(pNormalizedPath) != mReadFaultPaths.end();
}

bool MockHardDrive::HasWriteFaultUnlocked(const std::string& pNormalizedPath) const {
  return mWriteFaultPaths.find(pNormalizedPath) != mWriteFaultPaths.end();
}

bool MockHardDrive::EnsureDirectoryLocked(const std::string& pNormalizedDirectoryPath) {
  if (pNormalizedDirectoryPath.empty()) {
    mDirectories.insert("");
    return true;
  }
  if (HasWriteFaultUnlocked(pNormalizedDirectoryPath)) {
    return false;
  }

  const std::string aPath = NormalizePath(pNormalizedDirectoryPath);
  if (mFiles.find(aPath) != mFiles.end()) {
    return false;
  }

  const bool aAbsolute = !aPath.empty() && aPath[0] == '/';
  std::vector<std::string> aSegments = SplitPathSegments(aPath);

  std::string aCurrent = aAbsolute ? std::string("/") : std::string();
  if (aAbsolute) {
    mDirectories.insert("/");
  }

  for (const std::string& aSegment : aSegments) {
    if (!aCurrent.empty() && aCurrent != "/") {
      aCurrent.push_back('/');
    }
    if (aCurrent == "/") {
      aCurrent += aSegment;
    } else {
      aCurrent += aSegment;
    }

    if (mFiles.find(aCurrent) != mFiles.end()) {
      return false;
    }
    if (HasWriteFaultUnlocked(aCurrent)) {
      return false;
    }
    mDirectories.insert(aCurrent);
  }

  if (aPath.empty()) {
    mDirectories.insert("");
  }
  return true;
}

}  // namespace testkit
}  // namespace peanutbutter
