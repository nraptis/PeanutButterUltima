#ifndef PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_

#include <string>

#include "AppCore.hpp"
#include "MockFileSystem.hpp"

namespace peanutbutter::ultima::testing {

bool Execute_BundleAndUnbundle(peanutbutter::ultima::testing::MockFileSystem& pFileSystem,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage);

bool Execute_BundleAndUnbundle(peanutbutter::ultima::testing::MockFileSystem& pFileSystem,
                               peanutbutter::ultima::Crypt& pCrypt,
                               bool pUseEncryption,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage);

}  // namespace peanutbutter::ultima::testing

#endif  // PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_
