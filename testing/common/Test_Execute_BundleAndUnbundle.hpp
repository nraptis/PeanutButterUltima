#ifndef PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_

#include <string>

#include "AppCore.hpp"
#include "MockFileSystem.hpp"

namespace peanutbutter::testing {

bool Execute_BundleAndUnbundle(peanutbutter::testing::MockFileSystem& pFileSystem,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage);

bool Execute_BundleAndUnbundle(peanutbutter::testing::MockFileSystem& pFileSystem,
                               peanutbutter::Crypt& pCrypt,
                               bool pUseEncryption,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TEST_EXECUTE_BUNDLE_AND_UNBUNDLE_HPP_
