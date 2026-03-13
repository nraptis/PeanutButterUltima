#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_DETERMINISTIC_RNG_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_DETERMINISTIC_RNG_HPP_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace peanutbutter {
namespace testkit {

class DeterministicRng {
 public:
  explicit DeterministicRng(std::uint64_t pSeed = 0x9e3779b97f4a7c15ull)
      : mState(pSeed == 0u ? 0x9e3779b97f4a7c15ull : pSeed) {}

  std::uint64_t NextU64() {
    std::uint64_t aValue = mState;
    aValue ^= (aValue >> 12);
    aValue ^= (aValue << 25);
    aValue ^= (aValue >> 27);
    mState = aValue;
    return aValue * 2685821657736338717ull;
  }

  std::uint32_t NextU32() {
    return static_cast<std::uint32_t>(NextU64() & 0xFFFFFFFFu);
  }

  std::uint64_t UniformU64(std::uint64_t pMinInclusive,
                           std::uint64_t pMaxInclusive) {
    if (pMinInclusive >= pMaxInclusive) {
      return pMinInclusive;
    }
    const std::uint64_t aRange = (pMaxInclusive - pMinInclusive) + 1u;
    return pMinInclusive + (NextU64() % aRange);
  }

  std::size_t UniformSize(std::size_t pMinInclusive,
                          std::size_t pMaxInclusive) {
    if (pMinInclusive >= pMaxInclusive) {
      return pMinInclusive;
    }
    const std::uint64_t aValue = UniformU64(static_cast<std::uint64_t>(pMinInclusive),
                                            static_cast<std::uint64_t>(pMaxInclusive));
    return static_cast<std::size_t>(aValue);
  }

  bool Chance(std::uint32_t pNumerator,
              std::uint32_t pDenominator) {
    if (pDenominator == 0u) {
      return false;
    }
    if (pNumerator >= pDenominator) {
      return true;
    }
    const std::uint32_t aDraw = static_cast<std::uint32_t>(NextU64() % pDenominator);
    return aDraw < pNumerator;
  }

  void FillBytes(std::vector<unsigned char>& pBytes) {
    for (unsigned char& aByte : pBytes) {
      aByte = static_cast<unsigned char>(NextU32() & 0xFFu);
    }
  }

  static std::uint64_t HashString(const std::string& pText) {
    std::uint64_t aHash = 1469598103934665603ull;
    for (unsigned char aByte : pText) {
      aHash ^= static_cast<std::uint64_t>(aByte);
      aHash *= 1099511628211ull;
    }
    return aHash;
  }

  static std::uint64_t MixSeed(std::initializer_list<std::uint64_t> pValues) {
    std::uint64_t aHash = 0x243f6a8885a308d3ull;
    for (std::uint64_t aValue : pValues) {
      aHash ^= aValue + 0x9e3779b97f4a7c15ull + (aHash << 6) + (aHash >> 2);
    }
    if (aHash == 0u) {
      aHash = std::numeric_limits<std::uint64_t>::max() - 58u;
    }
    return aHash;
  }

 private:
  std::uint64_t mState;
};

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_DETERMINISTIC_RNG_HPP_
