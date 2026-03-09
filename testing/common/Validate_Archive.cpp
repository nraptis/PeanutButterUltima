#include "Validate_Archive.hpp"

#include "Test_Utils.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

bool Validate_Archive(const ByteVector& pExpectedBytes,
                      const ByteVector& pActualBytes,
                      const std::string& pLabel,
                      std::string* pErrorMessage) {
  TestArchive aExpectedArchive;
  TestArchive aActualArchive;
  std::string aError;

  if (!aExpectedArchive.Load(pExpectedBytes, &aError)) {
    return Fail("Validate_Archive failed for '" + pLabel + "': expected archive parse failed: " + aError,
                pErrorMessage);
  }
  if (!aActualArchive.Load(pActualBytes, &aError)) {
    return Fail("Validate_Archive failed for '" + pLabel + "': actual archive parse failed: " + aError,
                pErrorMessage);
  }

  aExpectedArchive.mPath = pLabel;
  aActualArchive.mPath = pLabel;
  if (!aExpectedArchive.Equals(aActualArchive, &aError)) {
    return Fail("Validate_Archive failed for '" + pLabel + "': " + aError, pErrorMessage);
  }

  if (pExpectedBytes.size() != pActualBytes.size()) {
    return Fail("Validate_Archive failed for '" + pLabel + "': size mismatch expected=" +
                    std::to_string(pExpectedBytes.size()) + " actual=" + std::to_string(pActualBytes.size()),
                pErrorMessage);
  }

  for (std::size_t aIndex = 0; aIndex < pExpectedBytes.size(); ++aIndex) {
    if (pExpectedBytes[aIndex] != pActualBytes[aIndex]) {
      return Fail("Validate_Archive failed for '" + pLabel + "' at offset " + std::to_string(aIndex) +
                      ": expected [" + ToHex(pExpectedBytes, aIndex, 1) + "] actual [" +
                      ToHex(pActualBytes, aIndex, 1) + "]",
                  pErrorMessage);
    }
  }

  return true;
}

}  // namespace peanutbutter::testing
