#ifndef PEANUT_BUTTER_ULTIMA_VALIDATE_MANIFEST_HPP_
#define PEANUT_BUTTER_ULTIMA_VALIDATE_MANIFEST_HPP_

#include <string>

#include "IO/FileSystem.hpp"

namespace peanutbutter::testing {

bool Validate_Manifest(const peanutbutter::FileSystem& pFileSystem,
                       const std::string& pInputDirectory,
                       const std::string& pOutputDirectory,
                       std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_VALIDATE_MANIFEST_HPP_
