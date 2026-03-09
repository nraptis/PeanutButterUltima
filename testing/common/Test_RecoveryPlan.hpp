#ifndef PEANUT_BUTTER_ULTIMA_TEST_RECOVERY_PLAN_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_RECOVERY_PLAN_HPP_

#include <string>
#include <vector>

#include "Encryption/Crypt.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter::ultima::testing {

bool GenerateAllRecoveryHeaders(const peanutbutter::ultima::FileSystem& pFileSystem,
                                const std::string& pArchiveDirectory,
                                std::vector<unsigned long long>& pRecoveryHeaders,
                                const peanutbutter::ultima::Crypt* pCrypt,
                                bool pUseEncryption,
                                std::string* pErrorMessage);

bool CollectAllRecoveryHeaders(const peanutbutter::ultima::FileSystem& pFileSystem,
                               const std::string& pArchiveDirectory,
                               std::vector<unsigned long long>& pRecoveryHeaders,
                               const peanutbutter::ultima::Crypt* pCrypt,
                               bool pUseEncryption,
                               std::string* pErrorMessage);

bool GenerateAllRecoveryHeaders(const peanutbutter::ultima::FileSystem& pFileSystem,
                                const std::string& pArchiveDirectory,
                                std::vector<unsigned long long>& pRecoveryHeaders,
                                std::string* pErrorMessage);

bool CollectAllRecoveryHeaders(const peanutbutter::ultima::FileSystem& pFileSystem,
                               const std::string& pArchiveDirectory,
                               std::vector<unsigned long long>& pRecoveryHeaders,
                               std::string* pErrorMessage);

}  // namespace peanutbutter::ultima::testing

#endif  // PEANUT_BUTTER_ULTIMA_TEST_RECOVERY_PLAN_HPP_
