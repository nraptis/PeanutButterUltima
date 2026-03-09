#ifndef PEANUT_BUTTER_ULTIMA_TESTING_MOCKS_MOCK_FILE_SYSTEM_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTING_MOCKS_MOCK_FILE_SYSTEM_HPP_

#include "IO/FileSystem.hpp"
#include "MockHardDrive.hpp"

namespace peanutbutter::testing {

class MockFileSystem final : public peanutbutter::FileSystem {
 public:
  MockFileSystem();

  bool Exists(const std::string& pPath) const override;
  bool IsDirectory(const std::string& pPath) const override;
  bool IsFile(const std::string& pPath) const override;
  bool EnsureDirectory(const std::string& pPath) override;
  bool ClearDirectory(const std::string& pPath) override;
  bool DirectoryHasEntries(const std::string& pPath) const override;
  std::vector<peanutbutter::DirectoryEntry> ListFilesRecursive(const std::string& pRootPath) const override;
  std::vector<peanutbutter::DirectoryEntry> ListDirectoriesRecursive(const std::string& pRootPath) const override;
  std::vector<peanutbutter::DirectoryEntry> ListFiles(const std::string& pRootPath) const override;
  std::unique_ptr<peanutbutter::FileReadStream> OpenReadStream(const std::string& pPath) const override;
  std::unique_ptr<peanutbutter::FileWriteStream> OpenWriteStream(const std::string& pPath) override;
  std::string JoinPath(const std::string& pLeft, const std::string& pRight) const override;
  std::string ParentPath(const std::string& pPath) const override;
  std::string FileName(const std::string& pPath) const override;
  std::string StemName(const std::string& pPath) const override;

  void AddFile(const std::string& pPath, const peanutbutter::ByteVector& pContents);
  bool ReadTextFile(const std::string& pPath, std::string& pContents) const;

 private:
  MockHardDrive mHardDrive;
};

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTING_MOCKS_MOCK_FILE_SYSTEM_HPP_
