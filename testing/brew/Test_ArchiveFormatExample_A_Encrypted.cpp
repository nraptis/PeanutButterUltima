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
  aFileSystem.AddFile("/input/love.txt", ToBytes("cpu"));
  aFileSystem.AddFile("/input/presidents/lincoln.txt", ToBytes("abraham"));
  aFileSystem.AddFile("/input/presidents/washington.txt", ToBytes("george"));
  aFileSystem.AddFile("/input/zebra.txt", ToBytes("attack zzz"));

  RotateMaskBlockCipher aCrypt(0xAA, 3);
  std::string aError;
  if (!Execute_BundleAndUnbundle(aFileSystem, aCrypt, true, "/input", "/archives", "/output", &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }

  std::cout << "Test_ArchiveFormatExample_A_Encrypted passed\n";
  return 0;
}
