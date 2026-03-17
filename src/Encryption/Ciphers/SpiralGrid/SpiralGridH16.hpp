#ifndef JELLY_SPIRAL_GRID_H16_HPP_
#define JELLY_SPIRAL_GRID_H16_HPP_

#include <array>
#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SpiralGridH16 final : public Crypt {
 public:
  explicit SpiralGridH16(int pAmount) : mAmount(pAmount) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return Apply(pSource, pDestination, pLength, mAmount);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return Apply(pSource, pDestination, pLength, -mAmount);
  }

 private:
  static constexpr std::size_t kTileSize = 16;
  static constexpr std::array<unsigned char, 12> kOuter = {
      0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4};
  static constexpr std::array<unsigned char, 4> kInner = {5, 6, 10, 9};

  template <std::size_t tCount>
  static void RotateRing(const unsigned char* pSource,
                         unsigned char* pDestination,
                         const std::array<unsigned char, tCount>& pRing,
                         int pAmount) {
    int aRotation = pAmount % static_cast<int>(tCount);
    if (aRotation < 0) {
      aRotation += static_cast<int>(tCount);
    }
    const std::size_t aShift = static_cast<std::size_t>(aRotation);
    for (std::size_t aIndex = 0; aIndex < tCount; ++aIndex) {
      const std::size_t aSourceIndex =
          (aIndex + tCount - aShift) % tCount;
      pDestination[pRing[aIndex]] = pSource[pRing[aSourceIndex]];
    }
  }

  static bool Apply(const unsigned char* pSource,
                    unsigned char* pDestination,
                    std::size_t pLength,
                    int pAmount) {
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if ((pLength % kTileSize) != 0) {
      return false;
    }
    if (pSource == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination) {
      return false;
    }

    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += kTileSize) {
      const unsigned char* aSource = pSource + aOffset;
      unsigned char* aDestination = pDestination + aOffset;
      RotateRing(aSource, aDestination, kOuter, pAmount);
      RotateRing(aSource, aDestination, kInner, pAmount);
    }

    return true;
  }

  int mAmount;
};

}  // namespace peanutbutter

#endif  // JELLY_SPIRAL_GRID_H16_HPP_
