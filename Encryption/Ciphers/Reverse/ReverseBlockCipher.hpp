#ifndef JELLY_REVERSE_BLOCK_CIPHER_HPP_
#define JELLY_REVERSE_BLOCK_CIPHER_HPP_

#include <cstddef>
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

template <std::size_t tBlockSize>
class ReverseBlockCipher final : public Crypt {
 public:
  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, pMode);
  }

 private:
  static bool Apply(const unsigned char* pSource,
                    unsigned char* pDestination,
                    std::size_t pLength,
                    CryptMode pMode) {
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if ((pLength % tBlockSize) != 0) {
      return false;
    }
    if (pSource == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination) {
      return false;
    }

    switch (pMode) {
      case CryptMode::kNormal:
        ApplySoftware(pSource, pDestination, pLength);
        return true;
      case CryptMode::kSimd:
#if defined(__SSSE3__) || defined(__AVX2__)
        ApplySimd(pSource, pDestination, pLength);
        return true;
#else
        return false;
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        ApplyNeon(pSource, pDestination, pLength);
        return true;
#else
        return false;
#endif
    }
    return false;
  }

  static void ApplySoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) {
    const std::size_t aBlockCount = pLength / tBlockSize;
    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aDestinationOffset = aBlock * tBlockSize;
      const std::size_t aSourceOffset =
          (aBlockCount - 1 - aBlock) * tBlockSize;
      std::memcpy(pDestination + aDestinationOffset, pSource + aSourceOffset,
                  tBlockSize);
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  static void ApplySimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    if constexpr (tBlockSize == 16) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const __m128i aValue = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aSourceOffset));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination +
                                                    aDestinationOffset),
                         aValue);
      }
      return;
    }
#if defined(__AVX2__)
    if constexpr (tBlockSize == 32) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const __m256i aValue = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aSourceOffset));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination +
                                                       aDestinationOffset),
                           aValue);
      }
      return;
    }
    if constexpr (tBlockSize == 48) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const __m256i aHead = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aSourceOffset));
        const __m128i aTail = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
            pSource + aSourceOffset + 32));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination +
                                                       aDestinationOffset),
                           aHead);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination +
                                                    aDestinationOffset + 32),
                         aTail);
      }
      return;
    }
#endif
    ApplySoftware(pSource, pDestination, pLength);
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  static void ApplyNeon(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    if constexpr (tBlockSize == 16) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const uint8x16_t aValue = vld1q_u8(pSource + aSourceOffset);
        vst1q_u8(pDestination + aDestinationOffset, aValue);
      }
      return;
    }
    if constexpr (tBlockSize == 32) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const uint8x16_t aValue0 = vld1q_u8(pSource + aSourceOffset);
        const uint8x16_t aValue1 = vld1q_u8(pSource + aSourceOffset + 16);
        vst1q_u8(pDestination + aDestinationOffset, aValue0);
        vst1q_u8(pDestination + aDestinationOffset + 16, aValue1);
      }
      return;
    }
    if constexpr (tBlockSize == 48) {
      const std::size_t aBlockCount = pLength / tBlockSize;
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aDestinationOffset = aBlock * tBlockSize;
        const std::size_t aSourceOffset =
            (aBlockCount - 1 - aBlock) * tBlockSize;
        const uint8x16_t aValue0 = vld1q_u8(pSource + aSourceOffset);
        const uint8x16_t aValue1 = vld1q_u8(pSource + aSourceOffset + 16);
        const uint8x16_t aValue2 = vld1q_u8(pSource + aSourceOffset + 32);
        vst1q_u8(pDestination + aDestinationOffset, aValue0);
        vst1q_u8(pDestination + aDestinationOffset + 16, aValue1);
        vst1q_u8(pDestination + aDestinationOffset + 32, aValue2);
      }
      return;
    }
    ApplySoftware(pSource, pDestination, pLength);
  }
#endif
};

using ReverseBlockCipher08 = ReverseBlockCipher<8>;
using ReverseBlockCipher12 = ReverseBlockCipher<12>;
using ReverseBlockCipher16 = ReverseBlockCipher<16>;
using ReverseBlockCipher24 = ReverseBlockCipher<24>;
using ReverseBlockCipher32 = ReverseBlockCipher<32>;
using ReverseBlockCipher48 = ReverseBlockCipher<48>;

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_BLOCK_CIPHER_HPP_
