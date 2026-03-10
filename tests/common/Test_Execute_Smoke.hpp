#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_SMOKE_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_SMOKE_HPP_

#include <string>
#include <vector>

#include "AppCore.hpp"
#include "IO/FileSystem.hpp"
#include "Test_Execute_Bundle.hpp"

namespace peanutbutter::testing {

bool CompareDirectoryToSeedFiles(FileSystem& pFileSystem,
                                 const std::string& pDirectory,
                                 const std::vector<TestSeedFile>& pExpectedFiles,
                                 std::string& pErrorMessage);

bool ExecuteBundleAndUnbundleSmoke(FileSystem& pFileSystem,
                                   ApplicationCore& pCore,
                                   const std::vector<TestSeedFile>& pSeedFiles,
                                   const BundleExecutionSpec& pBundleSpec,
                                   const std::string& pUnbundleDirectory,
                                   std::string& pErrorMessage);

bool ExecuteRecoverSmoke(FileSystem& pFileSystem,
                         ApplicationCore& pCore,
                         const std::string& pArchiveDirectory,
                         const std::string& pRecoveryStartFilePath,
                         const std::string& pRecoverDirectory,
                         const std::vector<TestSeedFile>& pExpectedRecoveredFiles,
                         bool pUseEncryption,
                         bool pRequireDirectoryMatch,
                         std::string& pRecoverMessage,
                         std::string& pErrorMessage);

bool ExecuteBundleAndRecoverSmoke(FileSystem& pFileSystem,
                                  ApplicationCore& pCore,
                                  const std::vector<TestSeedFile>& pSeedFiles,
                                  const BundleExecutionSpec& pBundleSpec,
                                  const std::string& pRecoverDirectory,
                                  const std::vector<TestSeedFile>& pExpectedRecoveredFiles,
                                  std::string& pRecoverMessage,
                                  std::string& pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_SMOKE_HPP_
