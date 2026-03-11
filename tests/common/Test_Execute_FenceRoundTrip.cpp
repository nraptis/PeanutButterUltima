#include "Test_Execute_FenceRoundTrip.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "AppCore.hpp"
#include "AppCore_Helpers.hpp"
#include "Encryption/Crypt.hpp"
#include "FormatConstants.hpp"
#include "MockFileSystem.hpp"
#include "Test_Execute_Smoke.hpp"
#include "Test_Execute_Unbundle.hpp"
#include "Test_Utils.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {
namespace {

void WriteArchiveHeaderBytes(TestArchive& pArchive) {
  unsigned char aHeaderBytes[peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH] = {};
  detail::WriteArchiveHeaderBytes(pArchive.mArchiveHeader.mData, aHeaderBytes);
  std::copy(aHeaderBytes,
            aHeaderBytes + peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH,
            pArchive.mData.begin());
}

bool ApplyArchiveDataMutations(std::vector<TestArchive>& pArchives,
                               const std::vector<GenericArchiveDataMutation>& pMutations,
                               std::string& pErrorMessage) {
  for (const GenericArchiveDataMutation& aMutation : pMutations) {
    auto aIt = std::find_if(pArchives.begin(), pArchives.end(), [&aMutation](const TestArchive& pArchive) {
      return pArchive.mArchiveHeader.mData.mArchiveIndex == aMutation.mArchiveIndex;
    });
    if (aIt == pArchives.end()) {
      pErrorMessage = "archive data mutation referenced missing archive index " +
                      std::to_string(aMutation.mArchiveIndex) + ".";
      return false;
    }
    if (aMutation.mBytes.empty()) {
      continue;
    }

    TestArchive& aArchive = *aIt;
    const std::size_t aPayloadAvailable =
        (aArchive.mData.size() > peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH)
            ? (aArchive.mData.size() - peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH)
            : 0;
    const std::size_t aPayloadLength = std::min<std::size_t>(
        static_cast<std::size_t>(aArchive.mArchiveHeader.mData.mPayloadLength), aPayloadAvailable);
    const std::size_t aLogicalCapacity = peanutbutter::detail::LogicalCapacityForPhysicalLength(aPayloadLength);
    // Backward-compatibility: older generated cases may encode archive-global logical offsets.
    const std::size_t aNormalizedLogicalOffset =
        (aLogicalCapacity > 0) ? (aMutation.mPayloadLogicalOffset % aLogicalCapacity) : aMutation.mPayloadLogicalOffset;

    std::set<std::size_t> aTouchedBlocks;
    for (std::size_t aByteIndex = 0; aByteIndex < aMutation.mBytes.size(); ++aByteIndex) {
      const std::size_t aLogicalOffset = aNormalizedLogicalOffset + aByteIndex;
      const std::size_t aBlockIndex = aLogicalOffset / peanutbutter::SB_PAYLOAD_SIZE;
      const std::size_t aInBlockPayload = aLogicalOffset % peanutbutter::SB_PAYLOAD_SIZE;
      const std::size_t aPhysicalPayloadOffset =
          (aBlockIndex * peanutbutter::SB_L1_LENGTH) + peanutbutter::SB_RECOVERY_HEADER_LENGTH + aInBlockPayload;
      const std::size_t aFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aPhysicalPayloadOffset;
      if (aFileOffset >= aArchive.mData.size()) {
        pErrorMessage = "archive data mutation exceeded archive bounds at archive index " +
                        std::to_string(aMutation.mArchiveIndex) + ".";
        return false;
      }
      aArchive.mData[aFileOffset] = aMutation.mBytes[aByteIndex];
      aTouchedBlocks.insert(aBlockIndex);
    }

    for (std::size_t aBlockIndex : aTouchedBlocks) {
      const std::size_t aBlockPayloadOffset = aBlockIndex * peanutbutter::SB_L1_LENGTH;
      const std::size_t aBlockFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aBlockPayloadOffset;
      if (aBlockFileOffset + peanutbutter::SB_L1_LENGTH > aArchive.mData.size()) {
        continue;
      }
      unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
      detail::GenerateChecksum(aArchive.mData.data() + aBlockFileOffset, aChecksum);
      std::copy(aChecksum,
                aChecksum + peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH,
                aArchive.mData.begin() + static_cast<std::ptrdiff_t>(aBlockFileOffset));
    }
  }
  return true;
}

bool ApplyArchiveByteMutations(std::vector<TestArchive>& pArchives,
                               const std::vector<GenericArchiveByteMutation>& pMutations,
                               std::string& pErrorMessage) {
  for (const GenericArchiveByteMutation& aMutation : pMutations) {
    auto aIt = std::find_if(pArchives.begin(), pArchives.end(), [&aMutation](const TestArchive& pArchive) {
      return pArchive.mArchiveHeader.mData.mArchiveIndex == aMutation.mArchiveIndex;
    });
    if (aIt == pArchives.end()) {
      pErrorMessage = "archive byte mutation referenced missing archive index " +
                      std::to_string(aMutation.mArchiveIndex) + ".";
      return false;
    }
    if (aMutation.mBytes.empty()) {
      continue;
    }

    TestArchive& aArchive = *aIt;
    std::set<std::size_t> aTouchedBlocks;
    for (std::size_t aByteIndex = 0; aByteIndex < aMutation.mBytes.size(); ++aByteIndex) {
      const std::size_t aFileOffset = aMutation.mFileOffset + aByteIndex;
      if (aFileOffset >= aArchive.mData.size()) {
        pErrorMessage = "archive byte mutation exceeded archive bounds at archive index " +
                        std::to_string(aMutation.mArchiveIndex) + ".";
        return false;
      }
      aArchive.mData[aFileOffset] = aMutation.mBytes[aByteIndex];

      if (aFileOffset < peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH) {
        continue;
      }
      const std::size_t aPayloadOffset = aFileOffset - peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
      const std::size_t aBlockIndex = aPayloadOffset / peanutbutter::SB_L1_LENGTH;
      aTouchedBlocks.insert(aBlockIndex);
    }

    for (std::size_t aBlockIndex : aTouchedBlocks) {
      const std::size_t aBlockPayloadOffset = aBlockIndex * peanutbutter::SB_L1_LENGTH;
      const std::size_t aBlockFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aBlockPayloadOffset;
      if (aBlockFileOffset + peanutbutter::SB_L1_LENGTH > aArchive.mData.size()) {
        continue;
      }
      unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
      detail::GenerateChecksum(aArchive.mData.data() + aBlockFileOffset, aChecksum);
      std::copy(aChecksum,
                aChecksum + peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH,
                aArchive.mData.begin() + static_cast<std::ptrdiff_t>(aBlockFileOffset));
    }
  }
  return true;
}

bool ApplyArchiveSetMutations(FileSystem& pFileSystem,
                              const std::string& pArchiveDirectory,
                              std::vector<TestArchive>& pArchives,
                              const GenericArchiveSetMutation& pMutation,
                              std::string& pErrorMessage) {
  if (!pMutation.mRemoveArchiveIndices.empty()) {
    const std::set<std::uint32_t> aRemoveSet(pMutation.mRemoveArchiveIndices.begin(),
                                             pMutation.mRemoveArchiveIndices.end());
    pArchives.erase(std::remove_if(pArchives.begin(),
                                   pArchives.end(),
                                   [&aRemoveSet](const TestArchive& pArchive) {
                                     return aRemoveSet.find(pArchive.mArchiveHeader.mData.mArchiveIndex) != aRemoveSet.end();
                                   }),
                    pArchives.end());
  }

  if (!pMutation.mCreateArchiveIndices.empty()) {
    if (pArchives.empty()) {
      pErrorMessage = "cannot create synthetic archives when no template archive exists.";
      return false;
    }

    const TestArchive aTemplate = pArchives.front();
    const std::string aTemplateExtension = pFileSystem.Extension(aTemplate.mFilePath).empty()
                                               ? ".pb"
                                               : pFileSystem.Extension(aTemplate.mFilePath);
    for (std::uint32_t aCreateIndex : pMutation.mCreateArchiveIndices) {
      const bool aExists = std::any_of(pArchives.begin(), pArchives.end(), [aCreateIndex](const TestArchive& pArchive) {
        return pArchive.mArchiveHeader.mData.mArchiveIndex == aCreateIndex;
      });
      if (aExists) {
        continue;
      }

      TestArchive aSynthetic = aTemplate;
      aSynthetic.mArchiveHeader.mData.mArchiveIndex = aCreateIndex;
      aSynthetic.mFilePath = pFileSystem.JoinPath(
          pArchiveDirectory, "mutation_added_" + std::to_string(aCreateIndex) + aTemplateExtension);
      WriteArchiveHeaderBytes(aSynthetic);
      pArchives.push_back(std::move(aSynthetic));
    }
  }

  if (pArchives.empty()) {
    pErrorMessage = "archive set mutation removed all archives.";
    return false;
  }

  std::uint32_t aMaxArchiveIndex = 0;
  for (const TestArchive& aArchive : pArchives) {
    aMaxArchiveIndex = std::max(aMaxArchiveIndex, aArchive.mArchiveHeader.mData.mArchiveIndex);
  }
  const std::uint32_t aArchiveCount = aMaxArchiveIndex + 1u;
  for (TestArchive& aArchive : pArchives) {
    aArchive.mArchiveHeader.mData.mArchiveCount =
        std::max(aArchive.mArchiveHeader.mData.mArchiveCount, aArchiveCount);
    WriteArchiveHeaderBytes(aArchive);
  }
  return true;
}

bool RewriteArchiveDirectory(FileSystem& pFileSystem,
                             const std::string& pArchiveDirectory,
                             std::vector<TestArchive>& pArchives,
                             std::vector<std::string>& pArchivePaths,
                             std::string& pErrorMessage) {
  std::sort(pArchives.begin(), pArchives.end(), [&pFileSystem](const TestArchive& pLeft, const TestArchive& pRight) {
    if (pLeft.mArchiveHeader.mData.mArchiveIndex != pRight.mArchiveHeader.mData.mArchiveIndex) {
      return pLeft.mArchiveHeader.mData.mArchiveIndex < pRight.mArchiveHeader.mData.mArchiveIndex;
    }
    return pFileSystem.FileName(pLeft.mFilePath) < pFileSystem.FileName(pRight.mFilePath);
  });

  if (!pFileSystem.ClearDirectory(pArchiveDirectory) || !pFileSystem.EnsureDirectory(pArchiveDirectory)) {
    pErrorMessage = "could not reset archive directory during mutation.";
    return false;
  }

  pArchivePaths.clear();
  for (TestArchive& aArchive : pArchives) {
    const std::string aTargetPath =
        pFileSystem.JoinPath(pArchiveDirectory, pFileSystem.FileName(aArchive.mFilePath));
    if (!pFileSystem.WriteFile(aTargetPath, aArchive.mData)) {
      pErrorMessage = "could not write mutated archive: " + pFileSystem.FileName(aTargetPath);
      return false;
    }
    aArchive.mFilePath = aTargetPath;
    pArchivePaths.push_back(aTargetPath);
  }
  return true;
}

bool ApplyGenericMutationToArchiveDirectory(FileSystem& pFileSystem,
                                            const std::string& pArchiveDirectory,
                                            const GenericMutation& pMutation,
                                            std::vector<std::string>& pArchivePaths,
                                            std::string& pErrorMessage) {
  std::vector<TestArchive> aArchives;
  if (!CollectTestArchives(pFileSystem, pArchiveDirectory, aArchives, pErrorMessage)) {
    return false;
  }
  if (!ApplyArchiveDataMutations(aArchives, pMutation.mArchiveDataMutations, pErrorMessage)) {
    return false;
  }
  if (!ApplyArchiveByteMutations(aArchives, pMutation.mArchiveByteMutations, pErrorMessage)) {
    return false;
  }
  if (!ApplyArchiveSetMutations(pFileSystem, pArchiveDirectory, aArchives, pMutation.mArchiveSetMutation, pErrorMessage)) {
    return false;
  }
  return RewriteArchiveDirectory(pFileSystem, pArchiveDirectory, aArchives, pArchivePaths, pErrorMessage);
}

}  // namespace

FenceRoundTripOutcome ExecuteFenceTestRoundTrip(const FenceRoundTripSpec& pSpec) {
  FenceRoundTripOutcome aOutcome;
  if (pSpec.mOriginalFiles.empty()) {
    aOutcome.mFailureMessage = "round-trip spec has no original files.";
    return aOutcome;
  }

  MockFileSystem aFileSystem;
  PassthroughCrypt aCrypt;
  CapturingLogger aLogger;
  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength =
      peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * pSpec.mArchiveBlockCount);
  ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  const std::string aRuntimeRoot = aFileSystem.JoinPath(
      aFileSystem.CurrentWorkingDirectory(),
      aFileSystem.JoinPath("tests/runtime", pSpec.mCaseName));

  BundleExecutionSpec aHealthyBundleSpec;
  aHealthyBundleSpec.mRuntimeRoot = aRuntimeRoot;
  aHealthyBundleSpec.mInputDirectoryName = "healthy_input";
  aHealthyBundleSpec.mArchiveDirectoryName = "healthy_archives";
  aHealthyBundleSpec.mArchivePrefix = pSpec.mArchivePrefix;
  aHealthyBundleSpec.mArchiveSuffix = pSpec.mArchiveSuffix;
  aHealthyBundleSpec.mArchiveBlockCount = pSpec.mArchiveBlockCount;
  aHealthyBundleSpec.mUseEncryption = pSpec.mUseEncryption;
  aHealthyBundleSpec.mSeedEmptyDirectories = pSpec.mOriginalEmptyDirectories;
  const std::string aHealthyUnbundleDirectory = aFileSystem.JoinPath(aRuntimeRoot, "healthy_unbundle");

  std::string aErrorMessage;
  if (!ExecuteBundleAndUnbundleSmoke(
          aFileSystem, aCore, pSpec.mOriginalFiles, aHealthyBundleSpec, aHealthyUnbundleDirectory, aErrorMessage)) {
    aOutcome.mFailureMessage = aErrorMessage;
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  BundleExecutionSpec aMutatedBundleSpec;
  aMutatedBundleSpec.mRuntimeRoot = aRuntimeRoot;
  aMutatedBundleSpec.mInputDirectoryName = "mut_input";
  aMutatedBundleSpec.mArchiveDirectoryName = "mut_archives";
  aMutatedBundleSpec.mArchivePrefix = pSpec.mArchivePrefix;
  aMutatedBundleSpec.mArchiveSuffix = pSpec.mArchiveSuffix;
  aMutatedBundleSpec.mArchiveBlockCount = pSpec.mArchiveBlockCount;
  aMutatedBundleSpec.mUseEncryption = pSpec.mUseEncryption;
  aMutatedBundleSpec.mSeedEmptyDirectories = pSpec.mOriginalEmptyDirectories;

  const BundleExecutionResult aMutatedBundleResult =
      ExecuteBundleForSeed(aFileSystem, aCore, pSpec.mOriginalFiles, aMutatedBundleSpec);
  if (!aMutatedBundleResult.mSucceeded) {
    aOutcome.mFailureMessage = "mutated bundle stage failed: " + aMutatedBundleResult.mMessage;
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  std::vector<std::string> aMutatedArchivePaths;
  if (!ApplyGenericMutationToArchiveDirectory(aFileSystem,
                                              aMutatedBundleResult.mArchiveDirectory,
                                              pSpec.mMutation,
                                              aMutatedArchivePaths,
                                              aErrorMessage)) {
    aOutcome.mFailureMessage = "mutation stage failed: " + aErrorMessage;
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  const std::string aMutatedUnbundleDirectory = aFileSystem.JoinPath(aRuntimeRoot, "mut_unbundle");
  UnbundleExecutionSpec aMutUnbundleSpec;
  aMutUnbundleSpec.mArchiveDirectory = aMutatedBundleResult.mArchiveDirectory;
  aMutUnbundleSpec.mDestinationDirectory = aMutatedUnbundleDirectory;
  aMutUnbundleSpec.mUseEncryption = pSpec.mUseEncryption;
  aMutUnbundleSpec.mAction = DestinationAction::Clear;
  const OperationResult aMutatedUnbundleResult = ExecuteUnbundle(aCore, aMutUnbundleSpec);
  aOutcome.mMutatedUnbundleMessage = aMutatedUnbundleResult.mMessage;

  if (pSpec.mExpectMutatedUnbundleFailure && aMutatedUnbundleResult.mSucceeded) {
    aOutcome.mFailureMessage = "mutated unbundle unexpectedly succeeded.";
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }
  if (pSpec.mExpectMutatedUnbundleFailure && !pSpec.mExpectedUnbundleErrorCode.empty()) {
    const std::string aLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    if (!ContainsToken(aMutatedUnbundleResult.mMessage, pSpec.mExpectedUnbundleErrorCode) &&
        !ContainsToken(aLogs, pSpec.mExpectedUnbundleErrorCode)) {
      aOutcome.mFailureMessage = "mutated unbundle did not surface expected error code " +
                                 pSpec.mExpectedUnbundleErrorCode + ".";
      aOutcome.mCollectedLogs = aLogs;
      return aOutcome;
    }
  }

  if (!pSpec.mRunRecoverAfterMutation) {
    aOutcome.mSucceeded = true;
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  if (aMutatedArchivePaths.empty()) {
    aOutcome.mFailureMessage = "mutation stage produced no archives for recover walk.";
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  const std::string aRecoverDirectory = aFileSystem.JoinPath(aRuntimeRoot, "mut_recover");
  std::string aRecoverMessage;
  if (!ExecuteRecoverSmoke(aFileSystem,
                           aCore,
                           aMutatedBundleResult.mArchiveDirectory,
                           aMutatedArchivePaths.front(),
                           aRecoverDirectory,
                           pSpec.mRecoverableFiles,
                           pSpec.mUseEncryption,
                           pSpec.mRequireRecoverTreeMatch,
                           aRecoverMessage,
                           aErrorMessage)) {
    aOutcome.mFailureMessage = aErrorMessage;
    aOutcome.mMutatedRecoverMessage = aRecoverMessage;
    aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
    return aOutcome;
  }

  aOutcome.mMutatedRecoverMessage = aRecoverMessage;
  aOutcome.mSucceeded = true;
  aOutcome.mCollectedLogs = JoinLogLines(aLogger.StatusMessages(), aLogger.ErrorMessages());
  return aOutcome;
}

}  // namespace peanutbutter::testing
