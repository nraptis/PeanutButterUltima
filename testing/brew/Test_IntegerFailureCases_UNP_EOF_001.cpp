#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::testing::SeedMultiArchiveIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
    if (pBytes.size() < aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 4) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for EOF mutation.";
      }
      return false;
    }
    pBytes[aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 0] = 0x00;
    pBytes[aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 1] = 0x00;
    pBytes[aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 2] = 0x00;
    pBytes[aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 3] = 0x00;
    for (std::size_t aIndex = aOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 4; aIndex < pBytes.size(); ++aIndex) {
      pBytes[aIndex] = 0x00;
    }
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_EOF_001", SeedMultiArchiveIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_EOF_001 passed\n";
  return 0;
}
