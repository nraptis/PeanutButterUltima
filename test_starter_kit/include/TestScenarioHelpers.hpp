#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_TEST_SCENARIO_HELPERS_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_TEST_SCENARIO_HELPERS_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "AppShell_Types.hpp"
#include "DeterministicRng.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {
namespace testkit {

enum class RandomTestFileNameType {
  kInvalidSmall,
  kMinimum,
  kSmall,
  kMedium,
  kLarge,
  kInvalidLarge,
};

enum class RandomTestFileContentType {
  kZero,
  kSmall,
  kMedium,
  kLarge,
  kGiant,
};

struct TestFile {
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::vector<unsigned char> mContentBytes;
};

TestFile GetRandomTestFile(RandomTestFileNameType pNameType,
                           RandomTestFileContentType pContentType,
                           DeterministicRng& pRng,
                           std::size_t pPayloadBytesPerBlock);

TestFile MakeDirectoryTestFile(const std::string& pRelativePath);

bool MaterializeTestFiles(const std::vector<TestFile>& pFiles,
                          const std::string& pRootDirectory,
                          FileSystem& pFileSystem,
                          std::vector<SourceEntry>& pOutSourceEntries);

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_TEST_SCENARIO_HELPERS_HPP_
