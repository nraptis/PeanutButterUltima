#include <iostream>
#include <string>
#include <cstdint>
#include <vector>

#include "Test_IntegerFailureCaseCommon.hpp"

namespace {

bool BuildLogicalToPhysicalMap(const peanutbutter::ByteVector& pBytes, std::vector<std::size_t>& pMap) {
  if (pBytes.size() <= peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH) {
    return false;
  }
  pMap.clear();
  for (std::size_t aBlockStart = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH; aBlockStart < pBytes.size();
       aBlockStart += peanutbutter::SB_L1_LENGTH) {
    const std::size_t aPayloadStart = aBlockStart + peanutbutter::SB_RECOVERY_HEADER_LENGTH;
    const std::size_t aPayloadEnd = std::min(aBlockStart + peanutbutter::SB_L1_LENGTH, pBytes.size());
    if (aPayloadStart >= aPayloadEnd) {
      continue;
    }
    for (std::size_t aIndex = aPayloadStart; aIndex < aPayloadEnd; ++aIndex) {
      pMap.push_back(aIndex);
    }
  }
  return !pMap.empty();
}

std::uint16_t ReadU16(const peanutbutter::ByteVector& pBytes, const std::vector<std::size_t>& pMap, std::size_t pOffset) {
  return static_cast<std::uint16_t>(pBytes[pMap[pOffset]] | (static_cast<std::uint16_t>(pBytes[pMap[pOffset + 1]]) << 8));
}

std::uint64_t ReadU48(const peanutbutter::ByteVector& pBytes,
                      const std::vector<std::size_t>& pMap,
                      std::size_t pOffset) {
  std::uint64_t aValue = 0;
  for (std::size_t aIndex = 0; aIndex < 6; ++aIndex) {
    aValue |= static_cast<std::uint64_t>(pBytes[pMap[pOffset + aIndex]]) << (8 * aIndex);
  }
  return aValue;
}

bool FindManifestTerminatorOffset(const peanutbutter::ByteVector& pBytes,
                                  const std::vector<std::size_t>& pMap,
                                  std::size_t& pTerminatorOffset) {
  std::size_t aOffset = 0;
  while (aOffset + 2 <= pMap.size()) {
    const std::uint16_t aPathLength = ReadU16(pBytes, pMap, aOffset);
    aOffset += 2;
    if (aPathLength == 0) {
      break;
    }
    if (aOffset + aPathLength + 6 > pMap.size()) {
      return false;
    }
    aOffset += aPathLength;
    const std::uint64_t aContentLength = ReadU48(pBytes, pMap, aOffset);
    aOffset += 6;
    if (aOffset + aContentLength > pMap.size()) {
      return false;
    }
    aOffset += static_cast<std::size_t>(aContentLength);
  }

  while (aOffset + 2 <= pMap.size()) {
    const std::uint16_t aFolderLength = ReadU16(pBytes, pMap, aOffset);
    if (aFolderLength == 0) {
      pTerminatorOffset = aOffset;
      return true;
    }
    aOffset += 2;
    if (aOffset + aFolderLength > pMap.size()) {
      return false;
    }
    aOffset += aFolderLength;
  }
  return false;
}

}  // namespace

int main() {
  using peanutbutter::testing::ArchiveMutator;
  using peanutbutter::testing::RunUnbundleIntegerFailureCase;
  using peanutbutter::testing::SeedBasicIntegerFailureInputTree;

  const ArchiveMutator aMutator = [](peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
    std::vector<std::size_t> aLogicalToPhysical;
    if (!BuildLogicalToPhysicalMap(pBytes, aLogicalToPhysical)) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: archive too small for EOF mutation.";
      }
      return false;
    }
    std::size_t aTerminatorOffset = 0;
    if (!FindManifestTerminatorOffset(pBytes, aLogicalToPhysical, aTerminatorOffset)) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: could not locate manifest terminator.";
      }
      return false;
    }
    if (aTerminatorOffset + 2 >= aLogicalToPhysical.size()) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "Mutation failed: no trailing payload for EOF mutation.";
      }
      return false;
    }
    pBytes[aLogicalToPhysical.back()] = 0x01;
    return true;
  };

  std::string aError;
  if (!RunUnbundleIntegerFailureCase("UNP_EOF_002", SeedBasicIntegerFailureInputTree, aMutator, &aError)) {
    std::cerr << aError << "\n";
    return 1;
  }
  std::cout << "Test_IntegerFailureCases_UNP_EOF_002 passed\n";
  return 0;
}
