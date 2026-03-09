#ifndef PEANUT_BUTTER_ULTIMA_VALIDATE_RECOVERY_HEADER_HPP_
#define PEANUT_BUTTER_ULTIMA_VALIDATE_RECOVERY_HEADER_HPP_

#include <string>

#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

bool Validate_RecoveryHeader(TestRecoveryHeader pRecoveryHeader,
                             unsigned long long pStride,
                             bool pStrideSkip,
                             std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_VALIDATE_RECOVERY_HEADER_HPP_
