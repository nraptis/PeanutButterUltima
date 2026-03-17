#ifndef JELLY_SPIRAL_GRID_MASK_V16_HPP_
#define JELLY_SPIRAL_GRID_MASK_V16_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SpiralGridMaskV16 final : public Crypt {
 public:
  SpiralGridMaskV16(std::uint8_t pMask, int pAmount)
      : mMask(pMask), mAmount(pAmount) {}

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
      0, 4, 8, 12, 13, 14, 15, 11, 7, 3, 2, 1};
  static constexpr std::array<unsigned char, 4> kInner = {5, 9, 10, 6};

  static unsigned char Blend(unsigned char pBase,
                             unsigned char pMoved,
                             unsigned char pMask,
                             unsigned char pAntiMask) {
    return static_cast<unsigned char>((pBase & pAntiMask) | (pMoved & pMask));
  }

  template <std::size_t tCount>
  static void RotateRingMasked(const unsigned char* pSource,
                               unsigned char* pDestination,
                               const std::array<unsigned char, tCount>& pRing,
                               int pAmount,
                               unsigned char pMask) {
    int aRotation = pAmount % static_cast<int>(tCount);
    if (aRotation < 0) {
      aRotation += static_cast<int>(tCount);
    }
    const std::size_t aShift = static_cast<std::size_t>(aRotation);
    const unsigned char aAntiMask = static_cast<unsigned char>(~pMask);
    for (std::size_t aIndex = 0; aIndex < tCount; ++aIndex) {
      const std::size_t aSourceIndex = (aIndex + tCount - aShift) % tCount;
      const std::size_t aDestinationSlot = pRing[aIndex];
      const std::size_t aMovedSlot = pRing[aSourceIndex];
      pDestination[aDestinationSlot] =
          Blend(pSource[aDestinationSlot], pSource[aMovedSlot], pMask, aAntiMask);
    }
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength,
             int pAmount) const {
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
      RotateRingMasked(aSource, aDestination, kOuter, pAmount, mMask);
      RotateRingMasked(aSource, aDestination, kInner, pAmount, mMask);
    }
    return true;
  }

  std::uint8_t mMask;
  int mAmount;
};

}  // namespace peanutbutter

#endif  // JELLY_SPIRAL_GRID_MASK_V16_HPP_

