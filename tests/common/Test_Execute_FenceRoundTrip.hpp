#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_FENCE_ROUND_TRIP_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_FENCE_ROUND_TRIP_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "Test_Execute_Bundle.hpp"

namespace peanutbutter::testing {

struct GenericArchiveDataMutation {
  std::uint32_t mArchiveIndex = 0;
  std::size_t mPayloadLogicalOffset = 0;
  std::vector<unsigned char> mBytes;
};

struct GenericArchiveByteMutation {
  std::uint32_t mArchiveIndex = 0;
  std::size_t mFileOffset = 0;
  std::vector<unsigned char> mBytes;
};

struct GenericArchiveSetMutation {
  std::vector<std::uint32_t> mCreateArchiveIndices;
  std::vector<std::uint32_t> mRemoveArchiveIndices;
};

struct GenericMutation {
  std::vector<GenericArchiveDataMutation> mArchiveDataMutations;
  std::vector<GenericArchiveByteMutation> mArchiveByteMutations;
  GenericArchiveSetMutation mArchiveSetMutation;
};

struct FenceRoundTripSpec {
  std::string mCaseName = "FenceRoundTrip";
  std::vector<TestSeedFile> mOriginalFiles;
  std::vector<std::string> mOriginalEmptyDirectories;
  GenericMutation mMutation;
  std::vector<TestSeedFile> mRecoverableFiles;

  std::size_t mArchiveBlockCount = 1;
  std::string mArchivePrefix = "tdd_";
  std::string mArchiveSuffix = "pb";
  bool mUseEncryption = false;

  bool mExpectMutatedUnbundleFailure = true;
  std::string mExpectedUnbundleErrorCode;
  bool mRunRecoverAfterMutation = true;
  bool mRequireRecoverTreeMatch = true;
};

struct FenceRoundTripOutcome {
  bool mSucceeded = false;
  std::string mFailureMessage;
  std::string mMutatedUnbundleMessage;
  std::string mMutatedRecoverMessage;
  std::string mCollectedLogs;
};

FenceRoundTripOutcome ExecuteFenceTestRoundTrip(const FenceRoundTripSpec& pSpec);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_FENCE_ROUND_TRIP_HPP_
