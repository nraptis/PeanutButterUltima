#include <iostream>

#include "Test_Execute_Recover.hpp"

int main() {
  using peanutbutter::testing::RecoverFileNameZeroScenario;
  using peanutbutter::testing::RecoverMutationOutcome;
  using peanutbutter::testing::TestSeedFile;

  RecoverFileNameZeroScenario aScenario;
  aScenario.mCaseName = "FileNameZeroRecoverTdd";
  aScenario.mArchiveBlockCount = 1;
  aScenario.mTargetFileRecordIndex = 1;
  aScenario.mSeedFiles = {
      TestSeedFile{"a.txt", "alpha-alpha-alpha"},
      TestSeedFile{"b.txt", "beta-beta-beta"},
  };

  const RecoverMutationOutcome aOutcome =
      peanutbutter::testing::ExecuteRecoverFileNameZeroMutationTest(aScenario);

  if (!aOutcome.mExecuted) {
    std::cerr << "[FAIL] executor did not run recover stage: " << aOutcome.mFailureMessage << "\n";
    std::cerr << aOutcome.mCollectedLogs;
    return 1;
  }

  if (!aOutcome.mObservedUnexpectedEndCode) {
    std::cerr << "[FAIL] expected UNP_EOF_003 after file-name-length->0 corruption.\n";
    std::cerr << "Recover message: " << aOutcome.mRecoverMessage << "\n";
    std::cerr << aOutcome.mCollectedLogs;
    return 1;
  }

  std::cout << "[PASS] observed UNP_EOF_003 for file-name-length->0 recover scenario.\n";
  return 0;
}
