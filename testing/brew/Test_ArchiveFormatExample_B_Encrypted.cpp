#include <iostream>
#include <string>

#include "Encryption/RotateMaskBlockCipher.hpp"
#include "Test_Execute_BundleAndUnbundle.hpp"
#include "Test_Utils.hpp"

namespace {

using peanutbutter::ultima::RotateMaskBlockCipher;
using peanutbutter::ultima::testing::Execute_BundleAndUnbundle;
using peanutbutter::ultima::testing::MockFileSystem;
using peanutbutter::ultima::testing::ToBytes;

}  // namespace

int main() {
  MockFileSystem aFileSystem;
  aFileSystem.AddFile("/input/alpha.txt", ToBytes("The Beginning"));
  aFileSystem.AddFile("/input/poem_line_1.txt", ToBytes("There once was a man."));
  aFileSystem.AddFile("/input/poem_line_2.txt", ToBytes("The man had a plan. The plan was a man."));
  aFileSystem.AddFile("/input/poem_line_3.txt", ToBytes("Eulogy"));
  aFileSystem.AddFile("/input/zeta.txt", ToBytes("Fin"));

  RotateMaskBlockCipher aCrypt(0xAA, 3);
  std::string aError;
  if (!Execute_BundleAndUnbundle(aFileSystem, aCrypt, true, "/input", "/archives", "/output", &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }

  std::cout << "Test_ArchiveFormatExample_B_Encrypted passed\n";
  return 0;
}
