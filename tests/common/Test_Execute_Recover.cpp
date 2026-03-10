#include "Test_Execute_Recover.hpp"

#include <algorithm>
#include <cstdint>

#include "FormatConstants.hpp"
#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_Utils.hpp"

namespace peanutbutter::testing {

RecoverMutationOutcome ExecuteRecoverFileNameZeroMutationTest(const RecoverFileNameZeroScenario& pScenario) {
  RecoverMutationOutcome aOutcome;

  if (pScenario.mSeedFiles.empty()) {
    aOutcome.mFailureMessage = "scenario has no seed files.";
    return aOutcome;
  }
  if (pScenario.mTargetFileRecordIndex >= pScenario.mSeedFiles.size()) {
    aOutcome.mFailureMessage = "target file record index is outside seed file range.";
    return aOutcome;
  }

  std::vector<TestSeedFile> aSortedSeedFiles = pScenario.mSeedFiles;
  std::sort(aSortedSeedFiles.begin(), aSortedSeedFiles.end(), [](const TestSeedFile& pLeft, const TestSeedFile& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });

  const std::size_t aTargetLogicalOffset =
      RecordStartLogicalOffset(aSortedSeedFiles, pScenario.mTargetFileRecordIndex);

  const std::size_t aLogicalCapacityPerArchive =
      (peanutbutter::SB_L3_LENGTH / peanutbutter::SB_L1_LENGTH) * peanutbutter::SB_PAYLOAD_SIZE;
  const std::size_t aTargetArchiveOffset = aTargetLogicalOffset / aLogicalCapacityPerArchive;
  const std::size_t aTargetArchiveLogicalOffset = aTargetLogicalOffset % aLogicalCapacityPerArchive;

  FenceRoundTripSpec aRoundTripSpec;
  aRoundTripSpec.mCaseName = pScenario.mCaseName;
  aRoundTripSpec.mOriginalFiles = aSortedSeedFiles;
  aRoundTripSpec.mRecoverableFiles = {aSortedSeedFiles.front()};
  aRoundTripSpec.mArchiveBlockCount = pScenario.mArchiveBlockCount;
  aRoundTripSpec.mArchivePrefix = pScenario.mArchivePrefix;
  aRoundTripSpec.mArchiveSuffix = pScenario.mArchiveSuffix;
  aRoundTripSpec.mExpectMutatedUnbundleFailure = false;

  GenericArchiveDataMutation aDataMutation;
  aDataMutation.mArchiveIndex = static_cast<std::uint32_t>(aTargetArchiveOffset);
  aDataMutation.mPayloadLogicalOffset = aTargetArchiveLogicalOffset;
  aDataMutation.mBytes = {0u, 0u};
  aRoundTripSpec.mMutation.mArchiveDataMutations.push_back(std::move(aDataMutation));

  const FenceRoundTripOutcome aRoundTripOutcome = ExecuteFenceTestRoundTrip(aRoundTripSpec);
  aOutcome.mExecuted = true;
  aOutcome.mRecoverSucceeded = aRoundTripOutcome.mSucceeded;
  aOutcome.mRecoverMessage = aRoundTripOutcome.mMutatedRecoverMessage;
  aOutcome.mCollectedLogs = aRoundTripOutcome.mCollectedLogs;
  aOutcome.mObservedUnexpectedEndCode =
      ContainsToken(aRoundTripOutcome.mMutatedRecoverMessage, "UNP_EOF_003") ||
      ContainsToken(aRoundTripOutcome.mCollectedLogs, "UNP_EOF_003");
  if (!aRoundTripOutcome.mSucceeded && aOutcome.mFailureMessage.empty()) {
    aOutcome.mFailureMessage = aRoundTripOutcome.mFailureMessage;
  }
  return aOutcome;
}

}  // namespace peanutbutter::testing
