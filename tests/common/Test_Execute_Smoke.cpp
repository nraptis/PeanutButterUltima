#include "Test_Execute_Smoke.hpp"

#include <map>

#include "Test_Execute_Unbundle.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

bool CompareDirectoryToSeedFiles(FileSystem& pFileSystem,
                                 const std::string& pDirectory,
                                 const std::vector<TestSeedFile>& pExpectedFiles,
                                 std::string& pErrorMessage) {
  std::map<std::string, std::string> aExpectedByPath;
  for (const TestSeedFile& aSeed : pExpectedFiles) {
    aExpectedByPath[aSeed.mRelativePath] = aSeed.mContents;
  }

  const std::vector<DirectoryEntry> aActualEntries = pFileSystem.ListFilesRecursive(pDirectory);
  if (aActualEntries.size() != aExpectedByPath.size()) {
    pErrorMessage = "file count mismatch: expected " + std::to_string(aExpectedByPath.size()) +
                    ", actual " + std::to_string(aActualEntries.size()) + ".";
    return false;
  }

  for (const DirectoryEntry& aEntry : aActualEntries) {
    const auto aIt = aExpectedByPath.find(aEntry.mRelativePath);
    if (aIt == aExpectedByPath.end()) {
      pErrorMessage = "unexpected file in output: " + aEntry.mRelativePath;
      return false;
    }

    std::string aContents;
    if (!pFileSystem.ReadTextFile(aEntry.mPath, aContents)) {
      pErrorMessage = "could not read output file: " + aEntry.mRelativePath;
      return false;
    }
    if (aContents != aIt->second) {
      pErrorMessage = "content mismatch for file: " + aEntry.mRelativePath;
      return false;
    }
  }

  return true;
}

bool ExecuteBundleAndUnbundleSmoke(FileSystem& pFileSystem,
                                   ApplicationCore& pCore,
                                   const std::vector<TestSeedFile>& pSeedFiles,
                                   const BundleExecutionSpec& pBundleSpec,
                                   const std::string& pUnbundleDirectory,
                                   std::string& pErrorMessage) {
  const BundleExecutionResult aBundleResult =
      ExecuteBundleForSeed(pFileSystem, pCore, pSeedFiles, pBundleSpec);
  if (!aBundleResult.mSucceeded) {
    pErrorMessage = "healthy bundle failed: " + aBundleResult.mMessage;
    return false;
  }

  UnbundleExecutionSpec aUnbundleSpec;
  aUnbundleSpec.mArchiveDirectory = aBundleResult.mArchiveDirectory;
  aUnbundleSpec.mDestinationDirectory = pUnbundleDirectory;
  aUnbundleSpec.mAction = DestinationAction::Clear;
  const OperationResult aUnbundleResult = ExecuteUnbundle(pCore, aUnbundleSpec);
  if (!aUnbundleResult.mSucceeded) {
    pErrorMessage = "healthy unbundle failed: " + aUnbundleResult.mMessage;
    return false;
  }

  if (!CompareDirectoryToSeedFiles(pFileSystem, pUnbundleDirectory, pSeedFiles, pErrorMessage)) {
    pErrorMessage = "healthy unbundle tree mismatch: " + pErrorMessage;
    return false;
  }

  return true;
}

bool ExecuteRecoverSmoke(FileSystem& pFileSystem,
                         ApplicationCore& pCore,
                         const std::string& pArchiveDirectory,
                         const std::string& pRecoveryStartFilePath,
                         const std::string& pRecoverDirectory,
                         const std::vector<TestSeedFile>& pExpectedRecoveredFiles,
                         bool pUseEncryption,
                         bool pRequireDirectoryMatch,
                         std::string& pRecoverMessage,
                         std::string& pErrorMessage) {
  std::string aRecoveryStart = pRecoveryStartFilePath;
  if (aRecoveryStart.empty()) {
    std::vector<TestArchive> aArchives;
    if (!CollectTestArchives(pFileSystem, pArchiveDirectory, aArchives, pErrorMessage)) {
      pErrorMessage = "recover smoke could not collect archives: " + pErrorMessage;
      return false;
    }
    if (aArchives.empty()) {
      pErrorMessage = "recover smoke found no archives in directory.";
      return false;
    }
    aRecoveryStart = aArchives.front().mFilePath;
  }

  RecoverRequest aRecoverRequest;
  aRecoverRequest.mArchiveDirectory = pArchiveDirectory;
  aRecoverRequest.mRecoveryStartFilePath = aRecoveryStart;
  aRecoverRequest.mDestinationDirectory = pRecoverDirectory;
  aRecoverRequest.mUseEncryption = pUseEncryption;

  const OperationResult aRecoverResult = pCore.RunRecover(aRecoverRequest, DestinationAction::Clear);
  pRecoverMessage = aRecoverResult.mMessage;
  if (!aRecoverResult.mSucceeded) {
    pErrorMessage = "recover smoke failed: " + aRecoverResult.mMessage;
    return false;
  }

  if (pRequireDirectoryMatch) {
    if (!CompareDirectoryToSeedFiles(pFileSystem, pRecoverDirectory, pExpectedRecoveredFiles, pErrorMessage)) {
      pErrorMessage = "recover smoke tree mismatch: " + pErrorMessage;
      return false;
    }
  }

  return true;
}

bool ExecuteBundleAndRecoverSmoke(FileSystem& pFileSystem,
                                  ApplicationCore& pCore,
                                  const std::vector<TestSeedFile>& pSeedFiles,
                                  const BundleExecutionSpec& pBundleSpec,
                                  const std::string& pRecoverDirectory,
                                  const std::vector<TestSeedFile>& pExpectedRecoveredFiles,
                                  std::string& pRecoverMessage,
                                  std::string& pErrorMessage) {
  const BundleExecutionResult aBundleResult =
      ExecuteBundleForSeed(pFileSystem, pCore, pSeedFiles, pBundleSpec);
  if (!aBundleResult.mSucceeded) {
    pErrorMessage = "bundle stage failed: " + aBundleResult.mMessage;
    return false;
  }
  const std::string aRecoveryStart =
      aBundleResult.mArchivePaths.empty() ? std::string() : aBundleResult.mArchivePaths.front();
  return ExecuteRecoverSmoke(pFileSystem,
                             pCore,
                             aBundleResult.mArchiveDirectory,
                             aRecoveryStart,
                             pRecoverDirectory,
                             pExpectedRecoveredFiles,
                             pBundleSpec.mUseEncryption,
                             true,
                             pRecoverMessage,
                             pErrorMessage);
}

}  // namespace peanutbutter::testing
