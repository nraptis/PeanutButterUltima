#include "MockFileSystem.hpp"

#include <memory>
#include <utility>

namespace peanutbutter::testing {

namespace {

class MockFileReadStream final : public FileReadStream {
 public:
  MockFileReadStream(const MockHardDrive& pDrive, std::string pPath)
      : mDrive(pDrive),
        mPath(pDrive.Normalize(std::move(pPath))),
        mReady(pDrive.HasFile(mPath)),
        mLength(mReady ? pDrive.GetFileLength(mPath) : 0) {}

  bool IsReady() const override {
    return mReady;
  }

  std::size_t GetLength() const override {
    return mLength;
  }

  bool Read(std::size_t pOffset, unsigned char* pDestination, std::size_t pLength) const override {
    if (!mReady) {
      return false;
    }
    return mDrive.ReadFileBytes(mPath, pOffset, pDestination, pLength);
  }

 private:
  const MockHardDrive& mDrive;
  std::string mPath;
  bool mReady = false;
  std::size_t mLength = 0;
};

class MockFileWriteStream final : public FileWriteStream {
 public:
  MockFileWriteStream(MockHardDrive& pDrive, std::string pPath)
      : mDrive(pDrive),
        mPath(pDrive.Normalize(std::move(pPath))),
        mReady(mDrive.ClearFileBytes(mPath)) {}

  bool IsReady() const override {
    return mReady;
  }

  bool Write(const unsigned char* pData, std::size_t pLength) override {
    if (!mReady || mClosed || pData == nullptr) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }
    if (!mDrive.AppendFileBytes(mPath, pData, pLength)) {
      return false;
    }
    mBytesWritten += pLength;
    return true;
  }

  std::size_t GetBytesWritten() const override {
    return mBytesWritten;
  }

  bool Close() override {
    mClosed = true;
    return true;
  }

 private:
  MockHardDrive& mDrive;
  std::string mPath;
  bool mReady = false;
  bool mClosed = false;
  std::size_t mBytesWritten = 0;
};

}  // namespace

MockFileSystem::MockFileSystem()
    : mCurrentWorkingDirectory("/") {}

MockFileSystem::MockFileSystem(std::string pCurrentWorkingDirectory)
    : mCurrentWorkingDirectory(mDrive.Normalize(std::move(pCurrentWorkingDirectory))) {}

std::string MockFileSystem::CurrentWorkingDirectory() const {
  return mCurrentWorkingDirectory;
}

bool MockFileSystem::Exists(const std::string& pPath) const {
  return mDrive.HasPath(pPath);
}

bool MockFileSystem::IsDirectory(const std::string& pPath) const {
  return mDrive.HasDirectory(pPath);
}

bool MockFileSystem::IsFile(const std::string& pPath) const {
  return mDrive.HasFile(pPath);
}

bool MockFileSystem::EnsureDirectory(const std::string& pPath) {
  return mDrive.EnsureDirectory(pPath);
}

bool MockFileSystem::ClearDirectory(const std::string& pPath) {
  return mDrive.ClearDirectory(pPath);
}

bool MockFileSystem::DirectoryHasEntries(const std::string& pPath) const {
  return mDrive.DirectoryHasEntries(pPath);
}

std::vector<DirectoryEntry> MockFileSystem::ListFilesRecursive(const std::string& pRootPath) const {
  std::vector<DirectoryEntry> aEntries;
  for (const std::string& aPath : mDrive.ListFilesRecursive(pRootPath)) {
    aEntries.push_back({aPath, RelativeToRoot(pRootPath, aPath), false});
  }
  return aEntries;
}

std::vector<DirectoryEntry> MockFileSystem::ListDirectoriesRecursive(const std::string& pRootPath) const {
  std::vector<DirectoryEntry> aEntries;
  for (const std::string& aPath : mDrive.ListDirectoriesRecursive(pRootPath)) {
    aEntries.push_back({aPath, RelativeToRoot(pRootPath, aPath), true});
  }
  return aEntries;
}

std::vector<DirectoryEntry> MockFileSystem::ListFiles(const std::string& pRootPath) const {
  std::vector<DirectoryEntry> aEntries;
  for (const std::string& aPath : mDrive.ListFiles(pRootPath)) {
    aEntries.push_back({aPath, FileName(aPath), false});
  }
  return aEntries;
}

std::unique_ptr<FileReadStream> MockFileSystem::OpenReadStream(const std::string& pPath) const {
  return std::make_unique<MockFileReadStream>(mDrive, pPath);
}

std::unique_ptr<FileWriteStream> MockFileSystem::OpenWriteStream(const std::string& pPath) {
  return std::make_unique<MockFileWriteStream>(mDrive, pPath);
}

bool MockFileSystem::AppendFile(const std::string& pPath,
                                const unsigned char* pContents,
                                std::size_t pLength) {
  return mDrive.AppendFileBytes(pPath, pContents, pLength);
}

std::string MockFileSystem::JoinPath(const std::string& pLeft, const std::string& pRight) const {
  return mDrive.JoinPath(pLeft, pRight);
}

std::string MockFileSystem::ParentPath(const std::string& pPath) const {
  return mDrive.ParentPath(pPath);
}

std::string MockFileSystem::FileName(const std::string& pPath) const {
  return mDrive.FileName(pPath);
}

std::string MockFileSystem::StemName(const std::string& pPath) const {
  return mDrive.StemName(pPath);
}

std::string MockFileSystem::Extension(const std::string& pPath) const {
  return mDrive.Extension(pPath);
}

std::string MockFileSystem::RelativeToRoot(const std::string& pRootPath, const std::string& pPath) const {
  const std::string aRoot = mDrive.Normalize(pRootPath);
  const std::string aPath = mDrive.Normalize(pPath);
  if (aRoot == "/") {
    return aPath == "/" ? std::string() : aPath.substr(1);
  }
  if (aPath == aRoot) {
    return {};
  }
  if (aPath.size() > aRoot.size() + 1 &&
      aPath.compare(0, aRoot.size(), aRoot) == 0 &&
      aPath[aRoot.size()] == '/') {
    return aPath.substr(aRoot.size() + 1);
  }
  return FileName(aPath);
}

}  // namespace peanutbutter::testing
