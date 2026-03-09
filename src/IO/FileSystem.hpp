#ifndef PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_

#include <memory>
#include <string>
#include <vector>

#include "IO/FileReadStream.hpp"
#include "IO/FileWriteStream.hpp"

namespace peanutbutter {

using ByteVector = std::vector<unsigned char>;

struct DirectoryEntry {
  std::string mPath;
  std::string mRelativePath;
  bool mIsDirectory = false;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual bool Exists(const std::string& pPath) const = 0;
  virtual bool IsDirectory(const std::string& pPath) const = 0;
  virtual bool IsFile(const std::string& pPath) const = 0;
  virtual bool EnsureDirectory(const std::string& pPath) = 0;
  virtual bool ClearDirectory(const std::string& pPath) = 0;
  virtual bool DirectoryHasEntries(const std::string& pPath) const = 0;
  virtual std::vector<DirectoryEntry> ListFilesRecursive(const std::string& pRootPath) const = 0;
  virtual std::vector<DirectoryEntry> ListDirectoriesRecursive(const std::string& pRootPath) const = 0;
  virtual std::vector<DirectoryEntry> ListFiles(const std::string& pRootPath) const = 0;
  virtual std::unique_ptr<FileReadStream> OpenReadStream(const std::string& pPath) const = 0;
  virtual std::unique_ptr<FileWriteStream> OpenWriteStream(const std::string& pPath) = 0;
  virtual std::string JoinPath(const std::string& pLeft, const std::string& pRight) const = 0;
  virtual std::string ParentPath(const std::string& pPath) const = 0;
  virtual std::string FileName(const std::string& pPath) const = 0;
  virtual std::string StemName(const std::string& pPath) const = 0;
  bool ReadFile(const std::string& pPath, ByteVector& pContents) const;
  bool WriteFile(const std::string& pPath, const ByteVector& pContents);
  bool WriteFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength);
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_IO_FILE_SYSTEM_HPP_
