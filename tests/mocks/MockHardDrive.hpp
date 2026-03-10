#ifndef PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_HARD_DRIVE_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_HARD_DRIVE_HPP_

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace peanutbutter::testing {

class MockHardDrive {
 public:
  MockHardDrive();

  std::string Normalize(const std::string& pPath) const;
  std::string JoinPath(const std::string& pLeft, const std::string& pRight) const;
  std::string ParentPath(const std::string& pPath) const;
  std::string FileName(const std::string& pPath) const;
  std::string StemName(const std::string& pPath) const;
  std::string Extension(const std::string& pPath) const;

  bool HasPath(const std::string& pPath) const;
  bool HasDirectory(const std::string& pPath) const;
  bool HasFile(const std::string& pPath) const;

  bool EnsureDirectory(const std::string& pPath);
  bool ClearDirectory(const std::string& pPath);
  bool DirectoryHasEntries(const std::string& pPath) const;
  std::vector<std::string> ListFilesRecursive(const std::string& pRootPath) const;
  std::vector<std::string> ListDirectoriesRecursive(const std::string& pRootPath) const;
  std::vector<std::string> ListFiles(const std::string& pRootPath) const;

  std::size_t GetFileLength(const std::string& pPath) const;
  bool ReadFileBytes(const std::string& pPath,
                     std::size_t pOffset,
                     unsigned char* pDestination,
                     std::size_t pLength) const;
  bool ClearFileBytes(const std::string& pPath);
  bool AppendFileBytes(const std::string& pPath, const unsigned char* pData, std::size_t pLength);

 private:
  void EnsureParents(const std::string& pPath);

 private:
  std::map<std::string, std::vector<unsigned char>> mFiles;
  std::set<std::string> mDirectories;
};

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_MOCKS_MOCK_HARD_DRIVE_HPP_
