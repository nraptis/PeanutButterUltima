#include "Test_Execute_BundleAndUnbundle.hpp"

#include <sstream>

#include "Test_RecoveryPlan.hpp"
#include "Test_Utils.hpp"
#include "Validate_File.hpp"
#include "Validate_Manifest.hpp"

namespace peanutbutter::testing {

bool Execute_BundleAndUnbundle(peanutbutter::testing::MockFileSystem& pFileSystem,
                               peanutbutter::Crypt& pCrypt,
                               bool pUseEncryption,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage) {
  peanutbutter::CapturingLogger aLogger;
  peanutbutter::RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = kDemoPlainTextHeaderLength + (2 * kDemoL3Length);
  peanutbutter::ApplicationCore aCore(pFileSystem, pCrypt, aLogger, aSettings);

  peanutbutter::BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = pInputDirectory;
  aBundleRequest.mDestinationDirectory = pArchiveDirectory;
  aBundleRequest.mArchivePrefix = "archive_";
  aBundleRequest.mArchiveSuffix = ".PBTR";
  aBundleRequest.mUseEncryption = pUseEncryption;

  const peanutbutter::PreflightResult aBundlePreflight = aCore.CheckBundle(aBundleRequest);
  if (aBundlePreflight.mSignal == peanutbutter::PreflightSignal::RedLight) {
    return Fail("Execute_BundleAndUnbundle failed during bundle pre-check: " + aBundlePreflight.mMessage,
                pErrorMessage);
  }

  const peanutbutter::OperationResult aBundleResult =
      aCore.RunBundle(aBundleRequest, peanutbutter::DestinationAction::Clear);
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

  peanutbutter::UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mArchiveDirectory = pArchiveDirectory;
  aUnbundleRequest.mDestinationDirectory = pOutputDirectory;
  aUnbundleRequest.mUseEncryption = pUseEncryption;

  const peanutbutter::PreflightResult aUnbundlePreflight = aCore.CheckUnbundle(aUnbundleRequest);
  if (aUnbundlePreflight.mSignal == peanutbutter::PreflightSignal::RedLight) {
    return Fail("Execute_BundleAndUnbundle failed during unbundle pre-check: " + aUnbundlePreflight.mMessage,
                pErrorMessage);
  }

  const peanutbutter::OperationResult aUnbundleResult =
      aCore.RunUnbundle(aUnbundleRequest, peanutbutter::DestinationAction::Clear);
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
  if (!Validate_Manifest(pFileSystem, pInputDirectory, pOutputDirectory, pErrorMessage)) {
    if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
      *pErrorMessage = "Execute_BundleAndUnbundle failed after unbundle: " + *pErrorMessage;
    }
    return false;
  }

  return true;
}

bool Execute_BundleAndUnbundle(peanutbutter::testing::MockFileSystem& pFileSystem,
                               const std::string& pInputDirectory,
                               const std::string& pArchiveDirectory,
                               const std::string& pOutputDirectory,
                               std::string* pErrorMessage) {
  peanutbutter::PassthroughCrypt aCrypt;
  return Execute_BundleAndUnbundle(pFileSystem,
                                   aCrypt,
                                   false,
                                   pInputDirectory,
                                   pArchiveDirectory,
                                   pOutputDirectory,
                                   pErrorMessage);
}

}  // namespace peanutbutter::testing
