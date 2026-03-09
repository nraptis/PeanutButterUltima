#include "MockFileSystem.hpp"

#include <memory>

namespace peanutbutter::testing {

namespace {

class MockFileReadStream final : public peanutbutter::FileReadStream {
 public:
  MockFileReadStream(const MockHardDrive& pHardDrive, std::string pPath)
      : mHardDrive(&pHardDrive),
        mPath(std::move(pPath)) {}

  bool IsReady() const override {
    return mHardDrive != nullptr && mHardDrive->HasFile(mPath);
  }

  std::size_t GetLength() const override {
    return mHardDrive != nullptr ? mHardDrive->GetFileLength(mPath) : 0;
  }

  bool Read(std::size_t pOffset, unsigned char* pDestination, std::size_t pLength) const override {
    return mHardDrive != nullptr && mHardDrive->ReadFileBytes(mPath, pOffset, pDestination, pLength);
  }

 private:
  const MockHardDrive* mHardDrive = nullptr;
  std::string mPath;
};

class MockFileWriteStream final : public peanutbutter::FileWriteStream {
 public:
  MockFileWriteStream(MockHardDrive& pHardDrive, std::string pPath)
      : mHardDrive(&pHardDrive),
        mPath(std::move(pPath)),
        mReady(mHardDrive->ClearFileBytes(mPath)) {}

  bool IsReady() const override {
    return mReady;
  }

  bool Write(const unsigned char* pData, std::size_t pLength) override {
    if (!mReady || mHardDrive == nullptr) {
      return false;
    }
    if (!mHardDrive->AppendFileBytes(mPath, pData, pLength)) {
      return false;
    }
    mBytesWritten += pLength;
    return true;
  }

  std::size_t GetBytesWritten() const override {
    return mBytesWritten;
  }

  bool Close() override {
    return mReady;
  }

 private:
  MockHardDrive* mHardDrive = nullptr;
  std::string mPath;
  std::size_t mBytesWritten = 0;
  bool mReady = false;
};

}  // namespace

MockFileSystem::MockFileSystem() = default;

bool MockFileSystem::Exists(const std::string& pPath) const {
  return mHardDrive.HasPath(pPath);
}

bool MockFileSystem::IsDirectory(const std::string& pPath) const {
  return mHardDrive.HasDirectory(pPath);
}

bool MockFileSystem::IsFile(const std::string& pPath) const {
  return mHardDrive.HasFile(pPath);
}

bool MockFileSystem::EnsureDirectory(const std::string& pPath) {
  return mHardDrive.EnsureDirectory(pPath);
}

bool MockFileSystem::ClearDirectory(const std::string& pPath) {
  return mHardDrive.ClearDirectory(pPath);
}

bool MockFileSystem::DirectoryHasEntries(const std::string& pPath) const {
  return mHardDrive.DirectoryHasEntries(pPath);
}

std::vector<peanutbutter::DirectoryEntry> MockFileSystem::ListFilesRecursive(const std::string& pRootPath) const {
  std::vector<peanutbutter::DirectoryEntry> aEntries;
  const std::string aRoot = mHardDrive.Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const std::string& aPath : mHardDrive.ListFilesRecursive(pRootPath)) {
    aEntries.push_back({aPath, aPath.substr(aPrefix.size()), false});
  }
  return aEntries;
}

std::vector<peanutbutter::DirectoryEntry> MockFileSystem::ListDirectoriesRecursive(const std::string& pRootPath) const {
  std::vector<peanutbutter::DirectoryEntry> aEntries;
  const std::string aRoot = mHardDrive.Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const std::string& aPath : mHardDrive.ListDirectoriesRecursive(pRootPath)) {
    aEntries.push_back({aPath, aPath.substr(aPrefix.size()), true});
  }
  return aEntries;
}

std::vector<peanutbutter::DirectoryEntry> MockFileSystem::ListFiles(const std::string& pRootPath) const {
  std::vector<peanutbutter::DirectoryEntry> aEntries;
  const std::string aRoot = mHardDrive.Normalize(pRootPath);
  const std::string aPrefix = aRoot == "/" ? "/" : aRoot + "/";
  for (const std::string& aPath : mHardDrive.ListFiles(pRootPath)) {
    aEntries.push_back({aPath, aPath.substr(aPrefix.size()), false});
  }
  return aEntries;
}

std::unique_ptr<peanutbutter::FileReadStream> MockFileSystem::OpenReadStream(const std::string& pPath) const {
  return std::make_unique<MockFileReadStream>(mHardDrive, pPath);
}

std::unique_ptr<peanutbutter::FileWriteStream> MockFileSystem::OpenWriteStream(const std::string& pPath) {
  return std::make_unique<MockFileWriteStream>(mHardDrive, pPath);
}

std::string MockFileSystem::JoinPath(const std::string& pLeft, const std::string& pRight) const {
  return mHardDrive.JoinPath(pLeft, pRight);
}

std::string MockFileSystem::ParentPath(const std::string& pPath) const {
  return mHardDrive.ParentPath(pPath);
}

std::string MockFileSystem::FileName(const std::string& pPath) const {
  return mHardDrive.FileName(pPath);
}

std::string MockFileSystem::StemName(const std::string& pPath) const {
  return mHardDrive.StemName(pPath);
}

void MockFileSystem::AddFile(const std::string& pPath, const peanutbutter::ByteVector& pContents) {
  mHardDrive.PutFile(pPath, pContents);
}

bool MockFileSystem::ReadTextFile(const std::string& pPath, std::string& pContents) const {
  peanutbutter::ByteVector aBytes;
  if (!ReadFile(pPath, aBytes)) {
    return false;
  }
  pContents.assign(aBytes.begin(), aBytes.end());
  return true;
}

}  // namespace peanutbutter::testing
