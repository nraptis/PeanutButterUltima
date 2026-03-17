#ifndef JELLY_SPLINT_MASK_CIPHER_HPP_
#define JELLY_SPLINT_MASK_CIPHER_HPP_

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

class SplintMaskCipher final : public Crypt {
 public:
  explicit SplintMaskCipher(std::uint8_t pMask) : mMask(pMask) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    if (!ValidateInputs(pSource, pWorker, pDestination, pLength)) {
      return false;
    }

    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyEncryptSoftware(pSource, pWorker, pDestination, pLength);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplyEncryptSimd(pSource, pWorker, pDestination, pLength);
#else
        return ApplyEncryptSoftware(pSource, pWorker, pDestination, pLength);
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyEncryptNeon(pSource, pWorker, pDestination, pLength);
#else
        return ApplyEncryptSoftware(pSource, pWorker, pDestination, pLength);
#endif
    }
    return false;
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    if (!ValidateInputs(pSource, pWorker, pDestination, pLength)) {
      return false;
    }

    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyDecryptSoftware(pSource, pWorker, pDestination, pLength);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplyDecryptSimd(pSource, pWorker, pDestination, pLength);
#else
        return ApplyDecryptSoftware(pSource, pWorker, pDestination, pLength);
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyDecryptNeon(pSource, pWorker, pDestination, pLength);
#else
        return ApplyDecryptSoftware(pSource, pWorker, pDestination, pLength);
#endif
    }
    return false;
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
    if (pSource == pDestination) {
      return false;
    }
    return true;
  }

  bool ApplyEncryptSoftware(const unsigned char* pSource,
                            unsigned char* pWorker,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopyScalar(pSource, aWorker, pLength);
    SplintEncrypt(aWorker, pDestination, pLength);
    AntiMaskOrScalar(pSource, pDestination, pLength);
    return true;
  }

  bool ApplyDecryptSoftware(const unsigned char* pSource,
                            unsigned char* pWorker,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopyScalar(pSource, aWorker, pLength);
    SplintDecrypt(aWorker, pDestination, pLength);
    AntiMaskOrScalar(pSource, pDestination, pLength);
    return true;
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  bool ApplyEncryptSimd(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopySimd(pSource, aWorker, pLength);
    SplintEncrypt(aWorker, pDestination, pLength);
    AntiMaskOrSimd(pSource, pDestination, pLength);
    return true;
  }

  bool ApplyDecryptSimd(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopySimd(pSource, aWorker, pLength);
    SplintDecrypt(aWorker, pDestination, pLength);
    AntiMaskOrSimd(pSource, pDestination, pLength);
    return true;
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  bool ApplyEncryptNeon(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopyNeon(pSource, aWorker, pLength);
    SplintEncrypt(aWorker, pDestination, pLength);
    AntiMaskOrNeon(pSource, pDestination, pLength);
    return true;
  }

  bool ApplyDecryptNeon(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    unsigned char* aWorker = const_cast<unsigned char*>(pWorker);
    MaskCopyNeon(pSource, aWorker, pLength);
    SplintDecrypt(aWorker, pDestination, pLength);
    AntiMaskOrNeon(pSource, pDestination, pLength);
    return true;
  }
#endif

  void MaskCopyScalar(const unsigned char* pSource,
                      unsigned char* pWorker,
                      std::size_t pLength) const {
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pWorker[aIndex] = static_cast<unsigned char>(pSource[aIndex] & mMask);
    }
  }

  void AntiMaskOrScalar(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          pDestination[aIndex] | (pSource[aIndex] & aAntimask));
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void MaskCopySimd(const unsigned char* pSource,
                    unsigned char* pWorker,
                    std::size_t pLength) const {
#if defined(__AVX2__)
    const __m256i aMask = _mm256_set1_epi8(static_cast<char>(mMask));
    std::size_t aIndex = 0;
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aIndex));
      const __m256i aMasked = _mm256_and_si256(aSource, aMask);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pWorker + aIndex),
                          aMasked);
    }
    MaskCopyScalar(pSource + aIndex, pWorker + aIndex, pLength - aIndex);
#else
    const __m128i aMask = _mm_set1_epi8(static_cast<char>(mMask));
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aSource = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aIndex));
      const __m128i aMasked = _mm_and_si128(aSource, aMask);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pWorker + aIndex), aMasked);
    }
    MaskCopyScalar(pSource + aIndex, pWorker + aIndex, pLength - aIndex);
#endif
  }

  void AntiMaskOrSimd(const unsigned char* pSource,
                      unsigned char* pDestination,
                      std::size_t pLength) const {
#if defined(__AVX2__)
    const __m256i aAntimask =
        _mm256_set1_epi8(static_cast<char>(static_cast<unsigned char>(~mMask)));
    std::size_t aIndex = 0;
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aIndex));
      const __m256i aDestination = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pDestination + aIndex));
      const __m256i aBase = _mm256_and_si256(aSource, aAntimask);
      const __m256i aResult = _mm256_or_si256(aDestination, aBase);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                          aResult);
    }
    AntiMaskOrScalar(pSource + aIndex, pDestination + aIndex, pLength - aIndex);
#else
    const __m128i aAntimask =
        _mm_set1_epi8(static_cast<char>(static_cast<unsigned char>(~mMask)));
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aSource = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pSource + aIndex));
      const __m128i aDestination = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(pDestination + aIndex));
      const __m128i aBase = _mm_and_si128(aSource, aAntimask);
      const __m128i aResult = _mm_or_si128(aDestination, aBase);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aResult);
    }
    AntiMaskOrScalar(pSource + aIndex, pDestination + aIndex, pLength - aIndex);
#endif
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void MaskCopyNeon(const unsigned char* pSource,
                    unsigned char* pWorker,
                    std::size_t pLength) const {
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
      const uint8x16_t aMasked = vandq_u8(aSource, aMask);
      vst1q_u8(pWorker + aIndex, aMasked);
    }
    MaskCopyScalar(pSource + aIndex, pWorker + aIndex, pLength - aIndex);
  }

  void AntiMaskOrNeon(const unsigned char* pSource,
                      unsigned char* pDestination,
                      std::size_t pLength) const {
    const uint8x16_t aAntimask = vdupq_n_u8(static_cast<unsigned char>(~mMask));
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
      const uint8x16_t aDestination = vld1q_u8(pDestination + aIndex);
      const uint8x16_t aBase = vandq_u8(aSource, aAntimask);
      const uint8x16_t aResult = vorrq_u8(aDestination, aBase);
      vst1q_u8(pDestination + aIndex, aResult);
    }
    AntiMaskOrScalar(pSource + aIndex, pDestination + aIndex, pLength - aIndex);
  }
#endif

  static void SplintEncrypt(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) {
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + (pLength >> 1);
    const unsigned char* aFrontShelf = aBack;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf) {
      *aOut = *aFront;
      aOut += 2;
      ++aFront;
    }

    aOut = pDestination + 1;
    while (aBack < aBackShelf) {
      *aOut = *aBack;
      aOut += 2;
      ++aBack;
    }
  }

  static void SplintDecrypt(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) {
    const unsigned char* aEven = pSource;
    const unsigned char* aEvenShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aEven < aEvenShelf) {
      *aOut++ = *aEven;
      aEven += 2;
    }

    const unsigned char* aOdd = pSource + 1;
    const unsigned char* aOddShelf = pSource + pLength;
    while (aOdd < aOddShelf) {
      *aOut++ = *aOdd;
      aOdd += 2;
    }
  }

  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_SPLINT_MASK_CIPHER_HPP_
