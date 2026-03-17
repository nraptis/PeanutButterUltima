#ifndef JELLY_RIPPLE_MASK_BLOCK_CIPHER_HPP_
#define JELLY_RIPPLE_MASK_BLOCK_CIPHER_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__SSSE3__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class RippleMaskBlockCipher final : public Crypt {
 public:
  RippleMaskBlockCipher(std::size_t pBlockSize, std::uint8_t pMask, int pRounds)
      : mBlockSize(pBlockSize),
        mMask(pMask),
        mRounds(pRounds > 0 ? pRounds : 1) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, true, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, false, pMode);
  }

 private:
  bool Apply(const unsigned char* pSource,
             unsigned char* pWorker,
             unsigned char* pDestination,
             std::size_t pLength,
             bool pEncrypt,
             CryptMode pMode) const {
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
        ApplyMaskedBlockPass(aInput, aOutput, aBlockCount, aPhase, pMode);
        aInput = aOutput;
        aOutput = (aOutput == pDestination) ? pWorker : pDestination;
      }
    } else {
      for (int aRound = mRounds; aRound > 0; --aRound) {
        const int aPhase = (aRound - 1) & 1;
        ApplyMaskedBlockPass(aInput, aOutput, aBlockCount, aPhase, pMode);
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
  void BlendBlocksFixed(unsigned char* pDestination,
                        const unsigned char* pBaseBlock,
                        const unsigned char* pMovedBlock,
                        unsigned char pAntiMask) const {
    const std::uint64_t aMask64 = 0x0101010101010101ull * mMask;
    const std::uint64_t aAntiMask64 = 0x0101010101010101ull * pAntiMask;
    std::size_t aByte = 0;
    for (; aByte + 8 <= kBlockSize; aByte += 8) {
      std::uint64_t aBase64 = 0;
      std::uint64_t aMoved64 = 0;
      std::memcpy(&aBase64, pBaseBlock + aByte, sizeof(aBase64));
      std::memcpy(&aMoved64, pMovedBlock + aByte, sizeof(aMoved64));
      const std::uint64_t aResult64 =
          (aBase64 & aAntiMask64) | (aMoved64 & aMask64);
      std::memcpy(pDestination + aByte, &aResult64, sizeof(aResult64));
    }
    for (; aByte < kBlockSize; ++aByte) {
      pDestination[aByte] = static_cast<unsigned char>(
          (pBaseBlock[aByte] & pAntiMask) | (pMovedBlock[aByte] & mMask));
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  template <std::size_t kBlockSize>
  void BlendBlocksFixedSimd(unsigned char* pDestination,
                            const unsigned char* pBaseBlock,
                            const unsigned char* pMovedBlock,
                            unsigned char pAntiMask) const {
    std::size_t aIndex = 0;
#if defined(__AVX2__)
    const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntiMask256 = _mm256_set1_epi8(static_cast<char>(pAntiMask));
    for (; aIndex + 32 <= kBlockSize; aIndex += 32) {
      const __m256i aBase = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pBaseBlock + aIndex));
      const __m256i aMoved = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pMovedBlock + aIndex));
      const __m256i aMaskedBase = _mm256_and_si256(aBase, aAntiMask256);
      const __m256i aMaskedMoved = _mm256_and_si256(aMoved, aMask256);
      const __m256i aResult = _mm256_or_si256(aMaskedBase, aMaskedMoved);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                          aResult);
    }
#endif
    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntiMask128 = _mm_set1_epi8(static_cast<char>(pAntiMask));
    for (; aIndex + 16 <= kBlockSize; aIndex += 16) {
      const __m128i aBase = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pBaseBlock + aIndex));
      const __m128i aMoved = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pMovedBlock + aIndex));
      const __m128i aMaskedBase = _mm_and_si128(aBase, aAntiMask128);
      const __m128i aMaskedMoved = _mm_and_si128(aMoved, aMask128);
      const __m128i aResult = _mm_or_si128(aMaskedBase, aMaskedMoved);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aResult);
    }
    for (; aIndex < kBlockSize; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseBlock[aIndex] & pAntiMask) | (pMovedBlock[aIndex] & mMask));
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  template <std::size_t kBlockSize>
  void BlendBlocksFixedNeon(unsigned char* pDestination,
                            const unsigned char* pBaseBlock,
                            const unsigned char* pMovedBlock,
                            unsigned char pAntiMask) const {
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntiMask = vdupq_n_u8(pAntiMask);
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= kBlockSize; aIndex += 16) {
      const uint8x16_t aBase = vld1q_u8(pBaseBlock + aIndex);
      const uint8x16_t aMoved = vld1q_u8(pMovedBlock + aIndex);
      const uint8x16_t aMaskedBase = vandq_u8(aBase, aAntiMask);
      const uint8x16_t aMaskedMoved = vandq_u8(aMoved, aMask);
      const uint8x16_t aResult = vorrq_u8(aMaskedBase, aMaskedMoved);
      vst1q_u8(pDestination + aIndex, aResult);
    }
    for (; aIndex < kBlockSize; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseBlock[aIndex] & pAntiMask) | (pMovedBlock[aIndex] & mMask));
    }
  }
#endif

  void BlendBlocks(unsigned char* pDestination,
                   const unsigned char* pBaseBlock,
                   const unsigned char* pMovedBlock) const {
    const unsigned char aAntiMask = static_cast<unsigned char>(~mMask);
    for (std::size_t aByte = 0; aByte < mBlockSize; ++aByte) {
      pDestination[aByte] = static_cast<unsigned char>(
          (pBaseBlock[aByte] & aAntiMask) | (pMovedBlock[aByte] & mMask));
    }
  }

  template <std::size_t kBlockSize>
  void ApplyMaskedBlockPassFixed(const unsigned char* pSource,
                                 unsigned char* pDestination,
                                 std::size_t pBlockCount,
                                 int pPhase,
                                 CryptMode pMode) const {
    const unsigned char aAntiMask = static_cast<unsigned char>(~mMask);
    std::size_t aBlock = 0;
    if (pPhase != 0) {
      CopyBlock<kBlockSize>(pDestination, pSource);
      aBlock = 1;
    }

    for (; aBlock + 1 < pBlockCount; aBlock += 2) {
      const std::size_t aLeftOffset = aBlock * kBlockSize;
      const std::size_t aRightOffset = aLeftOffset + kBlockSize;
      if (pMode == CryptMode::kNeon) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        if constexpr (kBlockSize == 16 ||
                      kBlockSize == 32 ||
                      kBlockSize == 48) {
          BlendBlocksFixedNeon<kBlockSize>(pDestination + aLeftOffset,
                                           pSource + aLeftOffset,
                                           pSource + aRightOffset, aAntiMask);
          BlendBlocksFixedNeon<kBlockSize>(pDestination + aRightOffset,
                                           pSource + aRightOffset,
                                           pSource + aLeftOffset, aAntiMask);
        } else {
          BlendBlocksFixed<kBlockSize>(pDestination + aLeftOffset,
                                       pSource + aLeftOffset,
                                       pSource + aRightOffset, aAntiMask);
          BlendBlocksFixed<kBlockSize>(pDestination + aRightOffset,
                                       pSource + aRightOffset,
                                       pSource + aLeftOffset, aAntiMask);
        }
#else
        BlendBlocksFixed<kBlockSize>(pDestination + aLeftOffset,
                                     pSource + aLeftOffset,
                                     pSource + aRightOffset, aAntiMask);
        BlendBlocksFixed<kBlockSize>(pDestination + aRightOffset,
                                     pSource + aRightOffset,
                                     pSource + aLeftOffset, aAntiMask);
#endif
      } else if (pMode == CryptMode::kSimd) {
#if defined(__SSSE3__) || defined(__AVX2__)
        BlendBlocksFixedSimd<kBlockSize>(pDestination + aLeftOffset,
                                         pSource + aLeftOffset,
                                         pSource + aRightOffset, aAntiMask);
        BlendBlocksFixedSimd<kBlockSize>(pDestination + aRightOffset,
                                         pSource + aRightOffset,
                                         pSource + aLeftOffset, aAntiMask);
#else
        BlendBlocksFixed<kBlockSize>(pDestination + aLeftOffset,
                                     pSource + aLeftOffset,
                                     pSource + aRightOffset, aAntiMask);
        BlendBlocksFixed<kBlockSize>(pDestination + aRightOffset,
                                     pSource + aRightOffset,
                                     pSource + aLeftOffset, aAntiMask);
#endif
      } else {
        BlendBlocksFixed<kBlockSize>(pDestination + aLeftOffset,
                                     pSource + aLeftOffset,
                                     pSource + aRightOffset, aAntiMask);
        BlendBlocksFixed<kBlockSize>(pDestination + aRightOffset,
                                     pSource + aRightOffset,
                                     pSource + aLeftOffset, aAntiMask);
      }
    }

    if (aBlock < pBlockCount) {
      const std::size_t aOffset = aBlock * kBlockSize;
      CopyBlock<kBlockSize>(pDestination + aOffset, pSource + aOffset);
    }
  }

  void ApplyMaskedBlockPassVariable(const unsigned char* pSource,
                                    unsigned char* pDestination,
                                    std::size_t pBlockCount,
                                    int pPhase) const {
    if (pPhase == 0) {
      std::size_t aBlock = 0;
      for (; aBlock + 1 < pBlockCount; aBlock += 2) {
        const std::size_t aLeftOffset = aBlock * mBlockSize;
        const std::size_t aRightOffset = (aBlock + 1) * mBlockSize;
        BlendBlocks(pDestination + aLeftOffset, pSource + aLeftOffset,
                    pSource + aRightOffset);
        BlendBlocks(pDestination + aRightOffset, pSource + aRightOffset,
                    pSource + aLeftOffset);
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
      BlendBlocks(pDestination + aLeftOffset, pSource + aLeftOffset,
                  pSource + aRightOffset);
      BlendBlocks(pDestination + aRightOffset, pSource + aRightOffset,
                  pSource + aLeftOffset);
    }
    if (aBlock < pBlockCount) {
      const std::size_t aOffset = aBlock * mBlockSize;
      std::memcpy(pDestination + aOffset, pSource + aOffset, mBlockSize);
    }
  }

  void ApplyMaskedBlockPass(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pBlockCount,
                            int pPhase,
                            CryptMode pMode) const {
    switch (mBlockSize) {
      case 8:
        ApplyMaskedBlockPassFixed<8>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      case 12:
        ApplyMaskedBlockPassFixed<12>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      case 16:
        ApplyMaskedBlockPassFixed<16>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      case 24:
        ApplyMaskedBlockPassFixed<24>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      case 32:
        ApplyMaskedBlockPassFixed<32>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      case 48:
        ApplyMaskedBlockPassFixed<48>(pSource, pDestination,
                                                    pBlockCount, pPhase, pMode);
        return;
      default:
        ApplyMaskedBlockPassVariable(pSource, pDestination, pBlockCount, pPhase);
        return;
    }
  }

  std::size_t mBlockSize;
  std::uint8_t mMask;
  int mRounds;
};

}  // namespace peanutbutter

#endif  // JELLY_RIPPLE_MASK_BLOCK_CIPHER_HPP_
