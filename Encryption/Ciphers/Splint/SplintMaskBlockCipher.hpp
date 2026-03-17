#ifndef JELLY_SPLINT_MASK_BLOCK_CIPHER_HPP_
#define JELLY_SPLINT_MASK_BLOCK_CIPHER_HPP_

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

class SplintMaskBlockCipher final : public Crypt {
 public:
  SplintMaskBlockCipher(std::size_t pBlockSize, std::uint8_t pMask)
      : mBlockSize(pBlockSize), mMask(pMask) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    if (!ValidateInputs(pSource, pDestination, pLength)) {
      return false;
    }

    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyEncryptSoftware(pSource, pDestination, pLength);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplyEncryptSimd(pSource, pDestination, pLength);
#else
        return ApplyEncryptSoftware(pSource, pDestination, pLength);
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyEncryptNeon(pSource, pDestination, pLength);
#else
        return ApplyEncryptSoftware(pSource, pDestination, pLength);
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
    if (!ValidateInputs(pSource, pDestination, pLength)) {
      return false;
    }

    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyDecryptSoftware(pSource, pDestination, pLength);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplyDecryptSimd(pSource, pDestination, pLength);
#else
        return ApplyDecryptSoftware(pSource, pDestination, pLength);
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyDecryptNeon(pSource, pDestination, pLength);
#else
        return ApplyDecryptSoftware(pSource, pDestination, pLength);
#endif
    }
    return false;
  }

 private:
  bool ValidateInputs(const unsigned char* pSource,
                      unsigned char* pDestination,
                      std::size_t pLength) const {
    if (mBlockSize == 0) {
      return false;
    }
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if ((pLength % mBlockSize) != 0) {
      return false;
    }
    if (pSource == nullptr || pDestination == nullptr) {
      return false;
    }
    if (pSource == pDestination) {
      return false;
    }
    return true;
  }

  bool ApplyEncryptSoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + aHalf;
    const unsigned char* aFrontShelf = pSource + aHalf;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf && aBack < aBackShelf) {
      BlendMaskedScalar(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
      BlendMaskedScalar(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    while (aFront < aFrontShelf) {
      BlendMaskedScalar(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
    }

    while (aBack < aBackShelf) {
      BlendMaskedScalar(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    return true;
  }

  bool ApplyDecryptSoftware(const unsigned char* pSource,
                            unsigned char* pDestination,
                            std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aIn = pSource;
    const unsigned char* aInShelf = pSource + pLength;
    unsigned char* aFront = pDestination;
    unsigned char* aBack = pDestination + aHalf;
    unsigned char* aFrontShelf = pDestination + aHalf;
    unsigned char* aBackShelf = pDestination + pLength;

    while (aFront < aFrontShelf && aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedScalar(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
      BlendMaskedScalar(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    while (aFront < aFrontShelf && aIn < aInShelf) {
      BlendMaskedScalar(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
    }

    while (aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedScalar(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    return true;
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  bool ApplyEncryptSimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + aHalf;
    const unsigned char* aFrontShelf = pSource + aHalf;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf && aBack < aBackShelf) {
      BlendMaskedSimd(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
      BlendMaskedSimd(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    while (aFront < aFrontShelf) {
      BlendMaskedSimd(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
    }

    while (aBack < aBackShelf) {
      BlendMaskedSimd(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    return true;
  }

  bool ApplyDecryptSimd(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aIn = pSource;
    const unsigned char* aInShelf = pSource + pLength;
    unsigned char* aFront = pDestination;
    unsigned char* aBack = pDestination + aHalf;
    unsigned char* aFrontShelf = pDestination + aHalf;
    unsigned char* aBackShelf = pDestination + pLength;

    while (aFront < aFrontShelf && aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedSimd(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
      BlendMaskedSimd(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    while (aFront < aFrontShelf && aIn < aInShelf) {
      BlendMaskedSimd(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
    }

    while (aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedSimd(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    return true;
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  bool ApplyEncryptNeon(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aFront = pSource;
    const unsigned char* aBack = pSource + aHalf;
    const unsigned char* aFrontShelf = pSource + aHalf;
    const unsigned char* aBackShelf = pSource + pLength;
    unsigned char* aOut = pDestination;

    while (aFront < aFrontShelf && aBack < aBackShelf) {
      BlendMaskedNeon(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
      BlendMaskedNeon(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    while (aFront < aFrontShelf) {
      BlendMaskedNeon(aOut, aFront, aOut, mBlockSize);
      aOut += mBlockSize;
      aFront += mBlockSize;
    }

    while (aBack < aBackShelf) {
      BlendMaskedNeon(aOut, aBack, aOut, mBlockSize);
      aOut += mBlockSize;
      aBack += mBlockSize;
    }

    return true;
  }

  bool ApplyDecryptNeon(const unsigned char* pSource,
                        unsigned char* pDestination,
                        std::size_t pLength) const {
    std::memcpy(pDestination, pSource, pLength);
    const std::size_t aHalf = ((pLength >> 1) / mBlockSize) * mBlockSize;
    const unsigned char* aIn = pSource;
    const unsigned char* aInShelf = pSource + pLength;
    unsigned char* aFront = pDestination;
    unsigned char* aBack = pDestination + aHalf;
    unsigned char* aFrontShelf = pDestination + aHalf;
    unsigned char* aBackShelf = pDestination + pLength;

    while (aFront < aFrontShelf && aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedNeon(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
      BlendMaskedNeon(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    while (aFront < aFrontShelf && aIn < aInShelf) {
      BlendMaskedNeon(aFront, aIn, aFront, mBlockSize);
      aFront += mBlockSize;
      aIn += mBlockSize;
    }

    while (aBack < aBackShelf && aIn < aInShelf) {
      BlendMaskedNeon(aBack, aIn, aBack, mBlockSize);
      aBack += mBlockSize;
      aIn += mBlockSize;
    }

    return true;
  }
#endif

  void BlendMaskedScalar(const unsigned char* pBaseSource,
                         const unsigned char* pMaskedSource,
                         unsigned char* pDestination,
                         std::size_t pLength) const {
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = static_cast<unsigned char>(
          (pBaseSource[aIndex] & aAntimask) | (pMaskedSource[aIndex] & mMask));
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void BlendMaskedSimd(const unsigned char* pBaseSource,
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
    BlendMaskedScalar(pBaseSource + aIndex, pMaskedSource + aIndex,
                      pDestination + aIndex, pLength - aIndex);
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
    BlendMaskedScalar(pBaseSource + aIndex, pMaskedSource + aIndex,
                      pDestination + aIndex, pLength - aIndex);
#endif
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void BlendMaskedNeon(const unsigned char* pBaseSource,
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
    BlendMaskedScalar(pBaseSource + aIndex, pMaskedSource + aIndex,
                      pDestination + aIndex, pLength - aIndex);
  }
#endif

  std::size_t mBlockSize;
  std::uint8_t mMask;
};

}  // namespace peanutbutter

#endif  // JELLY_SPLINT_MASK_BLOCK_CIPHER_HPP_
