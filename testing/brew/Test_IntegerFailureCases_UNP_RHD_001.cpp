#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::ultima::testing::ArchiveMutator;
  using peanutbutter::ultima::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::ultima::testing::SeedMultiArchiveIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ultima::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L1_LENGTH;
    if (pBytes.size() < aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for recovery-header overwrite.";
      }
      return false;
    }
    pBytes[aOffset + 0] = 200;
    pBytes[aOffset + 1] = 0x00;
    pBytes[aOffset + 2] = 0x00;
    pBytes[aOffset + 3] = 0x00;
    pBytes[aOffset + 4] = 0x00;
    pBytes[aOffset + 5] = 0x00;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_RHD_001", SeedMultiArchiveIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_RHD_001 passed\n";
  return 0;
}
