#ifndef JELLY_SWAP_GRID_VV64_HPP_
#define JELLY_SWAP_GRID_VV64_HPP_

#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SwapGridVV64 final : public Crypt {
 public:
  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return ApplyEncrypt(pSource, pDestination, pLength);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pWorker;
    (void)pMode;
    return ApplyDecrypt(pSource, pDestination, pLength);
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

  static bool ApplyEncrypt(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pLength) {
    if (!Validate(pSource, pDestination, pLength)) {
      return pLength == 0;
    }
    if (pLength == 0) {
      return true;
    }

    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += kTileSize) {
      for (std::size_t aCol = 0; aCol < kDim; ++aCol) {
        const std::size_t aDestinationBase = aOffset + (aCol << 3);
        pDestination[aDestinationBase + 0] =
            pSource[aOffset + ((1u << 3) + aCol)];
        pDestination[aDestinationBase + 1] =
            pSource[aOffset + ((0u << 3) + aCol)];
        pDestination[aDestinationBase + 2] =
            pSource[aOffset + ((3u << 3) + aCol)];
        pDestination[aDestinationBase + 3] =
            pSource[aOffset + ((2u << 3) + aCol)];
        pDestination[aDestinationBase + 4] =
            pSource[aOffset + ((5u << 3) + aCol)];
        pDestination[aDestinationBase + 5] =
            pSource[aOffset + ((4u << 3) + aCol)];
        pDestination[aDestinationBase + 6] =
            pSource[aOffset + ((7u << 3) + aCol)];
        pDestination[aDestinationBase + 7] =
            pSource[aOffset + ((6u << 3) + aCol)];
      }
    }

    return true;
  }

  static bool ApplyDecrypt(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pLength) {
    if (!Validate(pSource, pDestination, pLength)) {
      return pLength == 0;
    }
    if (pLength == 0) {
      return true;
    }

    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += kTileSize) {
      for (std::size_t aRow = 0; aRow < kDim; ++aRow) {
        const std::size_t aDestinationBase = aOffset + (aRow << 3);
        pDestination[aDestinationBase + 0] =
            pSource[aOffset + ((0u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 1] =
            pSource[aOffset + ((1u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 2] =
            pSource[aOffset + ((2u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 3] =
            pSource[aOffset + ((3u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 4] =
            pSource[aOffset + ((4u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 5] =
            pSource[aOffset + ((5u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 6] =
            pSource[aOffset + ((6u << 3) + (aRow ^ 1u))];
        pDestination[aDestinationBase + 7] =
            pSource[aOffset + ((7u << 3) + (aRow ^ 1u))];
      }
    }

    return true;
  }
};

}  // namespace peanutbutter

#endif  // JELLY_SWAP_GRID_VV64_HPP_
