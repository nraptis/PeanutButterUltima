#ifndef PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HPP_
#define PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HPP_

#include <string>

#include "AppCore.hpp"

namespace peanutbutter::testing {

bool Validate_Archive(const ByteVector& pExpectedBytes,
                      const ByteVector& pActualBytes,
                      const std::string& pLabel,
                      std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HPP_
