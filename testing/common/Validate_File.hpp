#ifndef PEANUT_BUTTER_ULTIMA_VALIDATE_FILE_HPP_
#define PEANUT_BUTTER_ULTIMA_VALIDATE_FILE_HPP_

#include <string>
#include <vector>

#include "AppCore.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

bool CollectFiles(const peanutbutter::FileSystem& pFileSystem,
                  const std::string& pRootDirectory,
                  std::vector<TestFile>& pFiles,
                  std::string* pErrorMessage);

bool Validate_File(const TestFile& pExpectedFile,
                   const TestFile& pActualFile,
                   std::string* pErrorMessage);

bool Validate_Files(const std::vector<TestFile>& pExpectedFiles,
                    const std::vector<TestFile>& pActualFiles,
                    std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_VALIDATE_FILE_HPP_
