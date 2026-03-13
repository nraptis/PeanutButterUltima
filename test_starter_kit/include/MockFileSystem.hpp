#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_FILE_SYSTEM_HPP_

#include <memory>
#include <string>
#include <vector>

#include "IO/FileSystem.hpp"
#include "MockHardDrive.hpp"

namespace peanutbutter {
namespace testkit {

class MockFileSystem final : public FileSystem {
 public:
  MockFileSystem();
  explicit MockFileSystem(MockHardDrivePtr pDrive);

  MockHardDrive& Drive();
  const MockHardDrive& Drive() const;

  void SetCurrentWorkingDirectory(const std::string& pPath);

  std::string CurrentWorkingDirectory() const override;
  bool Exists(const std::string& pPath) const override;
  bool IsDirectory(const std::string& pPath) const override;
  bool IsFile(const std::string& pPath) const override;
  bool EnsureDirectory(const std::string& pPath) override;
  bool ClearDirectory(const std::string& pPath) override;
  bool DirectoryHasEntries(const std::string& pPath) const override;

  std::vector<DirectoryEntry> ListFilesRecursive(
      const std::string& pRootPath,
      const std::function<bool(std::size_t)>& pProgressCallback = {}) const override;

  std::vector<DirectoryEntry> ListDirectoriesRecursive(
      const std::string& pRootPath,
      const std::function<bool(std::size_t)>& pProgressCallback = {}) const override;

  std::vector<DirectoryEntry> ListFiles(const std::string& pRootPath) const override;

  std::unique_ptr<FileReadStream> OpenReadStream(const std::string& pPath) const override;
  std::unique_ptr<FileWriteStream> OpenWriteStream(const std::string& pPath) override;

  bool AppendFile(const std::string& pPath,
                  const unsigned char* pContents,
                  std::size_t pLength) override;

  std::string JoinPath(const std::string& pLeft,
                       const std::string& pRight) const override;
  std::string ParentPath(const std::string& pPath) const override;
  std::string FileName(const std::string& pPath) const override;
  std::string StemName(const std::string& pPath) const override;
  std::string Extension(const std::string& pPath) const override;

 private:
  std::string Normalize(const std::string& pPath) const;
  std::string ComputeRelativePath(const std::string& pRoot,
                                  const std::string& pPath) const;

 private:
  MockHardDrivePtr mDrive;
  std::string mCurrentWorkingDirectory;
};

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_FILE_SYSTEM_HPP_
