#ifndef JELLY_RIPPLE_MASK_CIPHER_HPP_
#define JELLY_RIPPLE_MASK_CIPHER_HPP_

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

class RippleMaskCipher final : public Crypt {
 public:
  RippleMaskCipher(std::uint8_t pMask, int pRounds)
      : mMask(pMask), mRounds(pRounds > 0 ? pRounds : 1) {}

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
  bool ValidateInputs(const unsigned char* pSource,
                      unsigned char* pWorker,
                      unsigned char* pDestination,
                      std::size_t pLength) const {
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination || pSource == pWorker ||
        pDestination == pWorker) {
      return false;
    }
    return true;
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pWorker,
             unsigned char* pDestination,
             std::size_t pLength,
             bool pEncrypt,
             CryptMode pMode) const {
    if (!ValidateInputs(pSource, pWorker, pDestination, pLength)) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }

    const unsigned char* aInput = pSource;
    unsigned char* aOutput =
        ((mRounds & 1) == 0) ? pWorker : pDestination;
    int aRound = pEncrypt ? 0 : (mRounds - 1);
    const int aStep = pEncrypt ? 1 : -1;
    for (int aPass = 0; aPass < mRounds; ++aPass, aRound += aStep) {
      const int aPhase = aRound & 1;
      switch (pMode) {
        case CryptMode::kNormal:
          ApplyMaskedPassSoftware(aInput, aOutput, pLength, aPhase);
          break;
        case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
          ApplyMaskedPassSimd(aInput, aOutput, pLength, aPhase);
#else
          ApplyMaskedPassSoftware(aInput, aOutput, pLength, aPhase);
#endif
          break;
        case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
          ApplyMaskedPassNeon(aInput, aOutput, pLength, aPhase);
#else
          ApplyMaskedPassSoftware(aInput, aOutput, pLength, aPhase);
#endif
          break;
      }
      aInput = aOutput;
      aOutput = (aOutput == pDestination) ? pWorker : pDestination;
    }
    return true;
  }

  void ApplyMaskedPassSoftware(const unsigned char* pSource,
                               unsigned char* pDestination,
                               std::size_t pLength,
                               int pPhase) const {
    constexpr std::uint64_t kSwapLo = 0x00FF00FF00FF00FFull;
    constexpr std::uint64_t kSwapHi = 0xFF00FF00FF00FF00ull;
    const std::uint64_t aMask64 = 0x0101010101010101ull * mMask;
    const std::uint64_t aAntiMask64 =
        0x0101010101010101ull * static_cast<unsigned char>(~mMask);
    const unsigned char aAntiMask = static_cast<unsigned char>(~mMask);
    if (pPhase == 0) {
      std::size_t aIndex = 0;
      for (; aIndex + 8 <= pLength; aIndex += 8) {
        std::uint64_t aValue = 0;
        std::memcpy(&aValue, pSource + aIndex, sizeof(aValue));
        const std::uint64_t aSwapped =
            ((aValue & kSwapLo) << 8u) | ((aValue & kSwapHi) >> 8u);
        const std::uint64_t aResult =
            (aValue & aAntiMask64) | (aSwapped & aMask64);
        std::memcpy(pDestination + aIndex, &aResult, sizeof(aResult));
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        const unsigned char aLeft = pSource[aIndex];
        const unsigned char aRight = pSource[aIndex + 1];
        pDestination[aIndex] = static_cast<unsigned char>(
            (aLeft & aAntiMask) | (aRight & mMask));
        pDestination[aIndex + 1] = static_cast<unsigned char>(
            (aRight & aAntiMask) | (aLeft & mMask));
      }
      if (aIndex < pLength) {
        pDestination[aIndex] = pSource[aIndex];
      }
      return;
    }

    pDestination[0] = pSource[0];
    std::size_t aIndex = 1;
    for (; aIndex + 8 <= pLength; aIndex += 8) {
      std::uint64_t aValue = 0;
      std::memcpy(&aValue, pSource + aIndex, sizeof(aValue));
      const std::uint64_t aSwapped =
          ((aValue & kSwapLo) << 8u) | ((aValue & kSwapHi) >> 8u);
      const std::uint64_t aResult =
          (aValue & aAntiMask64) | (aSwapped & aMask64);
      std::memcpy(pDestination + aIndex, &aResult, sizeof(aResult));
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      const unsigned char aLeft = pSource[aIndex];
      const unsigned char aRight = pSource[aIndex + 1];
      pDestination[aIndex] = static_cast<unsigned char>(
          (aLeft & aAntiMask) | (aRight & mMask));
      pDestination[aIndex + 1] = static_cast<unsigned char>(
          (aRight & aAntiMask) | (aLeft & mMask));
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void ApplyMaskedPassSimd(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pLength,
                           int pPhase) const {
    if (pPhase == 0) {
      std::size_t aIndex = 0;
#if defined(__AVX2__)
      const __m256i aShuffle256 = _mm256_setr_epi8(
          1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1, 0, 3, 2, 5,
          4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
      const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(mMask));
      const __m256i aAntiMask256 = _mm256_set1_epi8(
          static_cast<char>(static_cast<unsigned char>(~mMask)));
      for (; aIndex + 32 <= pLength; aIndex += 32) {
        const __m256i aSource = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aIndex));
        const __m256i aSwapped = _mm256_shuffle_epi8(aSource, aShuffle256);
        const __m256i aBase = _mm256_and_si256(aSource, aAntiMask256);
        const __m256i aMoved = _mm256_and_si256(aSwapped, aMask256);
        const __m256i aResult = _mm256_or_si256(aBase, aMoved);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                            aResult);
      }
#endif
      const __m128i aShuffle128 = _mm_setr_epi8(
          1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
      const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
      const __m128i aAntiMask128 = _mm_set1_epi8(
          static_cast<char>(static_cast<unsigned char>(~mMask)));
      for (; aIndex + 16 <= pLength; aIndex += 16) {
        const __m128i aSource = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aIndex));
        const __m128i aSwapped = _mm_shuffle_epi8(aSource, aShuffle128);
        const __m128i aBase = _mm_and_si128(aSource, aAntiMask128);
        const __m128i aMoved = _mm_and_si128(aSwapped, aMask128);
        const __m128i aResult = _mm_or_si128(aBase, aMoved);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                         aResult);
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        const unsigned char aLeft = pSource[aIndex];
        const unsigned char aRight = pSource[aIndex + 1];
        pDestination[aIndex] = static_cast<unsigned char>(
            (aLeft & static_cast<unsigned char>(~mMask)) | (aRight & mMask));
        pDestination[aIndex + 1] = static_cast<unsigned char>(
            (aRight & static_cast<unsigned char>(~mMask)) | (aLeft & mMask));
      }
      if (aIndex < pLength) {
        pDestination[aIndex] = pSource[aIndex];
      }
      return;
    }

    pDestination[0] = pSource[0];
    std::size_t aIndex = 1;
#if defined(__AVX2__)
    const __m256i aShuffle256 = _mm256_setr_epi8(
        1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1, 0, 3, 2, 5, 4,
        7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntiMask256 = _mm256_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aIndex));
      const __m256i aSwapped = _mm256_shuffle_epi8(aSource, aShuffle256);
      const __m256i aBase = _mm256_and_si256(aSource, aAntiMask256);
      const __m256i aMoved = _mm256_and_si256(aSwapped, aMask256);
      const __m256i aResult = _mm256_or_si256(aBase, aMoved);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                          aResult);
    }
#endif
    const __m128i aShuffle128 = _mm_setr_epi8(
        1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntiMask128 = _mm_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aSource =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSource + aIndex));
      const __m128i aSwapped = _mm_shuffle_epi8(aSource, aShuffle128);
      const __m128i aBase = _mm_and_si128(aSource, aAntiMask128);
      const __m128i aMoved = _mm_and_si128(aSwapped, aMask128);
      const __m128i aResult = _mm_or_si128(aBase, aMoved);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aResult);
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      const unsigned char aLeft = pSource[aIndex];
      const unsigned char aRight = pSource[aIndex + 1];
      pDestination[aIndex] = static_cast<unsigned char>(
          (aLeft & static_cast<unsigned char>(~mMask)) | (aRight & mMask));
      pDestination[aIndex + 1] = static_cast<unsigned char>(
          (aRight & static_cast<unsigned char>(~mMask)) | (aLeft & mMask));
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void ApplyMaskedPassNeon(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pLength,
                           int pPhase) const {
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntiMask = vdupq_n_u8(static_cast<unsigned char>(~mMask));
    if (pPhase == 0) {
      std::size_t aIndex = 0;
      for (; aIndex + 16 <= pLength; aIndex += 16) {
        const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
        const uint8x16_t aSwapped = vrev16q_u8(aSource);
        const uint8x16_t aBase = vandq_u8(aSource, aAntiMask);
        const uint8x16_t aMoved = vandq_u8(aSwapped, aMask);
        const uint8x16_t aResult = vorrq_u8(aBase, aMoved);
        vst1q_u8(pDestination + aIndex, aResult);
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        const unsigned char aLeft = pSource[aIndex];
        const unsigned char aRight = pSource[aIndex + 1];
        pDestination[aIndex] = static_cast<unsigned char>(
            (aLeft & static_cast<unsigned char>(~mMask)) | (aRight & mMask));
        pDestination[aIndex + 1] = static_cast<unsigned char>(
            (aRight & static_cast<unsigned char>(~mMask)) | (aLeft & mMask));
      }
      if (aIndex < pLength) {
        pDestination[aIndex] = pSource[aIndex];
      }
      return;
    }

    pDestination[0] = pSource[0];
    std::size_t aIndex = 1;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
      const uint8x16_t aSwapped = vrev16q_u8(aSource);
      const uint8x16_t aBase = vandq_u8(aSource, aAntiMask);
      const uint8x16_t aMoved = vandq_u8(aSwapped, aMask);
      const uint8x16_t aResult = vorrq_u8(aBase, aMoved);
      vst1q_u8(pDestination + aIndex, aResult);
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      const unsigned char aLeft = pSource[aIndex];
      const unsigned char aRight = pSource[aIndex + 1];
      pDestination[aIndex] = static_cast<unsigned char>(
          (aLeft & static_cast<unsigned char>(~mMask)) | (aRight & mMask));
      pDestination[aIndex + 1] = static_cast<unsigned char>(
          (aRight & static_cast<unsigned char>(~mMask)) | (aLeft & mMask));
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }
#endif

  std::uint8_t mMask;
  int mRounds;
};

}  // namespace peanutbutter

#endif  // JELLY_RIPPLE_MASK_CIPHER_HPP_
