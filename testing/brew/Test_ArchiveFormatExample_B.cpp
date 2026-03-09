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

using namespace peanutbutter;

struct ExpectedArchive {
  ByteVector mBytes;
};

bool Fail(const std::string& pMessage) {
  std::cerr << pMessage << "\n";
  return false;
}

void PrintArchiveBytes(const ByteVector& pBytes, const std::string& pLabel) {
  std::cerr << pLabel << " = {\n  ";
  for (std::size_t aIndex = 0; aIndex < pBytes.size(); ++aIndex) {
    std::cerr << "0x" << peanutbutter::testing::ToHex(pBytes, aIndex, 1);
    if (aIndex + 1 != pBytes.size()) {
      std::cerr << ", ";
    }
    if ((aIndex + 1) % 12 == 0 && aIndex + 1 != pBytes.size()) {
      std::cerr << "\n  ";
    }
  }
  std::cerr << "\n}\n";
}

bool ValidateExpectedHeaders(const std::vector<ExpectedArchive>& pExpectedArchives) {
  using peanutbutter::testing::Read_ArchiveHeader;
  using peanutbutter::testing::Read_RecoveryHeader;
  using peanutbutter::testing::TestArchiveHeader;
  using peanutbutter::testing::TestRecoveryHeader;
  using peanutbutter::testing::Validate_ArchiveHeader;
  using peanutbutter::testing::Validate_RecoveryHeader;

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
                                1,
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
                                 aArchiveIndex == 0 ? 0 : 58,
                                 aArchiveIndex == 0,
                                 &aRecoveryError)) {
      return Fail(aRecoveryError);
    }
  }

  return true;
}

}  // namespace

int main() {
  using peanutbutter::testing::Execute_BundleAndUnbundle;
  using peanutbutter::testing::MockFileSystem;
  using peanutbutter::testing::ToBytes;
  using peanutbutter::testing::Validate_Archive;

  const std::vector<ExpectedArchive> aExpectedArchives = {
      {ByteVector{0x1e, 0xab, 0x1d, 0xf0, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x5a, 0x99, 0xe2,
                  0xf6, 0x23, 0xd4, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xad, 0xfb, 0xca, 0xde,
                  0x76, 0x47, 0xb2, 0x1d, 0xef, 0x8e, 0x09, 0x00, 0x61, 0x6c, 0x70, 0x68,
                  0x61, 0x2e, 0x74, 0x78, 0x74, 0x0d, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x68, 0x65, 0x20, 0x42, 0x65, 0x67,
                  0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x6e, 0x69, 0x6e, 0x67,
                  0x0f, 0x00, 0x70, 0x6f, 0x65, 0x6d, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x5f, 0x6c, 0x69, 0x6e, 0x65, 0x5f, 0x31, 0x2e, 0x74, 0x78, 0x74, 0x15,
                  0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54,
                  0x68, 0x65, 0x72, 0x65, 0x20, 0x6f, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x6e, 0x63, 0x65, 0x20, 0x77, 0x61, 0x73, 0x20, 0x61, 0x20, 0x6d, 0x61,
                  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x2e, 0x0f, 0x00, 0x70, 0x6f,
                  0x65, 0x6d, 0x5f, 0x6c, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x65, 0x5f, 0x32, 0x2e, 0x74, 0x78, 0x74, 0x27, 0x00, 0x00, 0x00, 0x00}},
      {ByteVector{0x1e, 0xab, 0x1d, 0xf0, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x5a, 0x99, 0xe2,
                  0xf6, 0x23, 0xd4, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xad, 0xfb, 0xca, 0xde,
                  0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x68, 0x65, 0x20, 0x6d,
                  0x61, 0x6e, 0x20, 0x68, 0x61, 0x64, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x20, 0x61, 0x20, 0x70, 0x6c, 0x61, 0x6e, 0x2e, 0x20, 0x54, 0x68, 0x65,
                  0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x70, 0x6c, 0x61, 0x6e, 0x20,
                  0x77, 0x61, 0x73, 0x20, 0x61, 0x20, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x6d, 0x61, 0x6e, 0x2e, 0x0f, 0x00, 0x70, 0x6f, 0x65, 0x6d, 0x5f, 0x6c,
                  0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x65, 0x5f, 0x33, 0x2e,
                  0x74, 0x78, 0x74, 0x06, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x45, 0x75, 0x6c, 0x6f, 0x67, 0x79, 0x08, 0x00, 0x7a,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x74, 0x61, 0x2e, 0x74, 0x78,
                  0x74, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x46, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};

  if (!ValidateExpectedHeaders(aExpectedArchives)) {
    return 1;
  }

  MockFileSystem aFileSystem;
  aFileSystem.AddFile("/input/alpha.txt", ToBytes("The Beginning"));
  aFileSystem.AddFile("/input/poem_line_1.txt", ToBytes("There once was a man."));
  aFileSystem.AddFile("/input/poem_line_2.txt", ToBytes("The man had a plan. The plan was a man."));
  aFileSystem.AddFile("/input/poem_line_3.txt", ToBytes("Eulogy"));
  aFileSystem.AddFile("/input/zeta.txt", ToBytes("Fin"));

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
      PrintArchiveBytes(aActualBytes, "actual archive[" + std::to_string(aArchiveIndex) + "]");
      return 1;
    }
  }

  std::cout << "Test_ArchiveFormatExample_B passed\n";
  return 0;
}
