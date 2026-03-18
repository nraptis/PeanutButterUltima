#ifndef PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "IO/FileReadStream.hpp"
#include "IO/FileWriteStream.hpp"

namespace peanutbutter {

class ByteBuffer {
 public:
  ByteBuffer() = default;
  explicit ByteBuffer(std::size_t pLength) {
    Resize(pLength);
  }

  bool Resize(std::size_t pLength) {
    if (pLength == 0) {
      mStorage.reset();
      mLength = 0;
      return true;
    }
    std::unique_ptr<unsigned char[]> aStorage(new (std::nothrow) unsigned char[pLength]);
    if (!aStorage) {
      return false;
    }
    mStorage = std::move(aStorage);
    mLength = pLength;
    return true;
  }

  void Clear() {
    mStorage.reset();
    mLength = 0;
  }

  unsigned char* Data() {
    return mStorage.get();
  }

  const unsigned char* Data() const {
    return mStorage.get();
  }

  std::size_t Size() const {
    return mLength;
  }

  bool Empty() const {
    return mLength == 0;
  }

 private:
  std::unique_ptr<unsigned char[]> mStorage;
  std::size_t mLength = 0;
};

struct DirectoryEntry {
  std::string mPath;
  std::string mRelativePath;
  bool mIsDirectory = false;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual std::string CurrentWorkingDirectory() const = 0;
  virtual bool Exists(const std::string& pPath) const = 0;
  virtual bool IsDirectory(const std::string& pPath) const = 0;
  virtual bool IsFile(const std::string& pPath) const = 0;
  virtual bool EnsureDirectory(const std::string& pPath) = 0;
  virtual bool ClearDirectory(const std::string& pPath) = 0;
  virtual bool DirectoryHasEntries(const std::string& pPath) const = 0;
  virtual std::vector<DirectoryEntry> ListFilesRecursive(
      const std::string& pRootPath,
      const std::function<bool(std::size_t)>& pProgressCallback = {}) const = 0;
  virtual std::vector<DirectoryEntry> ListDirectoriesRecursive(
      const std::string& pRootPath,
      const std::function<bool(std::size_t)>& pProgressCallback = {}) const = 0;
  virtual std::vector<DirectoryEntry> ListFiles(const std::string& pRootPath) const = 0;
  virtual std::unique_ptr<FileReadStream> OpenReadStream(const std::string& pPath) const = 0;
  virtual std::unique_ptr<FileWriteStream> OpenWriteStream(const std::string& pPath) = 0;
  virtual bool AppendFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength) = 0;
  virtual bool OverwriteFileRegion(const std::string& pPath,
                                   std::size_t pOffset,
                                   const unsigned char* pContents,
                                   std::size_t pLength) = 0;
  virtual std::string JoinPath(const std::string& pLeft, const std::string& pRight) const = 0;
  virtual std::string ParentPath(const std::string& pPath) const = 0;
  virtual std::string FileName(const std::string& pPath) const = 0;
  virtual std::string StemName(const std::string& pPath) const = 0;
  virtual std::string Extension(const std::string& pPath) const = 0;
  bool ReadFile(const std::string& pPath, ByteBuffer& pContents) const;
  bool ReadTextFile(const std::string& pPath, std::string& pContents) const;
  bool WriteFile(const std::string& pPath, const ByteBuffer& pContents);
  bool WriteFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength);
  bool WriteTextFile(const std::string& pPath, const std::string& pContents);
  bool AppendTextFile(const std::string& pPath, const std::string& pContents);
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_
