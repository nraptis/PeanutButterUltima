#ifndef JELLY_REVERSE_BLOCK_BYTE_CIPHER_HPP_
#define JELLY_REVERSE_BLOCK_BYTE_CIPHER_HPP_

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

template <std::size_t tBlockSize>
class ReverseBlockByteCipher final : public Crypt {
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
    if constexpr (tBlockSize == 8) {
      for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
        const std::size_t aBase = aBlock * tBlockSize;
        std::uint64_t aValue = 0;
        std::memcpy(&aValue, pSource + aBase, sizeof(aValue));
        aValue = __builtin_bswap64(aValue);
        std::memcpy(pDestination + aBase, &aValue, sizeof(aValue));
      }
      return;
    }

    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aBase = aBlock * tBlockSize;
      for (std::size_t aByte = 0; aByte < tBlockSize; ++aByte) {
        pDestination[aBase + aByte] =
            pSource[aBase + (tBlockSize - 1 - aByte)];
      }
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  static void ApplySimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    if constexpr (tBlockSize == 16) {
      const __m128i aLanes =
          _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const __m128i aSource = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase));
        const __m128i aReversed = _mm_shuffle_epi8(aSource, aLanes);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase),
                         aReversed);
      }
      return;
    }
#if defined(__AVX2__)
    if constexpr (tBlockSize == 32) {
      const __m256i aLanes = _mm256_setr_epi8(
          15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12,
          11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const __m256i aSource = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aBase));
        __m256i aReversed = _mm256_shuffle_epi8(aSource, aLanes);
        aReversed = _mm256_permute2x128_si256(aReversed, aReversed, 0x01);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aBase),
                           aReversed);
      }
      return;
    }
#endif
    if constexpr (tBlockSize == 48) {
      const __m128i aLanes =
          _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const __m128i aSource0 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase));
        const __m128i aSource1 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase + 16));
        const __m128i aSource2 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase + 32));
        const __m128i aReverse0 = _mm_shuffle_epi8(aSource2, aLanes);
        const __m128i aReverse1 = _mm_shuffle_epi8(aSource1, aLanes);
        const __m128i aReverse2 = _mm_shuffle_epi8(aSource0, aLanes);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase),
                         aReverse0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase + 16),
                         aReverse1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase + 32),
                         aReverse2);
      }
      return;
    }
    ApplySoftware(pSource, pDestination, pLength);
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  static void ApplyNeon(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    alignas(16) static const unsigned char kLaneMap[16] = {15, 14, 13, 12, 11,
                                                            10, 9,  8,  7,  6,
                                                            5,  4,  3,  2,  1,
                                                            0};
    const uint8x16_t aLanes = vld1q_u8(kLaneMap);

    if constexpr (tBlockSize == 16) {
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const uint8x16_t aSource = vld1q_u8(pSource + aBase);
        const uint8x16_t aReversed = vqtbl1q_u8(aSource, aLanes);
        vst1q_u8(pDestination + aBase, aReversed);
      }
      return;
    }

    if constexpr (tBlockSize == 32) {
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const uint8x16_t aSource0 = vld1q_u8(pSource + aBase);
        const uint8x16_t aSource1 = vld1q_u8(pSource + aBase + 16);
        const uint8x16_t aReverse0 = vqtbl1q_u8(aSource1, aLanes);
        const uint8x16_t aReverse1 = vqtbl1q_u8(aSource0, aLanes);
        vst1q_u8(pDestination + aBase, aReverse0);
        vst1q_u8(pDestination + aBase + 16, aReverse1);
      }
      return;
    }

    if constexpr (tBlockSize == 48) {
      for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
        const uint8x16_t aSource0 = vld1q_u8(pSource + aBase);
        const uint8x16_t aSource1 = vld1q_u8(pSource + aBase + 16);
        const uint8x16_t aSource2 = vld1q_u8(pSource + aBase + 32);
        const uint8x16_t aReverse0 = vqtbl1q_u8(aSource2, aLanes);
        const uint8x16_t aReverse1 = vqtbl1q_u8(aSource1, aLanes);
        const uint8x16_t aReverse2 = vqtbl1q_u8(aSource0, aLanes);
        vst1q_u8(pDestination + aBase, aReverse0);
        vst1q_u8(pDestination + aBase + 16, aReverse1);
        vst1q_u8(pDestination + aBase + 32, aReverse2);
      }
      return;
    }

    ApplySoftware(pSource, pDestination, pLength);
  }
#endif
};

using ReverseBlockByteCipher08 = ReverseBlockByteCipher<8>;
using ReverseBlockByteCipher12 = ReverseBlockByteCipher<12>;
using ReverseBlockByteCipher16 = ReverseBlockByteCipher<16>;
using ReverseBlockByteCipher24 = ReverseBlockByteCipher<24>;
using ReverseBlockByteCipher32 = ReverseBlockByteCipher<32>;
using ReverseBlockByteCipher48 = ReverseBlockByteCipher<48>;

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_BLOCK_BYTE_CIPHER_HPP_
