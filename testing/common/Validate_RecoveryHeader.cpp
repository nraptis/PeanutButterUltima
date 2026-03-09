#include "Validate_RecoveryHeader.hpp"

#include <sstream>

#include "Test_Utils.hpp"

namespace peanutbutter::ultima::testing {

namespace {

bool IsAllZero(const std::array<unsigned char, kDemoRecoveryHeaderLength>& pBytes) {
  for (unsigned char aByte : pBytes) {
    if (aByte != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool Validate_RecoveryHeader(TestRecoveryHeader pRecoveryHeader,
                             unsigned long long pStride,
                             bool pStrideSkip,
                             std::string* pErrorMessage) {
  if (pStrideSkip) {
    if (IsAllZero(pRecoveryHeader.mRawBytes)) {
      return Fail("Recovery header validation failed: special first recovery header must not be all zero.", pErrorMessage);
    }
    return true;
  }

  if (pRecoveryHeader.mStride != pStride) {
    std::ostringstream aStream;
    aStream << "Recovery header validation failed: stride mismatch. expected="
            << pStride << " actual=" << pRecoveryHeader.mStride;
    return Fail(aStream.str(), pErrorMessage);
  }

  return true;
}

}  // namespace peanutbutter::ultima::testing
