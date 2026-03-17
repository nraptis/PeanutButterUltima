#ifndef JELLY_SPLINT_CIPHER_HPP_
#define JELLY_SPLINT_CIPHER_HPP_

#include <cstddef>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class SplintCipher final : public Crypt {
 public:
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
  static bool ValidateInputs(const unsigned char* pSource,
                             unsigned char* pDestination,
                             std::size_t pLength) {
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

  static void ApplyEncryptSoftware(const unsigned char* pSource,
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

  static void ApplyDecryptSoftware(const unsigned char* pSource,
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
};

}  // namespace peanutbutter

#endif  // JELLY_SPLINT_CIPHER_HPP_
