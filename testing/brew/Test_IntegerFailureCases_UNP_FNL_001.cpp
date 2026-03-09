#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::ultima::testing::ArchiveMutator;
  using peanutbutter::ultima::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::ultima::testing::SeedBasicIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ultima::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_RECOVERY_HEADER_LENGTH;
    if (pBytes.size() < aOffset + 2) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for path-length overwrite.";
      }
      return false;
    }
    pBytes[aOffset + 0] = 0x01;
    pBytes[aOffset + 1] = 0x08;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FNL_001", SeedBasicIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FNL_001 passed\n";
  return 0;
}
