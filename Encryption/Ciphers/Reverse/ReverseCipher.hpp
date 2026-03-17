#ifndef JELLY_REVERSE_CIPHER_HPP_
#define JELLY_REVERSE_CIPHER_HPP_

#include <cstddef>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__SSSE3__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class ReverseCipher final : public Crypt {
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
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = pSource[pLength - 1 - aIndex];
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  static void ApplySimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    std::size_t aOffset = 0;
#if defined(__AVX2__)
    const __m256i aLanes256 = _mm256_setr_epi8(
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12,
        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const std::size_t aSourceOffset = pLength - aOffset - 32;
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aSourceOffset));
      __m256i aReversed = _mm256_shuffle_epi8(aSource, aLanes256);
      aReversed = _mm256_permute2x128_si256(aReversed, aReversed, 0x01);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aOffset),
                         aReversed);
    }
#endif
    const __m128i aLanes128 =
        _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const std::size_t aSourceOffset = pLength - aOffset - 16;
      const __m128i aSource = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aSourceOffset));
      const __m128i aReversed = _mm_shuffle_epi8(aSource, aLanes128);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aOffset),
                       aReversed);
    }
    for (; aOffset < pLength; ++aOffset) {
      pDestination[aOffset] = pSource[pLength - 1 - aOffset];
    }
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

    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const std::size_t aSourceOffset = pLength - aOffset - 16;
      const uint8x16_t aSource = vld1q_u8(pSource + aSourceOffset);
      const uint8x16_t aReversed = vqtbl1q_u8(aSource, aLanes);
      vst1q_u8(pDestination + aOffset, aReversed);
    }
    for (; aOffset < pLength; ++aOffset) {
      pDestination[aOffset] = pSource[pLength - 1 - aOffset];
    }
  }
#endif
};

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_CIPHER_HPP_
