#ifndef JELLY_REVERSE_MASK_CIPHER_HPP_
#define JELLY_REVERSE_MASK_CIPHER_HPP_

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

class ReverseMaskCipher final : public Crypt {
 public:
  explicit ReverseMaskCipher(std::uint8_t pMask) : mMask(pMask) {}

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
      const unsigned char aBase =
          static_cast<unsigned char>(pSource[aIndex] & aAntimask);
      const unsigned char aMasked =
          static_cast<unsigned char>(pSource[pLength - 1 - aIndex] & mMask);
      pDestination[aIndex] = static_cast<unsigned char>(aBase | aMasked);
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    std::size_t aOffset = 0;
#if defined(__AVX2__)
    const __m256i aLanes256 = _mm256_setr_epi8(
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12,
        11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m256i aMask = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntimask = _mm256_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const std::size_t aSourceOffset = pLength - aOffset - 32;
      const __m256i aBase =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pSource + aOffset));
      const __m256i aBack = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aSourceOffset));
      __m256i aMaskedReversed = _mm256_shuffle_epi8(aBack, aLanes256);
      aMaskedReversed =
          _mm256_permute2x128_si256(aMaskedReversed, aMaskedReversed, 0x01);
      const __m256i aResult =
          _mm256_or_si256(_mm256_and_si256(aBase, aAntimask),
                          _mm256_and_si256(aMaskedReversed, aMask));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aOffset),
                         aResult);
    }
#endif
    const __m128i aLanes128 =
        _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask128 = _mm_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const std::size_t aSourceOffset = pLength - aOffset - 16;
      const __m128i aBase = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aOffset));
      const __m128i aBack = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aSourceOffset));
      const __m128i aMaskedReversed = _mm_shuffle_epi8(aBack, aLanes128);
      const __m128i aResult = _mm_or_si128(_mm_and_si128(aBase, aAntimask128),
                                           _mm_and_si128(aMaskedReversed,
                                                         aMask128));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aOffset),
                       aResult);
    }
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (; aOffset < pLength; ++aOffset) {
      const unsigned char aBase =
          static_cast<unsigned char>(pSource[aOffset] & aAntimask);
      const unsigned char aMasked =
          static_cast<unsigned char>(pSource[pLength - 1 - aOffset] & mMask);
      pDestination[aOffset] = static_cast<unsigned char>(aBase | aMasked);
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    alignas(16) static const unsigned char kLaneMap[16] = {15, 14, 13, 12, 11,
                                                            10, 9,  8,  7,  6,
                                                            5,  4,  3,  2,  1,
                                                            0};
    const uint8x16_t aLanes = vld1q_u8(kLaneMap);
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const std::size_t aSourceOffset = pLength - aOffset - 16;
      const uint8x16_t aBase = vld1q_u8(pSource + aOffset);
      const uint8x16_t aBack = vld1q_u8(pSource + aSourceOffset);
      const uint8x16_t aMaskedReversed = vqtbl1q_u8(aBack, aLanes);
      const uint8x16_t aResult =
          vorrq_u8(vandq_u8(aBase, aAntimask), vandq_u8(aMaskedReversed, aMask));
      vst1q_u8(pDestination + aOffset, aResult);
    }

    const unsigned char aAntimaskScalar = static_cast<unsigned char>(~mMask);
    for (; aOffset < pLength; ++aOffset) {
      const unsigned char aBase =
          static_cast<unsigned char>(pSource[aOffset] & aAntimaskScalar);
      const unsigned char aMasked =
          static_cast<unsigned char>(pSource[pLength - 1 - aOffset] & mMask);
      pDestination[aOffset] = static_cast<unsigned char>(aBase | aMasked);
    }
  }
#endif

  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_MASK_CIPHER_HPP_
