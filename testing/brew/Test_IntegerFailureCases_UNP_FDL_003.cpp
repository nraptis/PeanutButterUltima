#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::testing::SeedBasicIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aContentLengthOffset =
        peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_RECOVERY_HEADER_LENGTH + 2 + 5;
    if (pBytes.size() <= aContentLengthOffset) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for content-length overwrite.";
      }
      return false;
    }
    pBytes[aContentLengthOffset] = 11;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FDL_003", SeedBasicIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FDL_003 passed\n";
  return 0;
}
