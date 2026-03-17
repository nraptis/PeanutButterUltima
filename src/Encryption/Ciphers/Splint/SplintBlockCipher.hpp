#ifndef JELLY_SPLINT_BLOCK_CIPHER_HPP_
#define JELLY_SPLINT_BLOCK_CIPHER_HPP_

#include <cstddef>
#include <cstring>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SplintBlockCipher final : public Crypt {
 public:
  explicit SplintBlockCipher(std::size_t pBlockSize) : mBlockSize(pBlockSize) {}

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
    if ((pLength % mBlockSize) != 0) {
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
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + aHalf;
    const unsigned char* aFrontShelf = pSource + aHalf;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf && aBack < aBackShelf) {
      std::memcpy(aOut, aFront, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
      std::memcpy(aOut, aBack, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    while (aFront < aFrontShelf) {
      std::memcpy(aOut, aFront, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
    }

    while (aBack < aBackShelf) {
      std::memcpy(aOut, aBack, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }
  }

  void ApplyDecryptSoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aIn = pSource;
    const unsigned char* aInShelf = pSource + pLength;
    unsigned char* aFront = pDestination;
    unsigned char* aBack = pDestination + aHalf;
    unsigned char* aFrontShelf = pDestination + aHalf;
    unsigned char* aBackShelf = pDestination + pLength;

    while (aFront < aFrontShelf && aBack < aBackShelf && aIn < aInShelf) {
      std::memcpy(aFront, aIn, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
      std::memcpy(aBack, aIn, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    while (aFront < aFrontShelf && aIn < aInShelf) {
      std::memcpy(aFront, aIn, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
    }

    while (aBack < aBackShelf && aIn < aInShelf) {
      std::memcpy(aBack, aIn, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }
  }

  std::size_t mBlockSize;
};

}  // namespace peanutbutter

#endif  // JELLY_SPLINT_BLOCK_CIPHER_HPP_
