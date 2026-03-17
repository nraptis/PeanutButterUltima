#ifndef JELLY_INVERT_MASK_CIPHER_HPP_
#define JELLY_INVERT_MASK_CIPHER_HPP_

#include <cstddef>
#include <cstdint>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__SSSE3__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class InvertMaskCipher final : public Crypt {
 public:
  explicit InvertMaskCipher(std::uint8_t pMask) : mMask(pMask) {}

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
  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength,
             CryptMode pMode) const {
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

  void ApplySoftware(const unsigned char* pSource,
                     unsigned char* pDestination,
                     std::size_t pLength) const {
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      unsigned char aByte = pSource[aIndex];
      const unsigned char aMaskedBits =
          static_cast<unsigned char>((~aByte) & mMask);
      aByte = static_cast<unsigned char>(aByte & aAntimask);
      aByte = static_cast<unsigned char>(aByte | aMaskedBits);
      pDestination[aIndex] = aByte;
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    std::size_t aOffset = 0;
#if defined(__AVX2__)
    const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntimask256 = _mm256_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aOffset));
      const __m256i aMasked = _mm256_andnot_si256(aSource, aMask256);
      const __m256i aBase = _mm256_and_si256(aSource, aAntimask256);
      const __m256i aResult = _mm256_or_si256(aBase, aMasked);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aOffset),
                          aResult);
    }
#endif

    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask128 = _mm_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const __m128i aSource = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aOffset));
      const __m128i aMasked = _mm_andnot_si128(aSource, aMask128);
      const __m128i aBase = _mm_and_si128(aSource, aAntimask128);
      const __m128i aResult = _mm_or_si128(aBase, aMasked);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aOffset),
                       aResult);
    }

    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (; aOffset < pLength; ++aOffset) {
      unsigned char aByte = pSource[aOffset];
      const unsigned char aMaskedBits =
          static_cast<unsigned char>((~aByte) & mMask);
      aByte = static_cast<unsigned char>(aByte & aAntimask);
      aByte = static_cast<unsigned char>(aByte | aMaskedBits);
      pDestination[aOffset] = aByte;
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aOffset);
      const uint8x16_t aMasked = vandq_u8(vmvnq_u8(aSource), aMask);
      const uint8x16_t aBase = vandq_u8(aSource, aAntimask);
      const uint8x16_t aResult = vorrq_u8(aBase, aMasked);
      vst1q_u8(pDestination + aOffset, aResult);
    }

    const unsigned char aAntimaskScalar = static_cast<unsigned char>(~mMask);
    for (; aOffset < pLength; ++aOffset) {
      unsigned char aByte = pSource[aOffset];
      const unsigned char aMaskedBits =
          static_cast<unsigned char>((~aByte) & mMask);
      aByte = static_cast<unsigned char>(aByte & aAntimaskScalar);
      aByte = static_cast<unsigned char>(aByte | aMaskedBits);
      pDestination[aOffset] = aByte;
    }
  }
#endif

  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_INVERT_MASK_CIPHER_HPP_
