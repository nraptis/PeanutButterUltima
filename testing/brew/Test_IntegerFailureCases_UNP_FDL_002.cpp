#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::ultima::testing::ArchiveMutator;
  using peanutbutter::ultima::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::ultima::testing::SeedBasicIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ultima::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aPathLengthOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_RECOVERY_HEADER_LENGTH;
    if (pBytes.size() < aPathLengthOffset + 2) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for content-length overwrite.";
      }
      return false;
    }
    const std::size_t aPathLength = static_cast<std::size_t>(pBytes[aPathLengthOffset]);
    const std::size_t aContentLengthOffset = aPathLengthOffset + 2 + aPathLength;
    if (pBytes.size() < aContentLengthOffset + 6) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for content-length overwrite.";
      }
      return false;
    }
    for (std::size_t aIndex = 0; aIndex < 6; ++aIndex) {
      pBytes[aContentLengthOffset + aIndex] = 0x00;
    }
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FDL_002", SeedBasicIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FDL_002 passed\n";
  return 0;
}
