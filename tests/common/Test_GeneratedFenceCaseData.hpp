#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_GENERATED_FENCE_CASE_DATA_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_GENERATED_FENCE_CASE_DATA_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Test_Execute_Bundle.hpp"

namespace peanutbutter::testing {

struct GeneratedFenceCaseData {
  std::string mCaseId;
  std::string mFlow;
  std::string mFieldKind;
  std::string mExpectedErrorCode;
  std::string mExpectedFenceFlag;
  std::vector<std::string> mForbiddenFenceFlags;
  std::size_t mArchiveBlockCount = 1;
  std::vector<TestSeedFile> mInputFiles;
  std::vector<std::string> mInputEmptyDirs;
  std::vector<TestSeedFile> mRecoverableFiles;
  std::uint32_t mArchiveIndex = 0;
  std::size_t mMutationFileOffset = 0;
  std::int64_t mMutationPayloadLogicalOffset = -1;
  std::vector<unsigned char> mMutationBytes;
  std::vector<std::uint32_t> mCreateArchiveIndices;
  std::vector<std::uint32_t> mRemoveArchiveIndices;
  std::string mFailurePointComment;
};

bool LoadGeneratedFenceCaseDataFile(const std::string& pPath,
                                    std::vector<GeneratedFenceCaseData>& pCases,
                                    std::string& pError);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_GENERATED_FENCE_CASE_DATA_HPP_
