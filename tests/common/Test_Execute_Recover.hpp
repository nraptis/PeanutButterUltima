#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_RECOVER_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_RECOVER_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "Test_Execute_Bundle.hpp"

namespace peanutbutter::testing {

struct RecoverFileNameZeroScenario {
  std::string mCaseName = "FileNameZeroMidPayload";
  std::vector<TestSeedFile> mSeedFiles;
  std::size_t mTargetFileRecordIndex = 1;
  std::size_t mArchiveBlockCount = 1;
  std::string mArchivePrefix = "tdd_";
  std::string mArchiveSuffix = "pb";
};

struct RecoverMutationOutcome {
  bool mExecuted = false;
  bool mRecoverSucceeded = false;
  bool mObservedUnexpectedEndCode = false;
  std::string mFailureMessage;
  std::string mRecoverMessage;
  std::string mCollectedLogs;
};

RecoverMutationOutcome ExecuteRecoverFileNameZeroMutationTest(const RecoverFileNameZeroScenario& pScenario);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_RECOVER_HPP_
