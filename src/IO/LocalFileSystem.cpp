#include "IO/LocalFileSystem.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace peanutbutter {

namespace {

std::string JoinLocalPath(const std::string& pLeft, const std::string& pRight) {
  if (pLeft.empty()) {
    return pRight;
  }
  if (pRight.empty()) {
    return pLeft;
  }
  return (std::filesystem::path(pLeft) / std::filesystem::path(pRight)).lexically_normal().generic_string();
}

std::string ParentLocalPath(const std::string& pPath) {
  return std::filesystem::path(pPath).parent_path().lexically_normal().generic_string();
}

std::string LocalFileName(const std::string& pPath) {
  return std::filesystem::path(pPath).filename().generic_string();
}

std::string LocalStemName(const std::string& pPath) {
  std::filesystem::path aPath(pPath);
  std::filesystem::path aStem = aPath.filename();
  if (aStem.empty()) {
    aStem = aPath.stem();
  }
  return aStem.empty() ? "archive" : aStem.generic_string();
}

std::string LocalExtension(const std::string& pPath) {
  return std::filesystem::path(pPath).extension().generic_string();
}

std::string FormatErrnoMessage(int pErrnoValue) {
  if (pErrnoValue == 0) {
    return {};
  }
  const char* aText = std::strerror(pErrnoValue);
  if (aText == nullptr || aText[0] == '\0') {
    return "errno " + std::to_string(pErrnoValue);
  }
  return std::string(aText) + " (errno " + std::to_string(pErrnoValue) + ")";
}

class LocalFileReadStream final : public FileReadStream {
 public:
  explicit LocalFileReadStream(std::string pPath)
      : mPath(std::move(pPath)) {
    const std::filesystem::path aPath(mPath);
    std::error_code aError;
    const std::uintmax_t aRawLength = std::filesystem::file_size(aPath, aError);
    if (aError) {
      return;
    }
    mInput.open(aPath, std::ios::binary);
    if (!mInput.is_open()) {
      return;
    }
    mLength = static_cast<std::size_t>(aRawLength);
    mReady = true;
  }

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
    if (pOffset > mLength || pLength > mLength - pOffset) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }
    if (pDestination == nullptr) {
      return false;
    }
    if (!mInput.is_open()) {
      return false;
    }
    if (!mCursorValid || mCursor != pOffset) {
      mInput.clear();
      mInput.seekg(static_cast<std::streamoff>(pOffset), std::ios::beg);
      if (!mInput.good()) {
        mCursorValid = false;
        return false;
      }
    }

    mInput.read(reinterpret_cast<char*>(pDestination), static_cast<std::streamsize>(pLength));
    if (mInput.gcount() != static_cast<std::streamsize>(pLength)) {
      mCursorValid = false;
      return false;
    }
    mCursor = pOffset + pLength;
    mCursorValid = true;
    return true;
  }

 private:
  std::string mPath;
  mutable std::ifstream mInput;
  mutable std::size_t mCursor = 0;
  mutable bool mCursorValid = false;
  std::size_t mLength = 0;
  bool mReady = false;
};

class LocalFileWriteStream final : public FileWriteStream {
 public:
  explicit LocalFileWriteStream(std::string pPath)
      : mPath(std::move(pPath)) {
    errno = 0;
    mOutput = std::fopen(mPath.c_str(), "wb");
    if (mOutput == nullptr) {
      mLastErrorMessage = FormatErrnoMessage(errno);
    }
  }

  LocalFileWriteStream(std::string pPath, std::string pInitialError)
      : mPath(std::move(pPath)),
        mLastErrorMessage(std::move(pInitialError)) {}

  bool IsReady() const override {
    return mOutput != nullptr;
  }

  bool Write(const unsigned char* pData, std::size_t pLength) override {
    if (mOutput == nullptr) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }
    if (pData == nullptr) {
      mLastErrorMessage = "null write buffer";
      return false;
    }
    errno = 0;
    const std::size_t aWritten = std::fwrite(pData, 1u, pLength, mOutput);
    if (aWritten != pLength) {
      mLastErrorMessage = FormatErrnoMessage(errno);
      if (mLastErrorMessage.empty()) {
        mLastErrorMessage = "short write";
      }
      return false;
    }
    mBytesWritten += pLength;
    mLastErrorMessage.clear();
    return true;
  }

  std::size_t GetBytesWritten() const override {
    return mBytesWritten;
  }

  bool Close() override {
    if (mOutput == nullptr) {
      return true;
    }
    errno = 0;
    if (std::fclose(mOutput) != 0) {
      mOutput = nullptr;
      mLastErrorMessage = FormatErrnoMessage(errno);
      if (mLastErrorMessage.empty()) {
        mLastErrorMessage = "close failed";
      }
      return false;
    }
    mOutput = nullptr;
    mLastErrorMessage.clear();
    return true;
  }

  std::string LastErrorMessage() const override {
    return mLastErrorMessage;
  }

 private:
  std::string mPath;
  std::FILE* mOutput = nullptr;
  std::size_t mBytesWritten = 0;
  std::string mLastErrorMessage;
};

}  // namespace

std::string LocalFileSystem::CurrentWorkingDirectory() const {
  return std::filesystem::current_path().lexically_normal().generic_string();
}

bool LocalFileSystem::Exists(const std::string& pPath) const {
  return std::filesystem::exists(std::filesystem::path(pPath));
}

bool LocalFileSystem::IsDirectory(const std::string& pPath) const {
  return std::filesystem::is_directory(std::filesystem::path(pPath));
}

bool LocalFileSystem::IsFile(const std::string& pPath) const {
  return std::filesystem::is_regular_file(std::filesystem::path(pPath));
}

bool LocalFileSystem::EnsureDirectory(const std::string& pPath) {
  std::error_code aError;
  std::filesystem::create_directories(std::filesystem::path(pPath), aError);
  return !aError && std::filesystem::is_directory(std::filesystem::path(pPath));
}

bool LocalFileSystem::ClearDirectory(const std::string& pPath) {
  std::error_code aError;
  std::filesystem::remove_all(std::filesystem::path(pPath), aError);
  if (aError) {
    return false;
  }
  return EnsureDirectory(pPath);
}

bool LocalFileSystem::DirectoryHasEntries(const std::string& pPath) const {
  if (!std::filesystem::is_directory(std::filesystem::path(pPath))) {
    return false;
  }
  return std::filesystem::directory_iterator(std::filesystem::path(pPath)) != std::filesystem::directory_iterator();
}

std::vector<DirectoryEntry> LocalFileSystem::ListFilesRecursive(
    const std::string& pRootPath,
    const std::function<bool(std::size_t)>& pProgressCallback) const {
  std::vector<DirectoryEntry> aEntries;
  const std::filesystem::path aRoot(pRootPath);
  if (!std::filesystem::is_directory(aRoot)) {
    return aEntries;
  }
  for (const auto& aEntry : std::filesystem::recursive_directory_iterator(
           aRoot,
           std::filesystem::directory_options::skip_permission_denied)) {
    if (!aEntry.is_regular_file()) {
      continue;
    }
    aEntries.push_back(
        {aEntry.path().lexically_normal().generic_string(), std::filesystem::relative(aEntry.path(), aRoot).generic_string(), false});
    if (pProgressCallback && !pProgressCallback(aEntries.size())) {
      break;
    }
  }
  return aEntries;
}

std::vector<DirectoryEntry> LocalFileSystem::ListDirectoriesRecursive(
    const std::string& pRootPath,
    const std::function<bool(std::size_t)>& pProgressCallback) const {
  std::vector<DirectoryEntry> aEntries;
  const std::filesystem::path aRoot(pRootPath);
  if (!std::filesystem::is_directory(aRoot)) {
    return aEntries;
  }
  for (const auto& aEntry : std::filesystem::recursive_directory_iterator(
           aRoot,
           std::filesystem::directory_options::skip_permission_denied)) {
    if (!aEntry.is_directory()) {
      continue;
    }
    aEntries.push_back(
        {aEntry.path().lexically_normal().generic_string(), std::filesystem::relative(aEntry.path(), aRoot).generic_string(), true});
    if (pProgressCallback && !pProgressCallback(aEntries.size())) {
      break;
    }
  }
  return aEntries;
}

std::vector<DirectoryEntry> LocalFileSystem::ListFiles(const std::string& pRootPath) const {
  std::vector<DirectoryEntry> aEntries;
  const std::filesystem::path aRoot(pRootPath);
  if (!std::filesystem::is_directory(aRoot)) {
    return aEntries;
  }
  for (const auto& aEntry : std::filesystem::directory_iterator(aRoot)) {
    if (!aEntry.is_regular_file()) {
      continue;
    }
    aEntries.push_back({aEntry.path().lexically_normal().generic_string(), aEntry.path().filename().generic_string(), false});
  }
  return aEntries;
}

std::unique_ptr<FileReadStream> LocalFileSystem::OpenReadStream(const std::string& pPath) const {
  return std::make_unique<LocalFileReadStream>(pPath);
}

std::unique_ptr<FileWriteStream> LocalFileSystem::OpenWriteStream(const std::string& pPath) {
  const std::string aParent = ParentLocalPath(pPath);
  if (!aParent.empty() && !EnsureDirectory(aParent)) {
    return std::make_unique<LocalFileWriteStream>(
        pPath, "failed creating parent directory for write stream");
  }
  return std::make_unique<LocalFileWriteStream>(pPath);
}

bool LocalFileSystem::AppendFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength) {
  const std::string aParent = ParentLocalPath(pPath);
  if (!aParent.empty() && !EnsureDirectory(aParent)) {
    return false;
  }

  std::ofstream aOutput(std::filesystem::path(pPath), std::ios::binary | std::ios::app);
  if (!aOutput.is_open()) {
    return false;
  }
  if (pLength == 0) {
    return true;
  }
  if (pContents == nullptr) {
    return false;
  }
  aOutput.write(reinterpret_cast<const char*>(pContents), static_cast<std::streamsize>(pLength));
  return aOutput.good();
}

std::string LocalFileSystem::JoinPath(const std::string& pLeft, const std::string& pRight) const {
  return JoinLocalPath(pLeft, pRight);
}

std::string LocalFileSystem::ParentPath(const std::string& pPath) const {
  return ParentLocalPath(pPath);
}

std::string LocalFileSystem::FileName(const std::string& pPath) const {
  return LocalFileName(pPath);
}

std::string LocalFileSystem::StemName(const std::string& pPath) const {
  return LocalStemName(pPath);
}

std::string LocalFileSystem::Extension(const std::string& pPath) const {
  return LocalExtension(pPath);
}

}  // namespace peanutbutter
