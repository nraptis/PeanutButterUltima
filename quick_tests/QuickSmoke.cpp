#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "Encryption/Crypt.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using peanutbutter::ApplicationCore;
using peanutbutter::BundleRequest;
using peanutbutter::DestinationAction;
using peanutbutter::Logger;
using peanutbutter::LocalFileSystem;
using peanutbutter::OperationResult;
using peanutbutter::PassthroughCrypt;
using peanutbutter::RecoverRequest;
using peanutbutter::RuntimeSettings;
using peanutbutter::UnbundleRequest;
using peanutbutter::ValidateRequest;

class QuickLogger final : public Logger {
 public:
  void LogStatus(const std::string& pMessage) override {
    std::cout << "[status] " << pMessage << "\n";
  }
  void LogError(const std::string& pMessage) override {
    std::cerr << "[error] " << pMessage << "\n";
  }
};

bool WriteFile(peanutbutter::FileSystem& pFileSystem, const std::string& pPath, const std::string& pContent) {
  return pFileSystem.WriteTextFile(pPath, pContent);
}

bool RunCase(const std::string& pName, const std::function<bool(std::string&)>& pCaseBody) {
  std::string aError;
  if (!pCaseBody(aError)) {
    std::cerr << "[FAIL] " << pName << ": " << aError << "\n";
    return false;
  }
  std::cout << "[PASS] " << pName << "\n";
  return true;
}

bool RequireSuccess(const OperationResult& pResult, std::string& pError) {
  if (!pResult.mSucceeded) {
    pError = pResult.mMessage;
    return false;
  }
  return true;
}

}  // namespace

int main() {
  LocalFileSystem aFileSystem;
  const std::string aRoot = aFileSystem.JoinPath(aFileSystem.CurrentWorkingDirectory(), "quick_smoke_runtime");
  if (!aFileSystem.ClearDirectory(aRoot) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aRoot, "input")) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aRoot, "archives")) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aRoot, "output")) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aRoot, "recover")) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aFileSystem.JoinPath(aRoot, "input"), "empty_only")) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aFileSystem.JoinPath(aRoot, "input"), "nested/dir"))) {
    std::cerr << "[FAIL] setup: could not prepare directories\n";
    return 1;
  }

  if (!WriteFile(aFileSystem, aFileSystem.JoinPath(aFileSystem.JoinPath(aRoot, "input"), "a.txt"), "alpha") ||
      !WriteFile(aFileSystem, aFileSystem.JoinPath(aFileSystem.JoinPath(aRoot, "input"), "nested/b.bin"), "beta")) {
    std::cerr << "[FAIL] setup: could not seed input files\n";
    return 1;
  }

  PassthroughCrypt aCrypt;
  QuickLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * 1);
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  bool aOk = true;

  aOk &= RunCase("Bundle", [&](std::string& pError) {
    BundleRequest aRequest;
    aRequest.mSourceDirectory = aFileSystem.JoinPath(aRoot, "input");
    aRequest.mDestinationDirectory = aFileSystem.JoinPath(aRoot, "archives");
    aRequest.mArchivePrefix = "q_";
    aRequest.mArchiveSuffix = "pb";
    aRequest.mUseEncryption = false;
    aRequest.mArchiveBlockCount = 1;
    return RequireSuccess(aCore.RunBundle(aRequest, DestinationAction::Clear), pError);
  });

  aOk &= RunCase("Unbundle", [&](std::string& pError) {
    UnbundleRequest aRequest;
    aRequest.mArchiveDirectory = aFileSystem.JoinPath(aRoot, "archives");
    aRequest.mDestinationDirectory = aFileSystem.JoinPath(aRoot, "output");
    aRequest.mUseEncryption = false;
    return RequireSuccess(aCore.RunUnbundle(aRequest, DestinationAction::Clear), pError);
  });

  aOk &= RunCase("Sanity", [&](std::string& pError) {
    ValidateRequest aRequest;
    aRequest.mLeftDirectory = aFileSystem.JoinPath(aRoot, "input");
    aRequest.mRightDirectory = aFileSystem.JoinPath(aRoot, "output");
    return RequireSuccess(aCore.RunValidate(aRequest), pError);
  });

  aOk &= RunCase("Recover wrapper", [&](std::string& pError) {
    RecoverRequest aRequest;
    aRequest.mArchiveDirectory = aFileSystem.JoinPath(aRoot, "archives");
    std::vector<peanutbutter::DirectoryEntry> aArchivePaths = aFileSystem.ListFiles(aRequest.mArchiveDirectory);
    std::sort(aArchivePaths.begin(), aArchivePaths.end(), [&aFileSystem](const peanutbutter::DirectoryEntry& pLeft,
                                                                         const peanutbutter::DirectoryEntry& pRight) {
      return aFileSystem.FileName(pLeft.mPath) < aFileSystem.FileName(pRight.mPath);
    });
    if (aArchivePaths.empty()) {
      pError = "no archives found after bundle";
      return false;
    }
    aRequest.mRecoveryStartFilePath = aArchivePaths.front().mPath;
    aRequest.mDestinationDirectory = aFileSystem.JoinPath(aRoot, "recover");
    aRequest.mUseEncryption = false;
    return RequireSuccess(aCore.RunRecover(aRequest, DestinationAction::Clear), pError);
  });

  (void)aFileSystem.ClearDirectory(aRoot);
  return aOk ? 0 : 1;
}
