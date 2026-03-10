#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_UTILS_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_UTILS_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Test_Execute_Bundle.hpp"

namespace peanutbutter::testing {

std::string JoinLogLines(const std::vector<std::string>& pStatusMessages,
                         const std::vector<std::string>& pErrorMessages);

bool ContainsToken(const std::string& pText, const std::string& pToken);

std::size_t SerializedFileRecordLength(const TestSeedFile& pFile);

std::size_t RecordStartLogicalOffset(const std::vector<TestSeedFile>& pSortedFiles, std::size_t pRecordIndex);

std::vector<unsigned char> EncodeLe6(std::uint64_t pValue);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_UTILS_HPP_
