#ifndef JELLY_RIPPLE_BLOCK_CIPHER_HPP_
#define JELLY_RIPPLE_BLOCK_CIPHER_HPP_

#include <cstddef>
#include <cstring>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class RippleBlockCipher final : public Crypt {
 public:
  RippleBlockCipher(std::size_t pBlockSize, int pRounds)
      : mBlockSize(pBlockSize), mRounds(pRounds > 0 ? pRounds : 1) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pWorker, pDestination, pLength, true);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pWorker, pDestination, pLength, false);
  }

 private:
  bool Apply(const unsigned char* pSource,
             unsigned char* pWorker,
             unsigned char* pDestination,
             std::size_t pLength,
             bool pEncrypt) const {
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
    if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination || pSource == pWorker || pDestination == pWorker) {
      return false;
    }

    const std::size_t aBlockCount = pLength / mBlockSize;
    const unsigned char* aInput = pSource;
    unsigned char* aOutput =
        ((mRounds & 1) == 0) ? pWorker : pDestination;
    if (pEncrypt) {
      for (int aRound = 0; aRound < mRounds; ++aRound) {
        const int aPhase = aRound & 1;
        ApplyBlockPass(aInput, aOutput, aBlockCount, aPhase);
        aInput = aOutput;
        aOutput = (aOutput == pDestination) ? pWorker : pDestination;
      }
    } else {
      for (int aRound = mRounds; aRound > 0; --aRound) {
        const int aPhase = (aRound - 1) & 1;
        ApplyBlockPass(aInput, aOutput, aBlockCount, aPhase);
        aInput = aOutput;
        aOutput = (aOutput == pDestination) ? pWorker : pDestination;
      }
    }
    return true;
  }

  template <std::size_t kBlockSize>
  static void CopyBlock(unsigned char* pDestination,
                        const unsigned char* pSource) {
    std::memcpy(pDestination, pSource, kBlockSize);
  }

  template <std::size_t kBlockSize>
  void ApplyBlockPassFixed(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pBlockCount,
                           int pPhase) const {
    std::size_t aBlock = 0;
    if (pPhase != 0) {
      CopyBlock<kBlockSize>(pDestination, pSource);
      aBlock = 1;
    }

    for (; aBlock + 1 < pBlockCount; aBlock += 2) {
      const std::size_t aLeftOffset = aBlock * kBlockSize;
      const std::size_t aRightOffset = aLeftOffset + kBlockSize;
      CopyBlock<kBlockSize>(pDestination + aLeftOffset,
                            pSource + aRightOffset);
      CopyBlock<kBlockSize>(pDestination + aRightOffset,
                            pSource + aLeftOffset);
    }

    if (aBlock < pBlockCount) {
      const std::size_t aOffset = aBlock * kBlockSize;
      CopyBlock<kBlockSize>(pDestination + aOffset, pSource + aOffset);
    }
  }

  void ApplyBlockPassVariable(const unsigned char* pSource,
                              unsigned char* pDestination,
                              std::size_t pBlockCount,
                              int pPhase) const {
    if (pPhase == 0) {
      std::size_t aBlock = 0;
      for (; aBlock + 1 < pBlockCount; aBlock += 2) {
        const std::size_t aLeftOffset = aBlock * mBlockSize;
        const std::size_t aRightOffset = (aBlock + 1) * mBlockSize;
        std::memcpy(pDestination + aLeftOffset, pSource + aRightOffset,
                    mBlockSize);
        std::memcpy(pDestination + aRightOffset, pSource + aLeftOffset,
                    mBlockSize);
      }
      if (aBlock < pBlockCount) {
        const std::size_t aOffset = aBlock * mBlockSize;
        std::memcpy(pDestination + aOffset, pSource + aOffset, mBlockSize);
      }
      return;
    }

    std::memcpy(pDestination, pSource, mBlockSize);
    std::size_t aBlock = 1;
    for (; aBlock + 1 < pBlockCount; aBlock += 2) {
      const std::size_t aLeftOffset = aBlock * mBlockSize;
      const std::size_t aRightOffset = (aBlock + 1) * mBlockSize;
      std::memcpy(pDestination + aLeftOffset, pSource + aRightOffset,
                  mBlockSize);
      std::memcpy(pDestination + aRightOffset, pSource + aLeftOffset,
                  mBlockSize);
    }
    if (aBlock < pBlockCount) {
      const std::size_t aOffset = aBlock * mBlockSize;
      std::memcpy(pDestination + aOffset, pSource + aOffset, mBlockSize);
    }
  }

  void ApplyBlockPass(const unsigned char* pSource,
                      unsigned char* pDestination,
                      std::size_t pBlockCount,
                      int pPhase) const {
    switch (mBlockSize) {
      case 8:
        ApplyBlockPassFixed<8>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      case 12:
        ApplyBlockPassFixed<12>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      case 16:
        ApplyBlockPassFixed<16>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      case 24:
        ApplyBlockPassFixed<24>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      case 32:
        ApplyBlockPassFixed<32>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      case 48:
        ApplyBlockPassFixed<48>(pSource, pDestination, pBlockCount,
                                              pPhase);
        return;
      default:
        ApplyBlockPassVariable(pSource, pDestination, pBlockCount, pPhase);
        return;
    }
  }

  std::size_t mBlockSize;
  int mRounds;
};

}  // namespace peanutbutter

#endif  // JELLY_RIPPLE_BLOCK_CIPHER_HPP_
