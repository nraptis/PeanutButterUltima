#ifndef JELLY_WEAVE_MASK_BLOCK_CIPHER_HPP_
#define JELLY_WEAVE_MASK_BLOCK_CIPHER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__SSSE3__) || defined(__AVX2__)
#include <immintrin.h>
#endif

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class WeaveMaskBlockCipher final : public Crypt {
 public:
  WeaveMaskBlockCipher(std::size_t pBlockSize,
                       std::uint8_t pMask,
                       int pCount,
                       int pFrontStride,
                       int pBackStride)
      : mBlockSize(pBlockSize),
        mMask(pMask),
        mCount(pCount),
        mFrontStride(pFrontStride),
        mBackStride(pBackStride) {}

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
    if (mBlockSize == 0) {
      return false;
    }
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

    const std::size_t aFullBlockCount = pLength / mBlockSize;
    const std::size_t aTailOffset = aFullBlockCount * mBlockSize;
    const std::vector<std::size_t>& aMap = GetMap(aFullBlockCount);

    for (std::size_t aBlockIndex = 0; aBlockIndex < aFullBlockCount;
         ++aBlockIndex) {
      BlendMasked(pSource + (aBlockIndex * mBlockSize),
                  pSource + (aMap[aBlockIndex] * mBlockSize),
                  pDestination + (aBlockIndex * mBlockSize), mBlockSize, pMode);
    }
    if (aTailOffset < pLength) {
      std::memcpy(pDestination + aTailOffset, pSource + aTailOffset,
                  pLength - aTailOffset);
    }
    return true;
  }

  const std::vector<std::size_t>& GetMap(std::size_t pBlockCount) const {
    if (mCachedMapBlockCount != pBlockCount) {
      mCachedMap = BuildMap(pBlockCount, mCount, mFrontStride, mBackStride);
      mCachedMapBlockCount = pBlockCount;
    }
    return mCachedMap;
  }

  void BlendMasked(const unsigned char* pBaseSource,
                   const unsigned char* pMaskedSource,
                   unsigned char* pDestination,
                   std::size_t pLength,
                   CryptMode pMode) const {
    switch (pMode) {
      case CryptMode::kNormal:
        BlendMaskedScalar(pBaseSource, pMaskedSource, pDestination, pLength);
        return;
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        BlendMaskedSimd(pBaseSource, pMaskedSource, pDestination, pLength);
        return;
#else
        BlendMaskedScalar(pBaseSource, pMaskedSource, pDestination, pLength);
        return;
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        BlendMaskedNeon(pBaseSource, pMaskedSource, pDestination, pLength);
        return;
#else
        BlendMaskedScalar(pBaseSource, pMaskedSource, pDestination, pLength);
        return;
#endif
    }
  }

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

  static std::size_t ClampPositiveCount(int pValue) {
    return static_cast<std::size_t>(pValue < 1 ? 1 : pValue);
  }

  static std::size_t ClampNonNegative(int pValue) {
    return static_cast<std::size_t>(pValue < 0 ? 0 : pValue);
  }

  static std::vector<std::size_t> BuildMap(std::size_t pLength,
                                           int pCount,
                                           int pFrontStride,
                                           int pBackStride) {
    std::vector<std::size_t> aMap(pLength);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      aMap[aIndex] = aIndex;
    }
    if (pLength < 2) {
      return aMap;
    }

    const std::size_t aCount = ClampPositiveCount(pCount);
    const std::size_t aFrontStride = ClampNonNegative(pFrontStride);
    const std::size_t aBackStride = ClampNonNegative(pBackStride);
    std::size_t aFront = 0;
    std::size_t aBack = pLength - 1;

    while (aFront < aBack) {
      std::size_t aSwaps = aCount;
      while (aSwaps > 0 && aFront < aBack) {
        std::swap(aMap[aFront], aMap[aBack]);
        --aSwaps;
        ++aFront;
        --aBack;
      }
      if (aFront >= aBack) {
        break;
      }
      std::size_t aSkips = aFrontStride;
      while (aSkips > 0 && aFront < aBack) {
        --aSkips;
        ++aFront;
      }
      if (aFront >= aBack) {
        break;
      }
      aSkips = aBackStride;
      while (aSkips > 0 && aFront < aBack) {
        --aSkips;
        --aBack;
      }
    }

    return aMap;
  }

  std::size_t mBlockSize;
  std::uint8_t mMask;
  int mCount;
  int mFrontStride;
  int mBackStride;
  mutable std::size_t mCachedMapBlockCount = static_cast<std::size_t>(-1);
  mutable std::vector<std::size_t> mCachedMap;
};

}  // namespace peanutbutter

#endif  // JELLY_WEAVE_MASK_BLOCK_CIPHER_HPP_
