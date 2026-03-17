#ifndef JELLY_ROTATE_MASK_CIPHER_HPP_
#define JELLY_ROTATE_MASK_CIPHER_HPP_

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

class RotateMaskCipher final : public Crypt {
 public:
  RotateMaskCipher(std::uint8_t pMask, int pShift)
      : mMask(pMask), mShift(pShift) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, mShift, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, -mShift, pMode);
  }

 private:
  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength,
             int pShift,
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
    if (pSource == pDestination && pLength != 0) {
      return false;
    }

    const std::size_t aRotation = NormalizeShift(pShift, pLength);
    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyScalar(pSource, pDestination, pLength, aRotation);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplySimd(pSource, pDestination, pLength, aRotation);
#else
        return false;
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyNeon(pSource, pDestination, pLength, aRotation);
#else
        return false;
#endif
    }
    return false;
  }

  static std::size_t NormalizeShift(int pShift, std::size_t pLength) {
    const int aLength = static_cast<int>(pLength);
    int aRotation = pShift % aLength;
    if (aRotation < 0) {
      aRotation += aLength;
    }
    return static_cast<std::size_t>(aRotation);
  }

  bool ApplyScalar(const unsigned char* pSource,
                   unsigned char* pDestination,
                   std::size_t pLength,
                   std::size_t pRotation) const {
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      const std::size_t aSourceIndex =
          aIndex + pRotation < pLength ? aIndex + pRotation
                                       : aIndex + pRotation - pLength;
      pDestination[aIndex] = static_cast<unsigned char>(
          (pSource[aIndex] & aAntimask) | (pSource[aSourceIndex] & mMask));
    }
    return true;
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void BlendSpanSimd(const unsigned char* pBaseSource,
                     const unsigned char* pMaskedSource,
                     unsigned char* pDestination,
                     std::size_t pLength) const {
#if defined(__AVX2__)
    const __m256i aMask = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntimask =
        _mm256_xor_si256(aMask, _mm256_set1_epi8(static_cast<char>(0xff)));
    std::size_t aIndex = 0;
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aBase = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pBaseSource + aIndex));
      const __m256i aMasked = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pMaskedSource + aIndex));
      const __m256i aResult =
          _mm256_or_si256(_mm256_and_si256(aBase, aAntimask),
                          _mm256_and_si256(aMasked, aMask));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                          aResult);
    }
    const unsigned char aAntimaskScalar = static_cast<unsigned char>(~mMask);
    for (; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseSource[aIndex] & aAntimaskScalar) |
          (pMaskedSource[aIndex] & mMask));
    }
#else
    const __m128i aMask = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask =
        _mm_xor_si128(aMask, _mm_set1_epi8(static_cast<char>(0xff)));
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aBase = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pBaseSource + aIndex));
      const __m128i aMasked = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pMaskedSource + aIndex));
      const __m128i aResult =
          _mm_or_si128(_mm_and_si128(aBase, aAntimask),
                       _mm_and_si128(aMasked, aMask));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aResult);
    }
    const unsigned char aAntimaskScalar = static_cast<unsigned char>(~mMask);
    for (; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseSource[aIndex] & aAntimaskScalar) |
          (pMaskedSource[aIndex] & mMask));
    }
#endif
  }

  bool ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength,
                 std::size_t pRotation) const {
    const std::size_t aFirstSpan = pLength - pRotation;
    BlendSpanSimd(pSource, pSource + pRotation, pDestination, aFirstSpan);
    if (pRotation != 0) {
      BlendSpanSimd(pSource + aFirstSpan, pSource, pDestination + aFirstSpan,
                    pRotation);
    }
    return true;
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void BlendSpanNeon(const unsigned char* pBaseSource,
                     const unsigned char* pMaskedSource,
                     unsigned char* pDestination,
                     std::size_t pLength) const {
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const uint8x16_t aBase = vld1q_u8(pBaseSource + aIndex);
      const uint8x16_t aMasked = vld1q_u8(pMaskedSource + aIndex);
      const uint8x16_t aResult =
          vorrq_u8(vandq_u8(aBase, aAntimask), vandq_u8(aMasked, aMask));
      vst1q_u8(pDestination + aIndex, aResult);
    }

    const unsigned char aAntimaskScalar = static_cast<unsigned char>(~mMask);
    for (; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseSource[aIndex] & aAntimaskScalar) |
          (pMaskedSource[aIndex] & mMask));
    }
  }

  bool ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength,
                 std::size_t pRotation) const {
    const std::size_t aFirstSpan = pLength - pRotation;
    BlendSpanNeon(pSource, pSource + pRotation, pDestination, aFirstSpan);
    if (pRotation != 0) {
      BlendSpanNeon(pSource + aFirstSpan, pSource, pDestination + aFirstSpan,
                    pRotation);
    }
    return true;
  }
#endif

  std::uint8_t mMask;
  int mShift;
};

}  // namespace peanutbutter

#endif  // JELLY_ROTATE_MASK_CIPHER_HPP_
