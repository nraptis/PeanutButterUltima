#ifndef PEANUT_BUTTER_ULTIMA_TESTING_COMMON_TEST_INTEGER_FAILURE_CASE_COMMON_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTING_COMMON_TEST_INTEGER_FAILURE_CASE_COMMON_HPP_

#include <functional>
#include <string>

#include "AppCore.hpp"
#include "MockFileSystem.hpp"

namespace peanutbutter::ultima::testing {

using ArchiveMutator = std::function<bool(ByteVector&, std::string*)>;
using InputSeeder = std::function<void(MockFileSystem&)>;

bool RunUnbundleIntegerFailureCase(const std::string& pExpectedCode,
                                   const InputSeeder& pSeedInput,
                                   const ArchiveMutator& pMutator,
                                   std::string* pErrorMessage);
bool RunRecoverIntegerFailureCase(const std::string& pExpectedCode,
                                  const InputSeeder& pSeedInput,
                                  const ArchiveMutator& pMutator,
                                  std::string* pErrorMessage);

void SeedBasicIntegerFailureInputTree(MockFileSystem& pFileSystem);
void SeedMultiArchiveIntegerFailureInputTree(MockFileSystem& pFileSystem);

}  // namespace peanutbutter::ultima::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTING_COMMON_TEST_INTEGER_FAILURE_CASE_COMMON_HPP_
