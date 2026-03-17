#ifndef JELLY_RIPPLE_CIPHER_HPP_
#define JELLY_RIPPLE_CIPHER_HPP_

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

class RippleCipher final : public Crypt {
 public:
  explicit RippleCipher(int pRounds) : mRounds(pRounds > 0 ? pRounds : 1) {}

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
          ApplyPassSoftware(aInput, aOutput, pLength, aPhase);
          break;
        case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
          ApplyPassSimd(aInput, aOutput, pLength, aPhase);
#else
          ApplyPassSoftware(aInput, aOutput, pLength, aPhase);
#endif
          break;
        case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
          ApplyPassNeon(aInput, aOutput, pLength, aPhase);
#else
          ApplyPassSoftware(aInput, aOutput, pLength, aPhase);
#endif
          break;
      }
      aInput = aOutput;
      aOutput = (aOutput == pDestination) ? pWorker : pDestination;
    }
    return true;
  }

  static void ApplyPassSoftware(const unsigned char* pSource,
                                unsigned char* pDestination,
                                std::size_t pLength,
                                int pPhase) {
    constexpr std::uint64_t kLo = 0x00FF00FF00FF00FFull;
    constexpr std::uint64_t kHi = 0xFF00FF00FF00FF00ull;
    if (pPhase == 0) {
      std::size_t aIndex = 0;
      for (; aIndex + 8 <= pLength; aIndex += 8) {
        std::uint64_t aValue = 0;
        std::memcpy(&aValue, pSource + aIndex, sizeof(aValue));
        aValue = ((aValue & kLo) << 8u) | ((aValue & kHi) >> 8u);
        std::memcpy(pDestination + aIndex, &aValue, sizeof(aValue));
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        pDestination[aIndex] = pSource[aIndex + 1];
        pDestination[aIndex + 1] = pSource[aIndex];
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
      aValue = ((aValue & kLo) << 8u) | ((aValue & kHi) >> 8u);
      std::memcpy(pDestination + aIndex, &aValue, sizeof(aValue));
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      pDestination[aIndex] = pSource[aIndex + 1];
      pDestination[aIndex + 1] = pSource[aIndex];
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  static void ApplyPassSimd(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength,
                            int pPhase) {
    if (pPhase == 0) {
      std::size_t aIndex = 0;
#if defined(__AVX2__)
      const __m256i aShuffle256 = _mm256_setr_epi8(
          1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1, 0, 3, 2, 5,
          4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
      for (; aIndex + 32 <= pLength; aIndex += 32) {
        const __m256i aSource = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aIndex));
        const __m256i aShuffled = _mm256_shuffle_epi8(aSource, aShuffle256);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                            aShuffled);
      }
#endif
      const __m128i aShuffle128 = _mm_setr_epi8(
          1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
      for (; aIndex + 16 <= pLength; aIndex += 16) {
        const __m128i aSource = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aIndex));
        const __m128i aShuffled = _mm_shuffle_epi8(aSource, aShuffle128);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                         aShuffled);
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        pDestination[aIndex] = pSource[aIndex + 1];
        pDestination[aIndex + 1] = pSource[aIndex];
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
        1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1, 0, 3, 2, 5,
        4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aIndex));
      const __m256i aShuffled = _mm256_shuffle_epi8(aSource, aShuffle256);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                          aShuffled);
    }
#endif
    const __m128i aShuffle128 = _mm_setr_epi8(
        1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aSource =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSource + aIndex));
      const __m128i aShuffled = _mm_shuffle_epi8(aSource, aShuffle128);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aShuffled);
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      pDestination[aIndex] = pSource[aIndex + 1];
      pDestination[aIndex + 1] = pSource[aIndex];
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  static void ApplyPassNeon(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength,
                            int pPhase) {
    if (pPhase == 0) {
      std::size_t aIndex = 0;
      for (; aIndex + 16 <= pLength; aIndex += 16) {
        const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
        const uint8x16_t aShuffled = vrev16q_u8(aSource);
        vst1q_u8(pDestination + aIndex, aShuffled);
      }
      for (; aIndex + 1 < pLength; aIndex += 2) {
        pDestination[aIndex] = pSource[aIndex + 1];
        pDestination[aIndex + 1] = pSource[aIndex];
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
      const uint8x16_t aShuffled = vrev16q_u8(aSource);
      vst1q_u8(pDestination + aIndex, aShuffled);
    }
    for (; aIndex + 1 < pLength; aIndex += 2) {
      pDestination[aIndex] = pSource[aIndex + 1];
      pDestination[aIndex + 1] = pSource[aIndex];
    }
    if (aIndex < pLength) {
      pDestination[aIndex] = pSource[aIndex];
    }
  }
#endif

  int mRounds;
};

}  // namespace peanutbutter

#endif  // JELLY_RIPPLE_CIPHER_HPP_
