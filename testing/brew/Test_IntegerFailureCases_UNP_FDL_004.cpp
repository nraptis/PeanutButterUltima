#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"
#include "Test_Utils.hpp"

int main() {
  using peanutbutter::ultima::testing::ArchiveMutator;
  using peanutbutter::ultima::testing::InputSeeder;
  using peanutbutter::ultima::testing::RunUnbundleIntegerFailureCase;

  const InputSeeder aSeedInput = [](peanutbutter::ultima::testing::MockFileSystem& pFileSystem) {
    pFileSystem.AddFile("/input/a.txt", peanutbutter::ultima::testing::ToBytes("abcdefghij"));
    pFileSystem.AddFile("/input/b.txt", peanutbutter::ultima::testing::ToBytes("beta"));
    pFileSystem.AddFile("/input/c.txt", peanutbutter::ultima::testing::ToBytes("gamma"));
  };

  const ArchiveMutator aMutator = [](peanutbutter::ultima::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aFirstArchivePayloadOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
    const std::size_t aSecondContentLengthByte0 = aFirstArchivePayloadOffset + 30;
    if (pBytes.size() <= aSecondContentLengthByte0) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for second content-length overwrite.";
      }
      return false;
    }
    pBytes[aSecondContentLengthByte0] = 12;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FDL_004", aSeedInput, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FDL_004 passed\n";
  return 0;
}
