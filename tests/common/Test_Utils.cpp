#include "Test_Utils.hpp"

namespace peanutbutter::testing {

std::string JoinLogLines(const std::vector<std::string>& pStatusMessages,
                         const std::vector<std::string>& pErrorMessages) {
  std::string aOutput;
  for (const std::string& aLine : pStatusMessages) {
    aOutput += "[status] " + aLine + "\n";
  }
  for (const std::string& aLine : pErrorMessages) {
    aOutput += "[error] " + aLine + "\n";
  }
  return aOutput;
}

bool ContainsToken(const std::string& pText, const std::string& pToken) {
  return !pToken.empty() && pText.find(pToken) != std::string::npos;
}

std::size_t SerializedFileRecordLength(const TestSeedFile& pFile) {
  return 2u + pFile.mRelativePath.size() + 6u + pFile.mContents.size();
}

std::size_t RecordStartLogicalOffset(const std::vector<TestSeedFile>& pSortedFiles, std::size_t pRecordIndex) {
  std::size_t aOffset = 0;
  for (std::size_t aIndex = 0; aIndex < pRecordIndex; ++aIndex) {
    aOffset += SerializedFileRecordLength(pSortedFiles[aIndex]);
  }
  return aOffset;
}

std::vector<unsigned char> EncodeLe6(std::uint64_t pValue) {
  std::vector<unsigned char> aBytes(6u, 0u);
  for (std::size_t aIndex = 0; aIndex < aBytes.size(); ++aIndex) {
    aBytes[aIndex] = static_cast<unsigned char>((pValue >> (8u * aIndex)) & 0xFFu);
  }
  return aBytes;
}

}  // namespace peanutbutter::testing
