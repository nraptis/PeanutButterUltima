#include <iostream>
#include <string>

#include "Encryption/RotateMaskBlockCipher.hpp"
#include "Test_Execute_BundleAndUnbundle.hpp"
#include "Test_Utils.hpp"

namespace {

using peanutbutter::RotateMaskBlockCipher;
using peanutbutter::testing::Execute_BundleAndUnbundle;
using peanutbutter::testing::MockFileSystem;
using peanutbutter::testing::ToBytes;

}  // namespace

int main() {
  MockFileSystem aFileSystem;
  aFileSystem.AddFile("/input/a", ToBytes("b"));
  aFileSystem.AddFile("/input/end", ToBytes("end"));

  RotateMaskBlockCipher aCrypt(0xAA, 3);
  std::string aError;
  if (!Execute_BundleAndUnbundle(aFileSystem, aCrypt, true, "/input", "/archives", "/output", &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }

  std::cout << "Test_ArchiveFormatExample_C_Encrypted passed\n";
  return 0;
}
