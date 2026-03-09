#include "Test_Execute_BundleAndUnbundle.hpp"

#include <sstream>

#include "Test_RecoveryPlan.hpp"
#include "Test_Utils.hpp"
#include "Validate_File.hpp"

namespace peanutbutter::ultima::testing {

bool Execute_BundleAndUnbundle(peanutbutter::ultima::testing::MockFileSystem& pFileSystem,
                               peanutbutter::ultima::Crypt& pCrypt,
                               bool pUseEncryption,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage) {
  peanutbutter::ultima::CapturingLogger aLogger;
  peanutbutter::ultima::RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = kDemoPlainTextHeaderLength + (2 * kDemoL3Length);
  peanutbutter::ultima::ApplicationCore aCore(pFileSystem, pCrypt, aLogger, aSettings);

  peanutbutter::ultima::BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = pInputDirectory;
  aBundleRequest.mDestinationDirectory = pArchiveDirectory;
  aBundleRequest.mArchivePrefix = "archive_";
  aBundleRequest.mArchiveSuffix = ".PBTR";
  aBundleRequest.mUseEncryption = pUseEncryption;

  const peanutbutter::ultima::PreflightResult aBundlePreflight = aCore.CheckBundle(aBundleRequest);
  if (aBundlePreflight.mSignal == peanutbutter::ultima::PreflightSignal::RedLight) {
    return Fail("Execute_BundleAndUnbundle failed during bundle pre-check: " + aBundlePreflight.mMessage,
                pErrorMessage);
  }

  const peanutbutter::ultima::OperationResult aBundleResult =
      aCore.RunBundle(aBundleRequest, peanutbutter::ultima::DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    return Fail("Execute_BundleAndUnbundle failed during bundle: " + aBundleResult.mMessage, pErrorMessage);
  }

  if (pFileSystem.ListFiles(pArchiveDirectory).empty()) {
    return Fail("Execute_BundleAndUnbundle failed: bundle produced no archives.", pErrorMessage);
  }

  std::vector<unsigned long long> aExpectedRecoveryHeaders;
  if (!GenerateAllRecoveryHeaders(pFileSystem,
                                  pArchiveDirectory,
                                  aExpectedRecoveryHeaders,
                                  &pCrypt,
                                  pUseEncryption,
                                  pErrorMessage)) {
    return false;
  }

  std::vector<unsigned long long> aCollectedRecoveryHeaders;
  if (!CollectAllRecoveryHeaders(pFileSystem,
                                 pArchiveDirectory,
                                 aCollectedRecoveryHeaders,
                                 &pCrypt,
                                 pUseEncryption,
                                 pErrorMessage)) {
    return false;
  }

  if (aExpectedRecoveryHeaders.size() != aCollectedRecoveryHeaders.size()) {
    std::ostringstream aStream;
    aStream << "Execute_BundleAndUnbundle failed: recovery header count mismatch. expected="
            << aExpectedRecoveryHeaders.size() << " actual=" << aCollectedRecoveryHeaders.size();
    return Fail(aStream.str(), pErrorMessage);
  }

  for (std::size_t aIndex = 1; aIndex < aExpectedRecoveryHeaders.size(); ++aIndex) {
    if (aExpectedRecoveryHeaders[aIndex] != aCollectedRecoveryHeaders[aIndex]) {
      std::ostringstream aStream;
      aStream << "Execute_BundleAndUnbundle failed: bad recovery plan at index "
              << aIndex << ". expected=" << aExpectedRecoveryHeaders[aIndex]
              << " actual=" << aCollectedRecoveryHeaders[aIndex];
      return Fail(aStream.str(), pErrorMessage);
    }
  }

  peanutbutter::ultima::UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mArchiveDirectory = pArchiveDirectory;
  aUnbundleRequest.mDestinationDirectory = pOutputDirectory;
  aUnbundleRequest.mUseEncryption = pUseEncryption;

  const peanutbutter::ultima::PreflightResult aUnbundlePreflight = aCore.CheckUnbundle(aUnbundleRequest);
  if (aUnbundlePreflight.mSignal == peanutbutter::ultima::PreflightSignal::RedLight) {
    return Fail("Execute_BundleAndUnbundle failed during unbundle pre-check: " + aUnbundlePreflight.mMessage,
                pErrorMessage);
  }

  const peanutbutter::ultima::OperationResult aUnbundleResult =
      aCore.RunUnbundle(aUnbundleRequest, peanutbutter::ultima::DestinationAction::Clear);
  if (!aUnbundleResult.mSucceeded) {
    return Fail("Execute_BundleAndUnbundle failed during unbundle: " + aUnbundleResult.mMessage, pErrorMessage);
  }

  std::vector<TestFile> aInputFiles;
  if (!CollectFiles(pFileSystem, pInputDirectory, aInputFiles, pErrorMessage)) {
    return false;
  }

  std::vector<TestFile> aOutputFiles;
  if (!CollectFiles(pFileSystem, pOutputDirectory, aOutputFiles, pErrorMessage)) {
    return false;
  }

  if (!Validate_Files(aInputFiles, aOutputFiles, pErrorMessage)) {
    if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
      *pErrorMessage = "Execute_BundleAndUnbundle failed after unbundle: " + *pErrorMessage;
    }
    return false;
  }

  return true;
}

bool Execute_BundleAndUnbundle(peanutbutter::ultima::testing::MockFileSystem& pFileSystem,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage) {
  peanutbutter::ultima::PassthroughCrypt aCrypt;
  return Execute_BundleAndUnbundle(pFileSystem,
                                   aCrypt,
                                   false,
                                   pInputDirectory,
                                   pArchiveDirectory,
                                   pOutputDirectory,
                                   pErrorMessage);
}

}  // namespace peanutbutter::ultima::testing
