#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::RunRecoverIntegerFailureCase;
  using peanutbutter::testing::SeedMultiArchiveIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L1_LENGTH;
    if (pBytes.size() < aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for recovery-header overwrite.";
      }
      return false;
    }
    pBytes[aOffset + 0] = 60;
    pBytes[aOffset + 1] = 0x00;
    pBytes[aOffset + 2] = 0x00;
    pBytes[aOffset + 3] = 0x00;
    pBytes[aOffset + 4] = 0x00;
    pBytes[aOffset + 5] = 0x00;
    return true;
  };

  std::string aError;
  if (!RunRecoverIntegerFailureCase("RCV_RHD_001", SeedMultiArchiveIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_RCV_RHD_001 passed\n";
  return 0;
}
