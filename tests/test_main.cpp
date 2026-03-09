#include <QCoreApplication>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "QtAppController.hpp"
#include "MockFileSystem.hpp"

namespace {

using namespace peanutbutter;
using peanutbutter::testing::MockFileSystem;

ByteVector ToBytes(const std::string& pText) {
  return ByteVector(pText.begin(), pText.end());
}

std::string Repeat(const std::string& pText, std::size_t pCount) {
  std::string aResult;
  for (std::size_t aIndex = 0; aIndex < pCount; ++aIndex) {
    aResult += pText;
  }
  return aResult;
}

struct TestShell final : public AppShell {
  void SetLoading(bool pEnabled) override {
    mEvents.push_back(pEnabled ? "loading:on" : "loading:off");
  }

  void ShowError(const std::string& pTitle, const std::string& pMessage) override {
    mEvents.push_back("error:" + pTitle + ":" + pMessage);
  }

  DestinationAction PromptDestinationAction(const std::string& pOperationName,
                                            const std::string& pDestinationPath) override {
    mEvents.push_back("prompt:" + pOperationName + ":" + pDestinationPath);
    return mAction;
  }

  DestinationAction mAction = DestinationAction::Merge;
  std::vector<std::string> mEvents;
};

bool Contains(const std::vector<std::string>& pMessages, const std::string& pNeedle) {
  return std::find_if(pMessages.begin(), pMessages.end(),
                      [&](const std::string& pMessage) { return pMessage.find(pNeedle) != std::string::npos; }) !=
         pMessages.end();
}

bool TestRoundTrip() {
  MockFileSystem aFileSystem;
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  aFileSystem.AddFile("/input/alpha.txt", ToBytes("alpha"));
  aFileSystem.AddFile("/input/nested/beta.txt", ToBytes("beta"));

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";

  const OperationResult aBundleResult = aCore.RunBundle(aBundleRequest, DestinationAction::Merge);
  if (!aBundleResult.mSucceeded) {
    std::cerr << "Bundle failed in round-trip test\n";
    return false;
  }

  UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mArchiveDirectory = "/archives";
  aUnbundleRequest.mDestinationDirectory = "/output";

  const OperationResult aUnbundleResult = aCore.RunUnbundle(aUnbundleRequest, DestinationAction::Merge);
  if (!aUnbundleResult.mSucceeded) {
    std::cerr << "Unbundle failed in round-trip test: " << aUnbundleResult.mMessage << "\n";
    return false;
  }

  ValidateRequest aValidateRequest{"/input", "/output"};
  const OperationResult aValidateResult = aCore.RunValidate(aValidateRequest);
  if (!aValidateResult.mSucceeded) {
    std::cerr << "Validate failed in round-trip test\n";
    return false;
  }

  if (!Contains(aLogger.StatusMessages(), "Bundle job starting") ||
      !Contains(aLogger.StatusMessages(), "Unbundle job starting") ||
      !Contains(aLogger.StatusMessages(), "Sanity job complete")) {
    std::cerr << "Expected status logs were not captured\n";
    return false;
  }

  return true;
}

bool TestRecoverFromSecondArchive() {
  MockFileSystem aFileSystem;
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  aFileSystem.AddFile("/input/a.txt", ToBytes("one"));
  aFileSystem.AddFile("/input/b.txt", ToBytes("two"));
  aFileSystem.AddFile("/input/c.txt", ToBytes(Repeat("three-three-three-", 70000)));

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";
  if (!aCore.RunBundle(aBundleRequest, DestinationAction::Merge).mSucceeded) {
    std::cerr << "Bundle failed in recover test\n";
    return false;
  }

  const std::vector<DirectoryEntry> aArchives = aFileSystem.ListFiles("/archives");
  if (aArchives.size() < 2) {
    std::cerr << "Recover test did not produce multiple archives\n";
    return false;
  }

  std::size_t aRecoverableArchiveIndex = 0;
  bool aFoundRecoverableArchive = false;
  for (std::size_t aIndex = 0; aIndex < aArchives.size(); ++aIndex) {
    if (aArchives[aIndex].mPath.find('R') != std::string::npos) {
      aRecoverableArchiveIndex = aIndex;
      aFoundRecoverableArchive = true;
      break;
    }
  }
  if (!aFoundRecoverableArchive) {
    std::cerr << "Recover test did not produce a recoverable archive\n";
    return false;
  }

  RecoverRequest aRecoverRequest;
  aRecoverRequest.mArchiveDirectory = "/archives";
  aRecoverRequest.mRecoveryStartFilePath = aArchives[aRecoverableArchiveIndex].mPath;
  aRecoverRequest.mDestinationDirectory = "/recovered";
  const OperationResult aRecoverResult = aCore.RunRecover(aRecoverRequest, DestinationAction::Merge);
  if (!aRecoverResult.mSucceeded) {
    std::cerr << "Recover failed: " << aRecoverResult.mMessage << "\n";
    return false;
  }

  if (!aFileSystem.IsFile("/recovered/b.txt") || !aFileSystem.IsFile("/recovered/c.txt")) {
    std::cerr << "Recover did not recreate later files: "
              << "b=" << (aFileSystem.IsFile("/recovered/b.txt") ? "yes" : "no")
              << " c=" << (aFileSystem.IsFile("/recovered/c.txt") ? "yes" : "no") << "\n";
    return false;
  }
  if (aRecoverableArchiveIndex > 0 && aFileSystem.IsFile("/recovered/a.txt")) {
    std::cerr << "Recover should not have recreated the first file from a later archive\n";
    return false;
  }
  return true;
}

bool TestControllerSequencing() {
  int aArgc = 0;
  QCoreApplication aApp(aArgc, nullptr);

  MockFileSystem aFileSystem;
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);
  TestShell aShell;
  QtAppController aController(aShell, aCore);

  aFileSystem.AddFile("/archives/bad.PBTR", ToBytes("not an archive"));

  UnbundleRequest aRequest;
  aRequest.mArchiveDirectory = "/archives";
  aRequest.mDestinationDirectory = "/output";
  aController.TriggerUnbundleFlow(aRequest);

  if (aShell.mEvents.empty() || aShell.mEvents.front().rfind("error:", 0) != 0) {
    std::cerr << "Invalid preflight should show error before loading\n";
    return false;
  }

  aShell.mEvents.clear();
  aFileSystem.ClearDirectory("/archives");
  aFileSystem.AddFile("/input/file1.txt", ToBytes("111"));
  aFileSystem.AddFile("/input/file2.txt", ToBytes("222"));

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";
  aController.TriggerBundleFlow(aBundleRequest);
  while (aController.IsBusy()) {
    aApp.processEvents();
  }

  const auto aLoadingOn = std::find(aShell.mEvents.begin(), aShell.mEvents.end(), "loading:on");
  const auto aLoadingOff = std::find(aShell.mEvents.begin(), aShell.mEvents.end(), "loading:off");
  if (aLoadingOn == aShell.mEvents.end() || aLoadingOff == aShell.mEvents.end() || aLoadingOn > aLoadingOff) {
    std::cerr << "Controller did not toggle loading around the operation\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!TestRoundTrip()) {
    return 1;
  }
  if (!TestRecoverFromSecondArchive()) {
    return 2;
  }
  if (!TestControllerSequencing()) {
    return 3;
  }

  std::cout << "PeanutButterUltimaTests passed\n";
  return 0;
}
