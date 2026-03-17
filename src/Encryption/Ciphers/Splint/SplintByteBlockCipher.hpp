#ifndef JELLY_SPLINT_BYTE_BLOCK_CIPHER_HPP_
#define JELLY_SPLINT_BYTE_BLOCK_CIPHER_HPP_

#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SplintByteBlockCipher final : public Crypt {
 public:
  explicit SplintByteBlockCipher(std::size_t pBlockSize)
      : mBlockSize(pBlockSize) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pMode;
    if (!ValidateInputs(pSource, pDestination, pLength)) {
      return false;
    }
    ApplyEncryptSoftware(pSource, pDestination, pLength);
    return true;
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pMode;
    if (!ValidateInputs(pSource, pDestination, pLength)) {
      return false;
    }
    ApplyDecryptSoftware(pSource, pDestination, pLength);
    return true;
  }

 private:
  bool ValidateInputs(const unsigned char* pSource,
                      unsigned char* pDestination,
                      std::size_t pLength) const {
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
    return true;
  }

  void ApplyEncryptSoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    const std::size_t aFullBlockCount = pLength / mBlockSize;
    const std::size_t aTailOffset = aFullBlockCount * mBlockSize;
    const unsigned char* aBlock = pSource;
    unsigned char* aOut = pDestination;

    for (std::size_t aIndex = 0; aIndex < aFullBlockCount; ++aIndex) {
      ApplyEncryptSpan(aBlock, aOut, mBlockSize);
      aBlock += mBlockSize;
      aOut += mBlockSize;
    }

    if (aTailOffset < pLength) {
      ApplyEncryptSpan(pSource + aTailOffset, pDestination + aTailOffset,
                       pLength - aTailOffset);
    }
  }

  void ApplyDecryptSoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    const std::size_t aFullBlockCount = pLength / mBlockSize;
    const std::size_t aTailOffset = aFullBlockCount * mBlockSize;
    const unsigned char* aBlock = pSource;
    unsigned char* aOut = pDestination;

    for (std::size_t aIndex = 0; aIndex < aFullBlockCount; ++aIndex) {
      ApplyDecryptSpan(aBlock, aOut, mBlockSize);
      aBlock += mBlockSize;
      aOut += mBlockSize;
    }

    if (aTailOffset < pLength) {
      ApplyDecryptSpan(pSource + aTailOffset, pDestination + aTailOffset,
                       pLength - aTailOffset);
    }
  }

  static void ApplyEncryptSpan(const unsigned char* pSource,
                               unsigned char* pDestination,
                               std::size_t pLength) {
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + (pLength >> 1);
    const unsigned char* aFrontShelf = aBack;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf) {
      *aOut = *aFront;
      aOut += 2;
      ++aFront;
    }

    aOut = pDestination + 1;
    while (aBack < aBackShelf) {
      *aOut = *aBack;
      aOut += 2;
      ++aBack;
    }
  }

  static void ApplyDecryptSpan(const unsigned char* pSource,
                               unsigned char* pDestination,
                               std::size_t pLength) {
    const unsigned char* aEven = pSource;
    const unsigned char* aEvenShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aEven < aEvenShelf) {
      *aOut++ = *aEven;
      aEven += 2;
    }

    const unsigned char* aOdd = pSource + 1;
    const unsigned char* aOddShelf = pSource + pLength;
    while (aOdd < aOddShelf) {
      *aOut++ = *aOdd;
      aOdd += 2;
    }
  }

  std::size_t mBlockSize;
};

}  // namespace peanutbutter

#endif  // JELLY_SPLINT_BYTE_BLOCK_CIPHER_HPP_
