#include "Validate_ArchiveHeader.hpp"

#include <sstream>

#include "Test_Utils.hpp"

namespace peanutbutter::ultima::testing {

bool Validate_ArchiveHeader(TestArchiveHeader pArchiveHeader,
                            unsigned long long pArchiveIndex,
                            bool pArchiveIndexSkip,
                            unsigned long long pRecoveryFlag,
                            bool pRecoveryFlagSkip,
                            unsigned long long pArchiveIdentifier,
                            bool pArchiveIdentifierSkip,
                            std::string* pErrorMessage) {
  if (pArchiveHeader.mMagicHeaderBytes != kDemoMagicHeaderBytes) {
    return Fail("Archive header validation failed: demo magic header bytes mismatch.", pErrorMessage);
  }
  if (pArchiveHeader.mMagicFooterBytes != kDemoMagicFooterBytes) {
    return Fail("Archive header validation failed: demo magic footer bytes mismatch.", pErrorMessage);
  }
  if (pArchiveHeader.mMajorVersion != kDemoMajorVersion) {
    return Fail("Archive header validation failed: demo major version mismatch.", pErrorMessage);
  }
  if (pArchiveHeader.mMinorVersion != kDemoMinorVersion) {
    return Fail("Archive header validation failed: demo minor version mismatch.", pErrorMessage);
  }
  if (pArchiveHeader.mReservedBytes != 0) {
    return Fail("Archive header validation failed: reserved bytes must be zero.", pErrorMessage);
  }
  if (!pArchiveIndexSkip && pArchiveHeader.mArchiveIndex != pArchiveIndex) {
    std::ostringstream aStream;
    aStream << "Archive header validation failed: archive index mismatch. expected="
            << pArchiveIndex << " actual=" << pArchiveHeader.mArchiveIndex;
    return Fail(aStream.str(), pErrorMessage);
  }
  if (!pRecoveryFlagSkip && pArchiveHeader.mRecoveryFlag != pRecoveryFlag) {
    std::ostringstream aStream;
    aStream << "Archive header validation failed: recovery flag mismatch. expected="
            << pRecoveryFlag << " actual=" << pArchiveHeader.mRecoveryFlag;
    return Fail(aStream.str(), pErrorMessage);
  }
  if (!pArchiveIdentifierSkip && pArchiveHeader.mArchiveIdentifier != pArchiveIdentifier) {
    std::ostringstream aStream;
    aStream << "Archive header validation failed: archive identifier mismatch. expected="
            << pArchiveIdentifier << " actual=" << pArchiveHeader.mArchiveIdentifier;
    return Fail(aStream.str(), pErrorMessage);
  }
  return true;
}

}  // namespace peanutbutter::ultima::testing
