#include "Test_IntegerFailureCaseCommon.hpp"

#include <algorithm>

#include "Test_Utils.hpp"

namespace peanutbutter::ultima::testing {

namespace {

bool RunSanityBundleAndUnbundle(MockFileSystem& pFileSystem, std::string* pErrorMessage) {
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(pFileSystem, aCrypt, aLogger, aSettings);

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";

  const OperationResult aBundleResult = aCore.RunBundle(aBundleRequest, DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    return Fail("Sanity check failed during bundle: " + aBundleResult.mMessage, pErrorMessage);
  }

  UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mArchiveDirectory = "/archives";
  aUnbundleRequest.mDestinationDirectory = "/output";

  const OperationResult aUnbundleResult = aCore.RunUnbundle(aUnbundleRequest, DestinationAction::Clear);
  if (!aUnbundleResult.mSucceeded) {
    return Fail("Sanity check failed during unbundle: " + aUnbundleResult.mMessage, pErrorMessage);
  }

  ValidateRequest aValidateRequest{"/input", "/output"};
  const OperationResult aValidateResult = aCore.RunValidate(aValidateRequest);
  if (!aValidateResult.mSucceeded) {
    return Fail("Sanity check failed during validate: " + aValidateResult.mMessage, pErrorMessage);
  }

  return true;
}

bool RunSanityBundleAndRecover(MockFileSystem& pFileSystem, std::string* pErrorMessage) {
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(pFileSystem, aCrypt, aLogger, aSettings);

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";

  const OperationResult aBundleResult = aCore.RunBundle(aBundleRequest, DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    return Fail("Sanity check failed during bundle: " + aBundleResult.mMessage, pErrorMessage);
  }

  std::vector<DirectoryEntry> aArchives = pFileSystem.ListFiles("/archives");
  std::sort(aArchives.begin(), aArchives.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) { return pLeft.mPath < pRight.mPath; });
  if (aArchives.empty()) {
    return Fail("Sanity check failed during recover: no archives were produced.", pErrorMessage);
  }

  RecoverRequest aRecoverRequest;
  aRecoverRequest.mArchiveDirectory = "/archives";
  aRecoverRequest.mDestinationDirectory = "/output";
  aRecoverRequest.mRecoveryStartFilePath = aArchives.front().mPath;

  const OperationResult aRecoverResult = aCore.RunRecover(aRecoverRequest, DestinationAction::Clear);
  if (!aRecoverResult.mSucceeded) {
    return Fail("Sanity check failed during recover: " + aRecoverResult.mMessage, pErrorMessage);
  }

  ValidateRequest aValidateRequest{"/input", "/output"};
  const OperationResult aValidateResult = aCore.RunValidate(aValidateRequest);
  if (!aValidateResult.mSucceeded) {
    return Fail("Sanity check failed during validate: " + aValidateResult.mMessage, pErrorMessage);
  }

  return true;
}

bool RebuildArchivesForMutation(MockFileSystem& pFileSystem, std::string* pErrorMessage) {
  pFileSystem.ClearDirectory("/archives");
  pFileSystem.ClearDirectory("/output");

  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(pFileSystem, aCrypt, aLogger, aSettings);

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = "/input";
  aBundleRequest.mDestinationDirectory = "/archives";
  aBundleRequest.mArchivePrefix = "bundle_";
  aBundleRequest.mArchiveSuffix = ".PBTR";

  const OperationResult aBundleResult = aCore.RunBundle(aBundleRequest, DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    return Fail("Mutation setup failed during bundle: " + aBundleResult.mMessage, pErrorMessage);
  }

  return true;
}

bool MutateFirstArchive(MockFileSystem& pFileSystem, const ArchiveMutator& pMutator, std::string* pErrorMessage) {
  std::vector<DirectoryEntry> aArchives = pFileSystem.ListFiles("/archives");
  std::sort(aArchives.begin(), aArchives.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) { return pLeft.mPath < pRight.mPath; });
  if (aArchives.empty()) {
    return Fail("Mutation setup failed: no archives were produced.", pErrorMessage);
  }

  ByteVector aBytes;
  if (!pFileSystem.ReadFile(aArchives.front().mPath, aBytes)) {
    return Fail("Mutation setup failed: could not read first archive.", pErrorMessage);
  }
  if (!pMutator(aBytes, pErrorMessage)) {
    return false;
  }
  if (!pFileSystem.WriteFile(aArchives.front().mPath, aBytes)) {
    return Fail("Mutation setup failed: could not rewrite first archive.", pErrorMessage);
  }
  return true;
}

}  // namespace

void SeedBasicIntegerFailureInputTree(MockFileSystem& pFileSystem) {
  pFileSystem.AddFile("/input/a.txt", ToBytes("alpha"));
  pFileSystem.AddFile("/input/b.txt", ToBytes("beta"));
}

void SeedMultiArchiveIntegerFailureInputTree(MockFileSystem& pFileSystem) {
  pFileSystem.AddFile("/input/a.txt", ToBytes("alpha"));
  pFileSystem.AddFile("/input/b.txt", ToBytes("beta"));
  pFileSystem.AddFile("/input/c.txt", ToBytes("gamma"));
  pFileSystem.AddFile("/input/d.txt", ToBytes("delta"));
}

bool RunUnbundleIntegerFailureCase(const std::string& pExpectedCode,
                                   const InputSeeder& pSeedInput,
                                   const ArchiveMutator& pMutator,
                                   std::string* pErrorMessage) {
  MockFileSystem aFileSystem;
  if (pSeedInput) {
    pSeedInput(aFileSystem);
  } else {
    SeedBasicIntegerFailureInputTree(aFileSystem);
  }

  if (!RunSanityBundleAndUnbundle(aFileSystem, pErrorMessage)) {
    return false;
  }
  if (!RebuildArchivesForMutation(aFileSystem, pErrorMessage)) {
    return false;
  }
  if (!MutateFirstArchive(aFileSystem, pMutator, pErrorMessage)) {
    return false;
  }

  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mArchiveDirectory = "/archives";
  aUnbundleRequest.mDestinationDirectory = "/output";

  const OperationResult aResult = aCore.RunUnbundle(aUnbundleRequest, DestinationAction::Clear);
  if (aResult.mSucceeded) {
    return Fail("Unbundle unexpectedly succeeded for integer-failure case " + pExpectedCode + ".", pErrorMessage);
  }
  if (aResult.mMessage.find(pExpectedCode) == std::string::npos) {
    return Fail("Unbundle did not report " + pExpectedCode + ": " + aResult.mMessage, pErrorMessage);
  }
  return true;
}

bool RunRecoverIntegerFailureCase(const std::string& pExpectedCode,
                                  const InputSeeder& pSeedInput,
                                  const ArchiveMutator& pMutator,
                                  std::string* pErrorMessage) {
  MockFileSystem aFileSystem;
  if (pSeedInput) {
    pSeedInput(aFileSystem);
  } else {
    SeedBasicIntegerFailureInputTree(aFileSystem);
  }

  if (!RunSanityBundleAndRecover(aFileSystem, pErrorMessage)) {
    return false;
  }
  if (!RebuildArchivesForMutation(aFileSystem, pErrorMessage)) {
    return false;
  }
  if (!MutateFirstArchive(aFileSystem, pMutator, pErrorMessage)) {
    return false;
  }

  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + peanutbutter::SB_L3_LENGTH;
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  std::vector<DirectoryEntry> aArchives = aFileSystem.ListFiles("/archives");
  std::sort(aArchives.begin(), aArchives.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) { return pLeft.mPath < pRight.mPath; });
  if (aArchives.empty()) {
    return Fail("Recover integer-failure case setup failed: no archives were produced.", pErrorMessage);
  }

  RecoverRequest aRecoverRequest;
  aRecoverRequest.mArchiveDirectory = "/archives";
  aRecoverRequest.mDestinationDirectory = "/output";
  aRecoverRequest.mRecoveryStartFilePath = aArchives.front().mPath;

  const OperationResult aResult = aCore.RunRecover(aRecoverRequest, DestinationAction::Clear);
  if (aResult.mSucceeded) {
    return Fail("Recover unexpectedly succeeded for integer-failure case " + pExpectedCode + ".", pErrorMessage);
  }
  if (aResult.mMessage.find(pExpectedCode) == std::string::npos) {
    return Fail("Recover did not report " + pExpectedCode + ": " + aResult.mMessage, pErrorMessage);
  }
  return true;
}

}  // namespace peanutbutter::ultima::testing
