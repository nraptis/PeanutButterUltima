#ifndef JELLY_SWAP_GRID_MASK_HH16_HPP_
#define JELLY_SWAP_GRID_MASK_HH16_HPP_

#include <cstddef>
#include <cstdint>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SwapGridMaskHH16 final : public Crypt {
 public:
  explicit SwapGridMaskHH16(std::uint8_t pMask) : mMask(pMask) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return Apply(pSource, pDestination, pLength);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return Apply(pSource, pDestination, pLength);
  }

 private:
  static constexpr std::size_t kTileSize = 16;

  static bool Validate(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength) {
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
    return true;
  }

  static unsigned char Blend(unsigned char pBase,
                             unsigned char pMoved,
                             unsigned char pMask,
                             unsigned char pAntiMask) {
    return static_cast<unsigned char>((pBase & pAntiMask) | (pMoved & pMask));
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength) const {
    if (!Validate(pSource, pDestination, pLength)) {
      return pLength == 0;
    }
    const unsigned char aAntiMask = static_cast<unsigned char>(~mMask);
    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += kTileSize) {
      pDestination[aOffset + 0] = Blend(pSource[aOffset + 0], pSource[aOffset + 1], mMask, aAntiMask);
      pDestination[aOffset + 1] = Blend(pSource[aOffset + 1], pSource[aOffset + 0], mMask, aAntiMask);
      pDestination[aOffset + 2] = Blend(pSource[aOffset + 2], pSource[aOffset + 3], mMask, aAntiMask);
      pDestination[aOffset + 3] = Blend(pSource[aOffset + 3], pSource[aOffset + 2], mMask, aAntiMask);
      pDestination[aOffset + 4] = Blend(pSource[aOffset + 4], pSource[aOffset + 5], mMask, aAntiMask);
      pDestination[aOffset + 5] = Blend(pSource[aOffset + 5], pSource[aOffset + 4], mMask, aAntiMask);
      pDestination[aOffset + 6] = Blend(pSource[aOffset + 6], pSource[aOffset + 7], mMask, aAntiMask);
      pDestination[aOffset + 7] = Blend(pSource[aOffset + 7], pSource[aOffset + 6], mMask, aAntiMask);
      pDestination[aOffset + 8] = Blend(pSource[aOffset + 8], pSource[aOffset + 9], mMask, aAntiMask);
      pDestination[aOffset + 9] = Blend(pSource[aOffset + 9], pSource[aOffset + 8], mMask, aAntiMask);
      pDestination[aOffset + 10] = Blend(pSource[aOffset + 10], pSource[aOffset + 11], mMask, aAntiMask);
      pDestination[aOffset + 11] = Blend(pSource[aOffset + 11], pSource[aOffset + 10], mMask, aAntiMask);
      pDestination[aOffset + 12] = Blend(pSource[aOffset + 12], pSource[aOffset + 13], mMask, aAntiMask);
      pDestination[aOffset + 13] = Blend(pSource[aOffset + 13], pSource[aOffset + 12], mMask, aAntiMask);
      pDestination[aOffset + 14] = Blend(pSource[aOffset + 14], pSource[aOffset + 15], mMask, aAntiMask);
      pDestination[aOffset + 15] = Blend(pSource[aOffset + 15], pSource[aOffset + 14], mMask, aAntiMask);
    }
    return true;
  }

  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_SWAP_GRID_MASK_HH16_HPP_

