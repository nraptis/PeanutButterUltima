#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_HARD_DRIVE_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_HARD_DRIVE_HPP_

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace peanutbutter {
namespace testkit {

class MockHardDrive {
 public:
  MockHardDrive();

  std::string NormalizePath(const std::string& pPath) const;

  bool Exists(const std::string& pPath) const;
  bool IsDirectory(const std::string& pPath) const;
  bool IsFile(const std::string& pPath) const;

  bool EnsureDirectory(const std::string& pPath);
  bool ClearDirectory(const std::string& pPath);
  bool DirectoryHasEntries(const std::string& pPath) const;

  bool DeletePath(const std::string& pPath);
  bool TruncateFile(const std::string& pPath, std::size_t pLength);

  bool SetFileBytes(const std::string& pPath,
                    const unsigned char* pData,
                    std::size_t pLength);
  bool AppendFileBytes(const std::string& pPath,
                       const unsigned char* pData,
                       std::size_t pLength);
  bool ReadFileBytes(const std::string& pPath,
                     std::size_t pOffset,
                     unsigned char* pDestination,
                     std::size_t pLength) const;

  std::size_t GetFileLength(const std::string& pPath) const;
  bool GetFileBytes(const std::string& pPath,
                    std::vector<unsigned char>& pOutBytes) const;

  std::vector<std::string> ListFilePaths(const std::string& pRootPath,
                                         bool pRecursive) const;
  std::vector<std::string> ListDirectoryPaths(const std::string& pRootPath,
                                              bool pRecursive) const;

  void AddReadFault(const std::string& pPath);
  void AddWriteFault(const std::string& pPath);
  void ClearFaults();
  bool HasReadFault(const std::string& pPath) const;
  bool HasWriteFault(const std::string& pPath) const;

 private:
  bool IsPathUnder(const std::string& pPath,
                   const std::string& pRootPath) const;
  bool IsDirectChildOf(const std::string& pPath,
                       const std::string& pRootPath) const;
  std::string ParentPathOf(const std::string& pPath) const;
  bool HasReadFaultUnlocked(const std::string& pNormalizedPath) const;
  bool HasWriteFaultUnlocked(const std::string& pNormalizedPath) const;
  bool EnsureDirectoryLocked(const std::string& pNormalizedDirectoryPath);

 private:
  mutable std::mutex mMutex;
  std::unordered_map<std::string, std::vector<unsigned char>> mFiles;
  std::unordered_set<std::string> mDirectories;
  std::unordered_set<std::string> mReadFaultPaths;
  std::unordered_set<std::string> mWriteFaultPaths;
};

using MockHardDrivePtr = std::shared_ptr<MockHardDrive>;

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_MOCK_HARD_DRIVE_HPP_
