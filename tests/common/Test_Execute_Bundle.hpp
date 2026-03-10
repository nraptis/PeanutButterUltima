#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_BUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_BUNDLE_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter::testing {

struct TestSeedFile {
  std::string mRelativePath;
  std::string mContents;
};

struct BundleExecutionSpec {
  std::string mRuntimeRoot;
  std::string mInputDirectoryName = "input";
  std::string mArchiveDirectoryName = "archives";
  std::string mArchivePrefix = "tdd_";
  std::string mArchiveSuffix = "pb";
  std::size_t mArchiveBlockCount = 1;
  bool mUseEncryption = false;
  std::vector<std::string> mSeedEmptyDirectories;
};

struct BundleExecutionResult {
  bool mSucceeded = false;
  std::string mMessage;
  std::string mInputDirectory;
  std::string mArchiveDirectory;
  std::vector<std::string> mArchivePaths;
};

struct BundlePayloadMutation {
  std::size_t mArchiveOffset = 0;
  std::size_t mPayloadLogicalOffset = 0;
  std::vector<unsigned char> mBytes;
};

BundleExecutionResult ExecuteBundleForSeed(FileSystem& pFileSystem,
                                           ApplicationCore& pCore,
                                           const std::vector<TestSeedFile>& pSeedFiles,
                                           const BundleExecutionSpec& pSpec);

BundleExecutionResult ExecuteBundleWithMutations(FileSystem& pFileSystem,
                                                 ApplicationCore& pCore,
                                                 const std::vector<TestSeedFile>& pSeedFiles,
                                                 const BundleExecutionSpec& pSpec,
                                                 const std::vector<BundlePayloadMutation>& pMutations);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_BUNDLE_HPP_
