#ifndef JELLY_INVERT_CIPHER_HPP_
#define JELLY_INVERT_CIPHER_HPP_

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

class InvertCipher final : public Crypt {
 public:
  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pWorker;
    return Apply(pSource, pDestination, pLength, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pWorker;
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
      pDestination[aIndex] = static_cast<unsigned char>(~pSource[aIndex]);
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  static void ApplySimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    std::size_t aOffset = 0;
#if defined(__AVX2__)
    const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(0xFF));
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aOffset));
      const __m256i aResult = _mm256_xor_si256(aSource, aMask256);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aOffset),
                          aResult);
    }
#endif

    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(0xFF));
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const __m128i aSource = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aOffset));
      const __m128i aResult = _mm_xor_si128(aSource, aMask128);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aOffset),
                       aResult);
    }

    for (; aOffset < pLength; ++aOffset) {
      pDestination[aOffset] = static_cast<unsigned char>(~pSource[aOffset]);
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  static void ApplyNeon(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aOffset);
      const uint8x16_t aResult = vmvnq_u8(aSource);
      vst1q_u8(pDestination + aOffset, aResult);
    }

    for (; aOffset < pLength; ++aOffset) {
      pDestination[aOffset] = static_cast<unsigned char>(~pSource[aOffset]);
    }
  }
#endif
};

}  // namespace peanutbutter

#endif  // JELLY_INVERT_CIPHER_HPP_
