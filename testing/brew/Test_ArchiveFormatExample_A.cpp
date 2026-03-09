#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "Test_Execute_BundleAndUnbundle.hpp"
#include "Test_Utils.hpp"
#include "Test_Wrappers.hpp"
#include "Validate_Archive.hpp"
#include "Validate_ArchiveHeader.hpp"
#include "Validate_RecoveryHeader.hpp"

namespace {

using namespace peanutbutter::ultima;

struct ExpectedArchive {
  std::string mName;
  ByteVector mBytes;
};

bool Fail(const std::string& pMessage) {
  std::cerr << pMessage << "\n";
  return false;
}

bool ValidateExpectedHeaders(const std::vector<ExpectedArchive>& pExpectedArchives) {
  using peanutbutter::ultima::testing::Read_ArchiveHeader;
  using peanutbutter::ultima::testing::Read_RecoveryHeader;
  using peanutbutter::ultima::testing::TestArchiveHeader;
  using peanutbutter::ultima::testing::TestRecoveryHeader;
  using peanutbutter::ultima::testing::Validate_ArchiveHeader;
  using peanutbutter::ultima::testing::Validate_RecoveryHeader;

  constexpr unsigned long long kExpectedArchiveIdentifier = 0x1fd423f6e2995a96ULL;

  for (std::size_t aArchiveIndex = 0; aArchiveIndex < pExpectedArchives.size(); ++aArchiveIndex) {
    TestArchiveHeader aArchiveHeader;
    std::string aHeaderError;
    if (!Read_ArchiveHeader(pExpectedArchives[aArchiveIndex].mBytes, aArchiveHeader, &aHeaderError)) {
      return Fail(aHeaderError);
    }
    if (!Validate_ArchiveHeader(aArchiveHeader,
                                aArchiveIndex,
                                false,
                                aArchiveIndex == 0 ? 1 : 0,
                                false,
                                kExpectedArchiveIdentifier,
                                false,
                                &aHeaderError)) {
      return Fail(aHeaderError);
    }

    TestRecoveryHeader aRecoveryHeader;
    std::string aRecoveryError;
    if (!Read_RecoveryHeader(pExpectedArchives[aArchiveIndex].mBytes, 0x0024, aRecoveryHeader, &aRecoveryError)) {
      return Fail(aRecoveryError);
    }
    if (!Validate_RecoveryHeader(aRecoveryHeader,
                                 aArchiveIndex == 0 ? 0 : 0,
                                 aArchiveIndex == 0,
                                 &aRecoveryError)) {
      return Fail(aRecoveryError);
    }
  }

  return true;
}

}  // namespace

int main() {
  using peanutbutter::ultima::testing::Execute_BundleAndUnbundle;
  using peanutbutter::ultima::testing::MockFileSystem;
  using peanutbutter::ultima::testing::ToBytes;
  using peanutbutter::ultima::testing::Validate_Archive;

  const std::vector<ExpectedArchive> aExpectedArchives = {
      {"archive_1.PBTR",
       ByteVector{0x1e, 0xab, 0x1d, 0xf0, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x5a, 0x99, 0xe2, 0xf6, 0x23, 0xd4, 0x1f,
                  0x00, 0x00, 0x00, 0x00, 0xad, 0xfb, 0xca, 0xde, 0x76, 0x47, 0xb2, 0x1d, 0xef, 0x8e,
                  0x08, 0x00, 0x6c, 0x6f, 0x76, 0x65, 0x2e, 0x74, 0x78, 0x74, 0x03, 0x00, 0x07, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x70, 0x75, 0x16, 0x00, 0x70,
                  0x72, 0x65, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x69, 0x64, 0x65, 0x6e, 0x74,
                  0x73, 0x2f, 0x6c, 0x69, 0x6e, 0x63, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6f, 0x6c,
                  0x6e, 0x2e, 0x74, 0x78, 0x74, 0x07, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x61, 0x62, 0x72, 0x61, 0x68, 0x61, 0x6d, 0x19, 0x00, 0x70, 0x72,
                  0x2f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x73, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x73,
                  0x2f, 0x77, 0x61, 0x73, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6e, 0x67,
                  0x74, 0x6f, 0x6e, 0x2e, 0x74, 0x78, 0x74, 0x06, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x65, 0x6f, 0x72, 0x67, 0x65, 0x09}},
      {"archive_2.PBTR",
       ByteVector{0x1e, 0xab, 0x1d, 0xf0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x5a, 0x99, 0xe2, 0xf6, 0x23, 0xd4, 0x1f,
                  0x00, 0x00, 0x00, 0x00, 0xad, 0xfb, 0xca, 0xde, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x7a, 0x65, 0x62, 0x72, 0x61, 0x2e, 0x74, 0x78, 0x74, 0x0a, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x74, 0x74, 0x61, 0x63, 0x6b,
                  0x20, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7a, 0x7a, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};

  if (!ValidateExpectedHeaders(aExpectedArchives)) {
    return 1;
  }

  MockFileSystem aFileSystem;
  aFileSystem.AddFile("/input/love.txt", ToBytes("cpu"));
  aFileSystem.AddFile("/input/presidents/lincoln.txt", ToBytes("abraham"));
  aFileSystem.AddFile("/input/presidents/washington.txt", ToBytes("george"));
  aFileSystem.AddFile("/input/zebra.txt", ToBytes("attack zzz"));

  std::string aBundleAndUnbundleError;
  if (!Execute_BundleAndUnbundle(aFileSystem, "/input", "/archives", "/output", &aBundleAndUnbundleError)) {
    std::cerr << aBundleAndUnbundleError << "\n";
    return 1;
  }

  std::vector<DirectoryEntry> aActualArchives = aFileSystem.ListFiles("/archives");
  std::sort(aActualArchives.begin(), aActualArchives.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) { return pLeft.mPath < pRight.mPath; });

  if (aActualArchives.size() != aExpectedArchives.size()) {
    std::cerr << "archive count mismatch expected=" << aExpectedArchives.size()
              << " actual=" << aActualArchives.size() << "\n";
    return 1;
  }

  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aExpectedArchives.size(); ++aArchiveIndex) {
    ByteVector aActualBytes;
    if (!aFileSystem.ReadFile(aActualArchives[aArchiveIndex].mPath, aActualBytes)) {
      std::cerr << "Could not read archive " << aActualArchives[aArchiveIndex].mPath << "\n";
      return 1;
    }
    std::string aArchiveError;
    if (!Validate_Archive(aExpectedArchives[aArchiveIndex].mBytes,
                          aActualBytes,
                          "archive[" + std::to_string(aArchiveIndex) + "]",
                          &aArchiveError)) {
      std::cerr << aArchiveError << "\n";
      return 1;
    }
  }

  std::cout << "Test_ArchiveFormatExample_A passed\n";
  return 0;
}
