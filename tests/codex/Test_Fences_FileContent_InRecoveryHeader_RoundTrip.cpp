#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_Utils.hpp"

namespace {

using peanutbutter::testing::FenceRoundTripOutcome;
using peanutbutter::testing::FenceRoundTripSpec;
using peanutbutter::testing::GenericArchiveByteMutation;
using peanutbutter::testing::TestSeedFile;

struct CodexCase {
  std::string mCaseName;
  std::string mExpectedErrorCode;
  std::size_t mArchiveBlockCount = 1;
  std::uint32_t mArchiveIndex = 0;
  std::size_t mMutationFileOffset = 0;
  std::vector<unsigned char> mMutationBytes;
  std::vector<TestSeedFile> mInputFiles;
};

const CodexCase kCase = {
    "FileContent_InRecoveryHeader_RoundTrip_Deterministic",
    "UNP_FDL_FENCE",
    2u,
    static_cast<std::uint32_t>(1u),
    static_cast<std::size_t>(80u),
    {0x88, 0x03, 0x00, 0x00, 0x00, 0x00},
    {
        {"BlitjBkxht/LmuVWuq.dat", "xJNTDAxMtwhXQGHIjeZathhUY"},
        {"LETIrkIssboBcjHFwdXjpr/AYdUowJAxlmxbxpCdl/viahxaDIIkILTlLR.dat",
         "dckWjpeBWVRxjFtFXSsupRxuMfObEfwLrGycIooRyinydKOSdBNdkLXXefNGMhpmzbUp"},
        {"QRKLAFhIqiuYDEHELhmEYIyaopTIEd/irLKLd.cfg", "fOiKaOULfpZWWUFeukFSXaERmKisP"},
        {"USUvuSTQy/TapyndckIGgYbD/dMzaxIzYJJbdscEpSrQwUOnyeBenaOhS.cfg", "OsiUOw"},
        {"bKRrIjyDzXihAvIaeNIZJqSR/zamgycqDygHxpMiEXFZPwPVFb/sBwBRs.md", "OORxVKHSINxYDMIXNCkimsvbfkgfQLnRzWDB"},
        {"fZcwYaevyXuCCStdAjUkILdlxuKepRP/iQfcNuUDeRdfesjEhjAAMSkAZlfnypS/xANwhvuIoQkQTYZcCgaNJBokGoaPopaL.bin",
         "WDcCIWtQXLSDxOrnCed"},
        {"hggtJmXsGqkaGBZGGa/xHyk.dat", "ZcrnsZXlsoAuCmxLPqqIbGORWNoRONAJCizoZnSMgqiUEnriiKsRLubVMHVW"},
        {"iXZoRtxcsxveiRmluQJppxKDBppPcoKRet/eOqKLJiJYuHoUNA/ArMfElWnJFBTB.cfg", "qiSKYenIAtgpjGQUWRBFTlLRHIFgq"},
        {"oYNmNuTMqDiIhVnXbm/cKP/DvGqr.md", "iZcnHJREqzclitFjZXyLjOHUAQWXdnTyJMDeuLPojqCrMGdVCJqaUZwZYc"},
        {"taOy.dat", "HOoPbfCRcesRPMrqXUiwPXUkATJBGYO"},
        {"uRVvPTqoGulpoVVxJC/VaiuIOHAAMkflPodHFFuYwvaQWbSodP/mowpiZKHLebUukPZaxgIJv.cfg", "XwxOLZak"},
    },
};

}  // namespace

int main() {
  FenceRoundTripSpec aSpec;
  aSpec.mCaseName = kCase.mCaseName;
  aSpec.mOriginalFiles = kCase.mInputFiles;
  aSpec.mRecoverableFiles = kCase.mInputFiles;
  aSpec.mArchiveBlockCount = kCase.mArchiveBlockCount;
  aSpec.mExpectMutatedUnbundleFailure = true;
  aSpec.mExpectedUnbundleErrorCode = kCase.mExpectedErrorCode;
  aSpec.mRunRecoverAfterMutation = false;
  aSpec.mRequireRecoverTreeMatch = false;

  GenericArchiveByteMutation aMutation;
  aMutation.mArchiveIndex = kCase.mArchiveIndex;
  aMutation.mFileOffset = kCase.mMutationFileOffset;
  aMutation.mBytes = kCase.mMutationBytes;
  aSpec.mMutation.mArchiveByteMutations.push_back(aMutation);

  const FenceRoundTripOutcome aOutcome = peanutbutter::testing::ExecuteFenceTestRoundTrip(aSpec);
  if (!aOutcome.mSucceeded) {
    std::cerr << "[FAIL] " << kCase.mCaseName << " failed: " << aOutcome.mFailureMessage << "\n";
    std::cerr << aOutcome.mCollectedLogs;
    return 1;
  }
  if (!peanutbutter::testing::ContainsToken(aOutcome.mMutatedUnbundleMessage, kCase.mExpectedErrorCode) &&
      !peanutbutter::testing::ContainsToken(aOutcome.mCollectedLogs, kCase.mExpectedErrorCode)) {
    std::cerr << "[FAIL] " << kCase.mCaseName << " missing expected error code " << kCase.mExpectedErrorCode << ".\n";
    std::cerr << aOutcome.mCollectedLogs;
    return 1;
  }

  std::cout << "[PASS] deterministic file-content in-recovery-header case passed.\n";
  return 0;
}
