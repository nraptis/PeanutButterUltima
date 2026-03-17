#ifndef JELLY_ROTATE_CIPHER_HPP_
#define JELLY_ROTATE_CIPHER_HPP_

#include <cstddef>
#include <cstring>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class RotateCipher final : public Crypt {
 public:
  explicit RotateCipher(int pShift) : mShift(pShift) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength, -mShift);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength, mShift);
  }

 private:
  static bool Apply(const unsigned char* pSource,
                    unsigned char* pDestination,
                    std::size_t pLength,
                    int pShift) {
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

    const std::size_t aRotation = NormalizeShift(pShift, pLength);
    const std::size_t aFirstSpan = pLength - aRotation;
    std::memcpy(pDestination, pSource + aRotation, aFirstSpan);
    std::memcpy(pDestination + aFirstSpan, pSource, aRotation);
    return true;
  }

  static std::size_t NormalizeShift(int pShift, std::size_t pLength) {
    int aRotation = pShift % static_cast<int>(pLength);
    if (aRotation < 0) {
      aRotation += static_cast<int>(pLength);
    }
    return static_cast<std::size_t>(aRotation);
  }

  int mShift;
};

}  // namespace peanutbutter

#endif  // JELLY_ROTATE_CIPHER_HPP_
