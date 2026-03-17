#ifndef JELLY_SWAP_GRID_MASK_VH64_HPP_
#define JELLY_SWAP_GRID_MASK_VH64_HPP_

#include <cstddef>
#include <cstdint>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SwapGridMaskVH64 final : public Crypt {
 public:
  explicit SwapGridMaskVH64(std::uint8_t pMask) : mMask(pMask) {}

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
  static constexpr std::size_t kDim = 8;
  static constexpr std::size_t kTileSize = 64;

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
      for (std::size_t aRow = 0; aRow < kDim; ++aRow) {
        const std::size_t aDestinationBase = aOffset + (aRow << 3);
        const std::size_t aSourceBase = aOffset + ((aRow ^ 1u) << 3);
        pDestination[aDestinationBase + 0] =
            Blend(pSource[aDestinationBase + 0], pSource[aSourceBase + 0], mMask, aAntiMask);
        pDestination[aDestinationBase + 1] =
            Blend(pSource[aDestinationBase + 1], pSource[aSourceBase + 1], mMask, aAntiMask);
        pDestination[aDestinationBase + 2] =
            Blend(pSource[aDestinationBase + 2], pSource[aSourceBase + 2], mMask, aAntiMask);
        pDestination[aDestinationBase + 3] =
            Blend(pSource[aDestinationBase + 3], pSource[aSourceBase + 3], mMask, aAntiMask);
        pDestination[aDestinationBase + 4] =
            Blend(pSource[aDestinationBase + 4], pSource[aSourceBase + 4], mMask, aAntiMask);
        pDestination[aDestinationBase + 5] =
            Blend(pSource[aDestinationBase + 5], pSource[aSourceBase + 5], mMask, aAntiMask);
        pDestination[aDestinationBase + 6] =
            Blend(pSource[aDestinationBase + 6], pSource[aSourceBase + 6], mMask, aAntiMask);
        pDestination[aDestinationBase + 7] =
            Blend(pSource[aDestinationBase + 7], pSource[aSourceBase + 7], mMask, aAntiMask);
      }
    }
    return true;
  }

  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_SWAP_GRID_MASK_VH64_HPP_

