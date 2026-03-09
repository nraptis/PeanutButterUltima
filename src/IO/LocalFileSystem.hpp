#ifndef PEANUT_BUTTER_ULTIMA_IO_LOCAL_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_LOCAL_FILE_SYSTEM_HPP_

#include "IO/FileSystem.hpp"

namespace peanutbutter::ultima {

class LocalFileSystem final : public FileSystem {
 public:
  bool Exists(const std::string& pPath) const override;
  bool IsDirectory(const std::string& pPath) const override;
  bool IsFile(const std::string& pPath) const override;
  bool EnsureDirectory(const std::string& pPath) override;
  bool ClearDirectory(const std::string& pPath) override;
  bool DirectoryHasEntries(const std::string& pPath) const override;
  std::vector<DirectoryEntry> ListFilesRecursive(const std::string& pRootPath) const override;
  std::vector<DirectoryEntry> ListFiles(const std::string& pRootPath) const override;
  std::unique_ptr<FileReadStream> OpenReadStream(const std::string& pPath) const override;
  std::unique_ptr<FileWriteStream> OpenWriteStream(const std::string& pPath) override;
  std::string JoinPath(const std::string& pLeft, const std::string& pRight) const override;
  std::string ParentPath(const std::string& pPath) const override;
  std::string FileName(const std::string& pPath) const override;
  std::string StemName(const std::string& pPath) const override;
};

}  // namespace peanutbutter::ultima

#endif  // PEANUT_BUTTER_ULTIMA_IO_LOCAL_FILE_SYSTEM_HPP_
