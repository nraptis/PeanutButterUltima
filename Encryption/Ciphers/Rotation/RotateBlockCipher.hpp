#ifndef JELLY_ROTATE_BLOCK_CIPHER_HPP_
#define JELLY_ROTATE_BLOCK_CIPHER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class RotateBlockCipher final : public Crypt {
 public:
  RotateBlockCipher(std::size_t pBlockSize, int pShift)
      : mBlockSize(pBlockSize), mShift(pShift) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength, mShift);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength, -mShift);
  }

 private:
  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength,
             int pShift) const {
    if (mBlockSize == 0) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if (pSource == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination) {
      return false;
    }

    for (std::size_t aOffset = 0; aOffset < pLength; aOffset += mBlockSize) {
      const std::size_t aSpan = std::min(mBlockSize, pLength - aOffset);
      const std::size_t aRotation = NormalizeShift(pShift, aSpan);
      const std::size_t aFirstSpan = aSpan - aRotation;
      std::memcpy(pDestination + aOffset, pSource + aOffset + aRotation,
                  aFirstSpan);
      std::memcpy(pDestination + aOffset + aFirstSpan, pSource + aOffset,
                  aRotation);
    }
    return true;
  }

  static std::size_t NormalizeShift(int pShift, std::size_t pLength) {
    int aRotation = pShift % static_cast<int>(pLength);
    if (aRotation < 0) {
      aRotation += static_cast<int>(pLength);
    }
    return static_cast<std::size_t>(aRotation);
  }

  std::size_t mBlockSize;
  int mShift;
};

}  // namespace peanutbutter

#endif  // JELLY_ROTATE_BLOCK_CIPHER_HPP_
