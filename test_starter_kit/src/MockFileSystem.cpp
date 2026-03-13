#include "MockFileSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <utility>

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

std::string ParentPathFromNormalized(const std::string& pPath) {
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

class MockFileReadStream final : public FileReadStream {
 public:
  MockFileReadStream(MockHardDrivePtr pDrive, std::string pPath)
      : mDrive(std::move(pDrive)),
        mPath(std::move(pPath)) {
    if (mDrive == nullptr || !mDrive->IsFile(mPath) || mDrive->HasReadFault(mPath)) {
      return;
    }
    mLength = mDrive->GetFileLength(mPath);
    mReady = true;
  }

  bool IsReady() const override {
    return mReady;
  }

  std::size_t GetLength() const override {
    return mLength;
  }

  bool Read(std::size_t pOffset,
            unsigned char* pDestination,
            std::size_t pLength) const override {
    if (!mReady || mDrive == nullptr) {
      return false;
    }
    return mDrive->ReadFileBytes(mPath, pOffset, pDestination, pLength);
  }

 private:
  MockHardDrivePtr mDrive;
  std::string mPath;
  std::size_t mLength = 0u;
  bool mReady = false;
};

class MockFileWriteStream final : public FileWriteStream {
 public:
  MockFileWriteStream(MockHardDrivePtr pDrive, std::string pPath)
      : mDrive(std::move(pDrive)),
        mPath(std::move(pPath)) {
    if (mDrive == nullptr || mPath.empty() || mPath == "/" || mDrive->HasWriteFault(mPath)) {
      return;
    }
    const std::string aParent = ParentPathFromNormalized(mPath);
    if (!mDrive->EnsureDirectory(aParent)) {
      return;
    }
    mReady = true;
  }

  bool IsReady() const override {
    return mReady;
  }

  bool Write(const unsigned char* pData, std::size_t pLength) override {
    if (!mReady || mClosed) {
      return false;
    }
    if (pLength > 0u && pData == nullptr) {
      return false;
    }
    if (pLength > 0u) {
      mBuffer.insert(mBuffer.end(), pData, pData + pLength);
    }
    return true;
  }

  std::size_t GetBytesWritten() const override {
    return mBuffer.size();
  }

  bool Close() override {
    if (mClosed) {
      return true;
    }
    mClosed = true;
    if (!mReady || mDrive == nullptr) {
      return false;
    }
    return mDrive->SetFileBytes(mPath, mBuffer.data(), mBuffer.size());
  }

 private:
  MockHardDrivePtr mDrive;
  std::string mPath;
  std::vector<unsigned char> mBuffer;
  bool mReady = false;
  bool mClosed = false;
};

}  // namespace

MockFileSystem::MockFileSystem()
    : mDrive(std::make_shared<MockHardDrive>()),
      mCurrentWorkingDirectory("/") {
  (void)mDrive->EnsureDirectory(mCurrentWorkingDirectory);
}

MockFileSystem::MockFileSystem(MockHardDrivePtr pDrive)
    : mDrive(std::move(pDrive)),
      mCurrentWorkingDirectory("/") {
  if (mDrive == nullptr) {
    mDrive = std::make_shared<MockHardDrive>();
  }
  (void)mDrive->EnsureDirectory(mCurrentWorkingDirectory);
}

MockHardDrive& MockFileSystem::Drive() {
  return *mDrive;
}

const MockHardDrive& MockFileSystem::Drive() const {
  return *mDrive;
}

void MockFileSystem::SetCurrentWorkingDirectory(const std::string& pPath) {
  const std::string aPath = Normalize(pPath);
  if (mDrive->EnsureDirectory(aPath)) {
    mCurrentWorkingDirectory = aPath.empty() ? std::string("/") : aPath;
  }
}

std::string MockFileSystem::CurrentWorkingDirectory() const {
  return mCurrentWorkingDirectory;
}

bool MockFileSystem::Exists(const std::string& pPath) const {
  return mDrive->Exists(Normalize(pPath));
}

bool MockFileSystem::IsDirectory(const std::string& pPath) const {
  return mDrive->IsDirectory(Normalize(pPath));
}

bool MockFileSystem::IsFile(const std::string& pPath) const {
  return mDrive->IsFile(Normalize(pPath));
}

bool MockFileSystem::EnsureDirectory(const std::string& pPath) {
  return mDrive->EnsureDirectory(Normalize(pPath));
}

bool MockFileSystem::ClearDirectory(const std::string& pPath) {
  return mDrive->ClearDirectory(Normalize(pPath));
}

bool MockFileSystem::DirectoryHasEntries(const std::string& pPath) const {
  return mDrive->DirectoryHasEntries(Normalize(pPath));
}

std::vector<DirectoryEntry> MockFileSystem::ListFilesRecursive(
    const std::string& pRootPath,
    const std::function<bool(std::size_t)>& pProgressCallback) const {
  std::vector<DirectoryEntry> aOut;
  const std::string aRoot = Normalize(pRootPath);
  const std::vector<std::string> aPaths = mDrive->ListFilePaths(aRoot, true);
  aOut.reserve(aPaths.size());
  for (const std::string& aPath : aPaths) {
    DirectoryEntry aEntry;
    aEntry.mPath = aPath;
    aEntry.mRelativePath = ComputeRelativePath(aRoot, aPath);
    aEntry.mIsDirectory = false;
    aOut.push_back(aEntry);
    if (pProgressCallback && !pProgressCallback(aOut.size())) {
      break;
    }
  }
  return aOut;
}

std::vector<DirectoryEntry> MockFileSystem::ListDirectoriesRecursive(
    const std::string& pRootPath,
    const std::function<bool(std::size_t)>& pProgressCallback) const {
  std::vector<DirectoryEntry> aOut;
  const std::string aRoot = Normalize(pRootPath);
  const std::vector<std::string> aPaths = mDrive->ListDirectoryPaths(aRoot, true);
  aOut.reserve(aPaths.size());
  for (const std::string& aPath : aPaths) {
    DirectoryEntry aEntry;
    aEntry.mPath = aPath;
    aEntry.mRelativePath = ComputeRelativePath(aRoot, aPath);
    aEntry.mIsDirectory = true;
    aOut.push_back(aEntry);
    if (pProgressCallback && !pProgressCallback(aOut.size())) {
      break;
    }
  }
  return aOut;
}

std::vector<DirectoryEntry> MockFileSystem::ListFiles(const std::string& pRootPath) const {
  std::vector<DirectoryEntry> aOut;
  const std::string aRoot = Normalize(pRootPath);
  const std::vector<std::string> aPaths = mDrive->ListFilePaths(aRoot, false);
  aOut.reserve(aPaths.size());
  for (const std::string& aPath : aPaths) {
    DirectoryEntry aEntry;
    aEntry.mPath = aPath;
    aEntry.mRelativePath = FileName(aPath);
    aEntry.mIsDirectory = false;
    aOut.push_back(aEntry);
  }
  return aOut;
}

std::unique_ptr<FileReadStream> MockFileSystem::OpenReadStream(const std::string& pPath) const {
  return std::make_unique<MockFileReadStream>(mDrive, Normalize(pPath));
}

std::unique_ptr<FileWriteStream> MockFileSystem::OpenWriteStream(const std::string& pPath) {
  return std::make_unique<MockFileWriteStream>(mDrive, Normalize(pPath));
}

bool MockFileSystem::AppendFile(const std::string& pPath,
                                const unsigned char* pContents,
                                std::size_t pLength) {
  return mDrive->AppendFileBytes(Normalize(pPath), pContents, pLength);
}

std::string MockFileSystem::JoinPath(const std::string& pLeft,
                                     const std::string& pRight) const {
  if (pLeft.empty()) {
    return mDrive->NormalizePath(pRight);
  }
  if (pRight.empty()) {
    return mDrive->NormalizePath(pLeft);
  }
  if (IsAbsolutePath(pRight)) {
    return mDrive->NormalizePath(pRight);
  }

  std::string aJoined = pLeft;
  if (!aJoined.empty() && aJoined.back() != '/' && aJoined.back() != '\\') {
    aJoined.push_back('/');
  }
  aJoined += pRight;
  return mDrive->NormalizePath(aJoined);
}

std::string MockFileSystem::ParentPath(const std::string& pPath) const {
  return ParentPathFromNormalized(Normalize(pPath));
}

std::string MockFileSystem::FileName(const std::string& pPath) const {
  const std::string aPath = Normalize(pPath);
  if (aPath.empty() || aPath == "/") {
    return std::string();
  }
  const std::size_t aSlash = aPath.find_last_of('/');
  if (aSlash == std::string::npos) {
    return aPath;
  }
  if (aSlash + 1u >= aPath.size()) {
    return std::string();
  }
  return aPath.substr(aSlash + 1u);
}

std::string MockFileSystem::StemName(const std::string& pPath) const {
  std::string aFileName = FileName(pPath);
  if (aFileName.empty()) {
    return std::string("archive");
  }
  const std::size_t aDot = aFileName.find_last_of('.');
  if (aDot == std::string::npos || aDot == 0u) {
    return aFileName;
  }
  return aFileName.substr(0u, aDot);
}

std::string MockFileSystem::Extension(const std::string& pPath) const {
  const std::string aFileName = FileName(pPath);
  const std::size_t aDot = aFileName.find_last_of('.');
  if (aDot == std::string::npos || aDot == 0u) {
    return std::string();
  }
  return aFileName.substr(aDot);
}

std::string MockFileSystem::Normalize(const std::string& pPath) const {
  if (pPath.empty()) {
    return pPath;
  }
  if (IsAbsolutePath(pPath)) {
    return mDrive->NormalizePath(pPath);
  }
  std::string aJoined = mCurrentWorkingDirectory;
  if (!aJoined.empty() && aJoined.back() != '/' && aJoined.back() != '\\') {
    aJoined.push_back('/');
  }
  aJoined += pPath;
  return mDrive->NormalizePath(aJoined);
}

std::string MockFileSystem::ComputeRelativePath(const std::string& pRoot,
                                                const std::string& pPath) const {
  const std::string aRoot = mDrive->NormalizePath(pRoot);
  const std::string aPath = mDrive->NormalizePath(pPath);

  if (aRoot.empty()) {
    return aPath;
  }
  if (aRoot == "/") {
    if (!aPath.empty() && aPath[0] == '/') {
      return aPath.substr(1u);
    }
    return aPath;
  }
  if (aPath == aRoot) {
    return std::string();
  }
  const std::string aPrefix = aRoot + "/";
  if (aPath.size() > aPrefix.size() && aPath.compare(0u, aPrefix.size(), aPrefix) == 0) {
    return aPath.substr(aPrefix.size());
  }
  return aPath;
}

}  // namespace testkit
}  // namespace peanutbutter
