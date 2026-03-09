#include <iostream>
#include <string>

#include "Test_IntegerFailureCaseCommon.hpp"
#include "Test_Utils.hpp"

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::InputSeeder;
  using peanutbutter::testing::RunUnbundleIntegerFailureCase;

  const InputSeeder aSeedInput = [](peanutbutter::testing::MockFileSystem& pFileSystem) {
    pFileSystem.AddFile("/input/a.txt", peanutbutter::testing::ToBytes("abcdefghijklmnopqrstuv"));
    pFileSystem.AddFile("/input/b.txt", peanutbutter::testing::ToBytes("beta"));
    pFileSystem.AddFile("/input/c.txt", peanutbutter::testing::ToBytes("gamma"));
  };

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
    const std::size_t aFirstArchivePayloadOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
    const std::size_t aFirstPathLengthByteOffset = aFirstArchivePayloadOffset + peanutbutter::SB_RECOVERY_HEADER_LENGTH;
    const std::size_t aSecondPathLengthByte0 = aFirstPathLengthByteOffset + 35;
    const std::size_t aSecondPathLengthByte1 = aFirstPathLengthByteOffset + 42;
    if (pBytes.size() <= aSecondPathLengthByte1) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for second path-length overwrite.";
      }
      return false;
    }
    pBytes[aSecondPathLengthByte0] = 11;
    pBytes[aSecondPathLengthByte1] = 0x00;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_FNL_005", aSeedInput, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_FNL_005 passed\n";
  return 0;
}
