#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::testing::SeedBasicIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
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
    pBytes[aContentLengthOffset + 0] = 200;
    pBytes[aContentLengthOffset + 1] = 0x00;
    pBytes[aContentLengthOffset + 2] = 0x00;
    pBytes[aContentLengthOffset + 3] = 0x00;
    pBytes[aContentLengthOffset + 4] = 0x00;
    pBytes[aContentLengthOffset + 5] = 0x00;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FDL_001", SeedBasicIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FDL_001 passed\n";
  return 0;
}
