#ifndef JELLY_SWAP_GRID_VH64_HPP_
#define JELLY_SWAP_GRID_VH64_HPP_

#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SwapGridVH64 final : public Crypt {
 public:
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

  static bool Apply(const unsigned char* pSource,
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

    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += kTileSize) {
      for (std::size_t aRow = 0; aRow < kDim; ++aRow) {
        const std::size_t aDestinationBase = aOffset + (aRow << 3);
        const std::size_t aSourceBase = aOffset + ((aRow ^ 1u) << 3);
        pDestination[aDestinationBase + 0] = pSource[aSourceBase + 0];
        pDestination[aDestinationBase + 1] = pSource[aSourceBase + 1];
        pDestination[aDestinationBase + 2] = pSource[aSourceBase + 2];
        pDestination[aDestinationBase + 3] = pSource[aSourceBase + 3];
        pDestination[aDestinationBase + 4] = pSource[aSourceBase + 4];
        pDestination[aDestinationBase + 5] = pSource[aSourceBase + 5];
        pDestination[aDestinationBase + 6] = pSource[aSourceBase + 6];
        pDestination[aDestinationBase + 7] = pSource[aSourceBase + 7];
      }
    }

    return true;
  }
};

}  // namespace peanutbutter

#endif  // JELLY_SWAP_GRID_VH64_HPP_
