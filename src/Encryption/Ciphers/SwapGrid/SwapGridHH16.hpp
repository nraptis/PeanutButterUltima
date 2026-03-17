#ifndef JELLY_SWAP_GRID_HH16_HPP_
#define JELLY_SWAP_GRID_HH16_HPP_

#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SwapGridHH16 final : public Crypt {
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
  static constexpr std::size_t kTileSize = 16;

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
      pDestination[aOffset + 0] = pSource[aOffset + 1];
      pDestination[aOffset + 1] = pSource[aOffset + 0];
      pDestination[aOffset + 2] = pSource[aOffset + 3];
      pDestination[aOffset + 3] = pSource[aOffset + 2];
      pDestination[aOffset + 4] = pSource[aOffset + 5];
      pDestination[aOffset + 5] = pSource[aOffset + 4];
      pDestination[aOffset + 6] = pSource[aOffset + 7];
      pDestination[aOffset + 7] = pSource[aOffset + 6];
      pDestination[aOffset + 8] = pSource[aOffset + 9];
      pDestination[aOffset + 9] = pSource[aOffset + 8];
      pDestination[aOffset + 10] = pSource[aOffset + 11];
      pDestination[aOffset + 11] = pSource[aOffset + 10];
      pDestination[aOffset + 12] = pSource[aOffset + 13];
      pDestination[aOffset + 13] = pSource[aOffset + 12];
      pDestination[aOffset + 14] = pSource[aOffset + 15];
      pDestination[aOffset + 15] = pSource[aOffset + 14];
    }

    return true;
  }
};

}  // namespace peanutbutter

#endif  // JELLY_SWAP_GRID_HH16_HPP_
