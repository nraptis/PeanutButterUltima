#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_FENCE_EXTRAS_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_FENCE_EXTRAS_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppCore_Fences.hpp"
#include "FormatConstants.hpp"
#include "Test_Execute_Bundle.hpp"
#include "Test_Execute_FenceRoundTrip.hpp"
#include "Test_Utils.hpp"

namespace peanutbutter::testing {

struct FenceCaseBase {
  std::string mCaseName;
  std::vector<TestSeedFile> mInputFiles;
  std::string mExpectedErrorCode;
  std::vector<TestSeedFile> mRecoveryFiles;
  std::size_t mArchiveBlockCount = 1;
  std::size_t mMutationRecordIndex = 1;
  std::string mExpectedFenceFlag;
};

struct FileNameFenceCase : public FenceCaseBase {};
struct FolderNameFenceCase : public FenceCaseBase {};
struct FileContentFenceCase : public FenceCaseBase {};
struct RecoveryStrideFenceCase : public FenceCaseBase {};

struct LengthFenceProbeRequest {
  detail::FenceRule mRule = detail::FenceRule::kFileContentLength;
  detail::FenceCode mExpectedCode = detail::FenceCode::kFileContentLength;
  std::uint32_t mRequiredFlag = detail::kFenceNone;
  std::size_t mCursorLogicalOffset = 0;
  std::size_t mLogicalCapacityPerArchive = 0;
  std::size_t mArchiveBlockCount = 1;
  std::uint64_t mMaxDelta = 8192;
};

inline std::vector<TestSeedFile> SortSeedFiles(std::vector<TestSeedFile> pFiles) {
  std::sort(pFiles.begin(), pFiles.end(), [](const TestSeedFile& pLeft, const TestSeedFile& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });
  return pFiles;
}

inline std::size_t LogicalCapacityPerArchiveBlocks(std::size_t pArchiveBlockCount) {
  if (pArchiveBlockCount == 0u) {
    return 0u;
  }
  const std::size_t aArchivePayloadLength = pArchiveBlockCount * peanutbutter::SB_L3_LENGTH;
  const std::size_t aL1BlockCount = aArchivePayloadLength / peanutbutter::SB_L1_LENGTH;
  return aL1BlockCount * peanutbutter::SB_PAYLOAD_SIZE;
}

inline bool TryLengthWithFenceFlag(const LengthFenceProbeRequest& pRequest, std::uint64_t& pLengthOut) {
  if (pRequest.mArchiveBlockCount == 0u || pRequest.mCursorLogicalOffset >= pRequest.mLogicalCapacityPerArchive) {
    return false;
  }

  const std::size_t aBlockIndex = pRequest.mCursorLogicalOffset / peanutbutter::SB_PAYLOAD_SIZE;
  const std::size_t aOffsetInBlock = pRequest.mCursorLogicalOffset % peanutbutter::SB_PAYLOAD_SIZE;
  const std::size_t aCursorPhysicalOffset =
      (aBlockIndex * peanutbutter::SB_L1_LENGTH) + peanutbutter::SB_RECOVERY_HEADER_LENGTH + aOffsetInBlock;
  const std::size_t aReadableLogicalRemaining = pRequest.mLogicalCapacityPerArchive - pRequest.mCursorLogicalOffset;

  detail::ArchiveHeaderRecord aArchive;
  aArchive.mPayloadLength = static_cast<std::uint64_t>(pRequest.mArchiveBlockCount) * peanutbutter::SB_L3_LENGTH;
  const detail::FenceDomain aDomain = detail::BuildFenceDomain({aArchive});

  detail::FenceProbe aProbe;
  aProbe.mRule = pRequest.mRule;
  aProbe.mCursor.mArchiveIndex = 0;
  aProbe.mCursor.mPhysicalOffset = aCursorPhysicalOffset;

  for (std::uint64_t aDelta = 1; aDelta <= pRequest.mMaxDelta; ++aDelta) {
    aProbe.mValue = static_cast<std::uint64_t>(aReadableLogicalRemaining) + aDelta;
    detail::FenceViolation aViolation;
    if (!detail::FenceCheck(aDomain, aProbe, &aViolation)) {
      continue;
    }
    if (aViolation.mCode != pRequest.mExpectedCode) {
      continue;
    }
    if (pRequest.mRequiredFlag != detail::kFenceNone &&
        (aViolation.mFlags & pRequest.mRequiredFlag) != pRequest.mRequiredFlag) {
      continue;
    }

    pLengthOut = aProbe.mValue;
    return true;
  }
  return false;
}

inline FenceRoundTripOutcome ExecuteFileContentInRecoveryHeaderRoundTrip(const FileContentFenceCase& pCase) {
  if (pCase.mInputFiles.empty()) {
    FenceRoundTripOutcome aOutcome;
    aOutcome.mSucceeded = false;
    aOutcome.mFailureMessage = "test case has no input files.";
    return aOutcome;
  }
  if (pCase.mArchiveBlockCount == 0u) {
    FenceRoundTripOutcome aOutcome;
    aOutcome.mSucceeded = false;
    aOutcome.mFailureMessage = "archive block count must be greater than zero.";
    return aOutcome;
  }

  std::vector<TestSeedFile> aSortedFiles = SortSeedFiles(pCase.mInputFiles);
  if (pCase.mMutationRecordIndex >= aSortedFiles.size()) {
    FenceRoundTripOutcome aOutcome;
    aOutcome.mSucceeded = false;
    aOutcome.mFailureMessage = "mutation record index is outside file-tree range.";
    return aOutcome;
  }

  std::vector<TestSeedFile> aSortedRecoverable =
      pCase.mRecoveryFiles.empty() ? std::vector<TestSeedFile>() : SortSeedFiles(pCase.mRecoveryFiles);
  if (aSortedRecoverable.empty()) {
    for (std::size_t aIndex = 0; aIndex < pCase.mMutationRecordIndex; ++aIndex) {
      aSortedRecoverable.push_back(aSortedFiles[aIndex]);
    }
  }

  const std::size_t aRecordStart = RecordStartLogicalOffset(aSortedFiles, pCase.mMutationRecordIndex);
  const std::size_t aContentLengthLogicalOffset =
      aRecordStart + 2u + aSortedFiles[pCase.mMutationRecordIndex].mRelativePath.size();
  const std::size_t aCursorAfterContentLength = aContentLengthLogicalOffset + 6u;
  const std::size_t aLogicalCapacityPerArchive = LogicalCapacityPerArchiveBlocks(pCase.mArchiveBlockCount);

  std::uint64_t aMutatedContentLength = 0;
  LengthFenceProbeRequest aProbeRequest;
  aProbeRequest.mRule = detail::FenceRule::kFileContentLength;
  aProbeRequest.mExpectedCode = detail::FenceCode::kFileContentLength;
  aProbeRequest.mRequiredFlag = detail::kFenceInRecoveryHeader;
  aProbeRequest.mCursorLogicalOffset = aCursorAfterContentLength;
  aProbeRequest.mLogicalCapacityPerArchive = aLogicalCapacityPerArchive;
  aProbeRequest.mArchiveBlockCount = pCase.mArchiveBlockCount;
  if (!TryLengthWithFenceFlag(aProbeRequest, aMutatedContentLength)) {
    FenceRoundTripOutcome aOutcome;
    aOutcome.mSucceeded = false;
    aOutcome.mFailureMessage = "could not find a content-length mutation that lands in a recovery-header fence.";
    return aOutcome;
  }

  GenericArchiveDataMutation aMutation;
  aMutation.mArchiveIndex = static_cast<std::uint32_t>(aContentLengthLogicalOffset / aLogicalCapacityPerArchive);
  aMutation.mPayloadLogicalOffset = aContentLengthLogicalOffset % aLogicalCapacityPerArchive;
  aMutation.mBytes = EncodeLe6(aMutatedContentLength);

  FenceRoundTripSpec aSpec;
  aSpec.mCaseName = pCase.mCaseName;
  aSpec.mOriginalFiles = aSortedFiles;
  aSpec.mRecoverableFiles = std::move(aSortedRecoverable);
  aSpec.mArchiveBlockCount = pCase.mArchiveBlockCount;
  aSpec.mExpectedUnbundleErrorCode = pCase.mExpectedErrorCode;
  aSpec.mMutation.mArchiveDataMutations.push_back(std::move(aMutation));
  return ExecuteFenceTestRoundTrip(aSpec);
}

inline bool ValidateFenceRoundTripOutcome(const FenceCaseBase& pCase,
                                          const FenceRoundTripOutcome& pOutcome,
                                          std::string& pFailureMessage) {
  if (!pOutcome.mSucceeded) {
    pFailureMessage = pOutcome.mFailureMessage;
    return false;
  }
  if (!ContainsToken(pOutcome.mMutatedUnbundleMessage, pCase.mExpectedErrorCode) &&
      !ContainsToken(pOutcome.mCollectedLogs, pCase.mExpectedErrorCode)) {
    pFailureMessage = "missing expected error code in mutated unbundle result: " + pCase.mExpectedErrorCode;
    return false;
  }
  if (!pCase.mExpectedFenceFlag.empty() &&
      !ContainsToken(pOutcome.mMutatedUnbundleMessage, pCase.mExpectedFenceFlag) &&
      !ContainsToken(pOutcome.mCollectedLogs, pCase.mExpectedFenceFlag)) {
    pFailureMessage = "missing expected fence flag in mutated unbundle result: " + pCase.mExpectedFenceFlag;
    return false;
  }
  return true;
}

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_FENCE_EXTRAS_HPP_
