#ifndef PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_FILE_SYSTEM_HPP_

#include <string>
#include <vector>

#include "IO/FileSystem.hpp"
#include "MockHardDrive.hpp"

namespace peanutbutter::testing {

class MockFileSystem final : public FileSystem {
 public:
  MockFileSystem();
  explicit MockFileSystem(std::string pCurrentWorkingDirectory);

  std::string CurrentWorkingDirectory() const override;
  bool Exists(const std::string& pPath) const override;
  bool IsDirectory(const std::string& pPath) const override;
  bool IsFile(const std::string& pPath) const override;
  bool EnsureDirectory(const std::string& pPath) override;
  bool ClearDirectory(const std::string& pPath) override;
  bool DirectoryHasEntries(const std::string& pPath) const override;
  std::vector<DirectoryEntry> ListFilesRecursive(const std::string& pRootPath) const override;
  std::vector<DirectoryEntry> ListDirectoriesRecursive(const std::string& pRootPath) const override;
  std::vector<DirectoryEntry> ListFiles(const std::string& pRootPath) const override;
  std::unique_ptr<FileReadStream> OpenReadStream(const std::string& pPath) const override;
  std::unique_ptr<FileWriteStream> OpenWriteStream(const std::string& pPath) override;
  bool AppendFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength) override;
  std::string JoinPath(const std::string& pLeft, const std::string& pRight) const override;
  std::string ParentPath(const std::string& pPath) const override;
  std::string FileName(const std::string& pPath) const override;
  std::string StemName(const std::string& pPath) const override;
  std::string Extension(const std::string& pPath) const override;

 private:
  std::string RelativeToRoot(const std::string& pRootPath, const std::string& pPath) const;

 private:
  mutable MockHardDrive mDrive;
  std::string mCurrentWorkingDirectory;
};

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_FILE_SYSTEM_HPP_
