#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_WRAPPERS_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_WRAPPERS_HPP_

#include <string>
#include <vector>

#include "AppCore_Helpers.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter::testing {

struct TestFile {
  std::string mFilePath;
  ByteVector mData;

  std::string DataString() const;
};

struct TestArchiveHeader {
  detail::ArchiveHeader mData{};
};

struct TestRecoveryHeader {
  detail::RecoveryHeader mData{};
};

struct TestBlockL1 {
  std::size_t mBlockIndex = 0;
  ByteVector mData;
  TestRecoveryHeader mRecoveryHeader;
};

struct TestBlockL3 {
  std::size_t mPageIndex = 0;
  ByteVector mData;
  std::vector<TestBlockL1> mL1Blocks;
};

struct TestArchive {
  std::string mFilePath;
  ByteVector mData;
  TestArchiveHeader mArchiveHeader;
  std::vector<TestBlockL3> mL3Blocks;

  std::string DataString() const;
};

bool CollectTestFilesRecursive(FileSystem& pFileSystem,
                               const std::string& pDirectory,
                               std::vector<TestFile>& pFiles,
                               std::string& pErrorMessage);

bool CollectTestArchives(FileSystem& pFileSystem,
                         const std::string& pArchiveDirectory,
                         std::vector<TestArchive>& pArchives,
                         std::string& pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_WRAPPERS_HPP_
