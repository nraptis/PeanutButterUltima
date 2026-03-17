#ifndef JELLY_ROTATE_MASK_BLOCK_CIPHER_HPP_
#define JELLY_ROTATE_MASK_BLOCK_CIPHER_HPP_

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

template <std::size_t tBlockSize>
class RotateMaskBlockCipher final : public Crypt {
 public:
  RotateMaskBlockCipher(std::uint8_t pMask, int pShift)
      : mMask(pMask), mShift(pShift) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, NormalizeShift(mShift), pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return Apply(pSource, pDestination, pLength, NormalizeShift(-mShift),
                 pMode);
  }

 private:
  static std::size_t NormalizeShift(int pShift) {
    int aRotation = pShift % static_cast<int>(tBlockSize);
    if (aRotation < 0) {
      aRotation += static_cast<int>(tBlockSize);
    }
    return static_cast<std::size_t>(aRotation);
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength,
             std::size_t pRotation,
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
    if ((pLength % tBlockSize) != 0) {
      return false;
    }
    switch (pMode) {
      case CryptMode::kNormal:
        return ApplyScalar(pSource, pDestination, pLength, pRotation);
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        return ApplySimd(pSource, pDestination, pLength, pRotation);
#else
        return false;
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        return ApplyNeon(pSource, pDestination, pLength, pRotation);
#else
        return false;
#endif
    }
    return false;
  }

  bool ApplyScalar(const unsigned char* pSource,
                   unsigned char* pDestination,
                   std::size_t pLength,
                   std::size_t pRotation) const {
    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    const std::size_t aBlockCount = pLength / tBlockSize;

    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aBase = aBlock * tBlockSize;
      for (std::size_t aIndex = 0; aIndex < tBlockSize; ++aIndex) {
        const std::size_t aSourceIndex = aIndex + pRotation < tBlockSize
                                             ? aIndex + pRotation
                                             : aIndex + pRotation - tBlockSize;
        const unsigned char aBaseByte =
            static_cast<unsigned char>(pSource[aBase + aIndex] & aAntimask);
        const unsigned char aMasked =
            static_cast<unsigned char>(pSource[aBase + aSourceIndex] & mMask);
        pDestination[aBase + aIndex] =
            static_cast<unsigned char>(aBaseByte | aMasked);
      }
    }

    return true;
  }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  bool ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength,
                 std::size_t pRotation) const {
    if constexpr (tBlockSize == 2 || tBlockSize == 4 || tBlockSize == 8 ||
                  tBlockSize == 16) {
      return ApplyTile16Neon(pSource, pDestination, pLength, pRotation);
    } else if constexpr (tBlockSize == 32) {
      return ApplyTile32Neon(pSource, pDestination, pLength, pRotation);
    } else if constexpr (tBlockSize == 12 || tBlockSize == 24 ||
                         tBlockSize == 48) {
      return ApplyTile48Neon(pSource, pDestination, pLength, pRotation);
    } else {
      return ApplyScalar(pSource, pDestination, pLength, pRotation);
    }
  }

  template <std::size_t tTileSize>
  void FillLaneMap(std::uint8_t (&pLaneMap)[tTileSize],
                   std::size_t pRotation) const {
    for (std::size_t aIndex = 0; aIndex < tTileSize; ++aIndex) {
      const std::size_t aBlockBase = (aIndex / tBlockSize) * tBlockSize;
      const std::size_t aIndexInBlock = aIndex - aBlockBase;
      pLaneMap[aIndex] = static_cast<std::uint8_t>(
          aBlockBase +
          (aIndexInBlock + pRotation < tBlockSize
               ? aIndexInBlock + pRotation
               : aIndexInBlock + pRotation - tBlockSize));
    }
  }

  bool ApplyTile16Neon(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength,
                       std::size_t pRotation) const {
    std::uint8_t aLaneMap[16];
    FillLaneMap(aLaneMap, pRotation);
    const uint8x16_t aLanes = vld1q_u8(aLaneMap);
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aOffset);
      const uint8x16_t aRotated = vqtbl1q_u8(aSource, aLanes);
      const uint8x16_t aBase = vandq_u8(aSource, aAntimask);
      const uint8x16_t aResult = vorrq_u8(aBase, vandq_u8(aRotated, aMask));
      vst1q_u8(pDestination + aOffset, aResult);
    }

    if (aOffset < pLength) {
      return ApplyScalar(pSource + aOffset, pDestination + aOffset,
                         pLength - aOffset, pRotation);
    }

    return true;
  }

  bool ApplyTile32Neon(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength,
                       std::size_t pRotation) const {
    std::uint8_t aLaneMap[32];
    FillLaneMap(aLaneMap, pRotation);
    const uint8x16_t aLane0 = vld1q_u8(aLaneMap);
    const uint8x16_t aLane1 = vld1q_u8(aLaneMap + 16);
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    std::size_t aOffset = 0;
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const uint8x16_t aSource0 = vld1q_u8(pSource + aOffset);
      const uint8x16_t aSource1 = vld1q_u8(pSource + aOffset + 16);
      const uint8x16x2_t aTable = {aSource0, aSource1};
      const uint8x16_t aRotated0 = vqtbl2q_u8(aTable, aLane0);
      const uint8x16_t aRotated1 = vqtbl2q_u8(aTable, aLane1);
      const uint8x16_t aBase0 = vandq_u8(aSource0, aAntimask);
      const uint8x16_t aBase1 = vandq_u8(aSource1, aAntimask);
      const uint8x16_t aResult0 = vorrq_u8(aBase0, vandq_u8(aRotated0, aMask));
      const uint8x16_t aResult1 = vorrq_u8(aBase1, vandq_u8(aRotated1, aMask));
      vst1q_u8(pDestination + aOffset, aResult0);
      vst1q_u8(pDestination + aOffset + 16, aResult1);
    }

    if (aOffset < pLength) {
      return ApplyScalar(pSource + aOffset, pDestination + aOffset,
                         pLength - aOffset, pRotation);
    }

    return true;
  }

  bool ApplyTile48Neon(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength,
                       std::size_t pRotation) const {
    std::uint8_t aLaneMap[48];
    FillLaneMap(aLaneMap, pRotation);
    const uint8x16_t aLane0 = vld1q_u8(aLaneMap);
    const uint8x16_t aLane1 = vld1q_u8(aLaneMap + 16);
    const uint8x16_t aLane2 = vld1q_u8(aLaneMap + 32);
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);
    const uint8x16_t aZero = vdupq_n_u8(0);

    std::size_t aOffset = 0;
    for (; aOffset + 48 <= pLength; aOffset += 48) {
      const uint8x16_t aSource0 = vld1q_u8(pSource + aOffset);
      const uint8x16_t aSource1 = vld1q_u8(pSource + aOffset + 16);
      const uint8x16_t aSource2 = vld1q_u8(pSource + aOffset + 32);
      const uint8x16x4_t aTable = {aSource0, aSource1, aSource2, aZero};
      const uint8x16_t aRotated0 = vqtbl4q_u8(aTable, aLane0);
      const uint8x16_t aRotated1 = vqtbl4q_u8(aTable, aLane1);
      const uint8x16_t aRotated2 = vqtbl4q_u8(aTable, aLane2);
      const uint8x16_t aBase0 = vandq_u8(aSource0, aAntimask);
      const uint8x16_t aBase1 = vandq_u8(aSource1, aAntimask);
      const uint8x16_t aBase2 = vandq_u8(aSource2, aAntimask);
      const uint8x16_t aResult0 = vorrq_u8(aBase0, vandq_u8(aRotated0, aMask));
      const uint8x16_t aResult1 = vorrq_u8(aBase1, vandq_u8(aRotated1, aMask));
      const uint8x16_t aResult2 = vorrq_u8(aBase2, vandq_u8(aRotated2, aMask));
      vst1q_u8(pDestination + aOffset, aResult0);
      vst1q_u8(pDestination + aOffset + 16, aResult1);
      vst1q_u8(pDestination + aOffset + 32, aResult2);
    }

    if (aOffset < pLength) {
      return ApplyScalar(pSource + aOffset, pDestination + aOffset,
                         pLength - aOffset, pRotation);
    }

    return true;
  }
#endif

#if defined(__AVX2__) || defined(__SSSE3__)
  bool ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength,
                 std::size_t pRotation) const {
    if constexpr (tBlockSize == 2 || tBlockSize == 4 || tBlockSize == 8 ||
                  tBlockSize == 16) {
      return ApplyTile16Simd(pSource, pDestination, pLength, pRotation);
    } else if constexpr (tBlockSize == 32) {
#if defined(__AVX2__)
      return ApplyTile32Avx2(pSource, pDestination, pLength, pRotation);
#else
      return ApplyScalar(pSource, pDestination, pLength, pRotation);
#endif
    } else {
      return ApplyScalar(pSource, pDestination, pLength, pRotation);
    }
  }

  template <std::size_t tTileSize>
  void FillLaneMapSimd(std::uint8_t (&pLaneMap)[tTileSize],
                       std::size_t pRotation) const {
    for (std::size_t aIndex = 0; aIndex < tTileSize; ++aIndex) {
      const std::size_t aBlockBase = (aIndex / tBlockSize) * tBlockSize;
      const std::size_t aIndexInBlock = aIndex - aBlockBase;
      pLaneMap[aIndex] = static_cast<std::uint8_t>(
          aBlockBase +
          (aIndexInBlock + pRotation < tBlockSize
               ? aIndexInBlock + pRotation
               : aIndexInBlock + pRotation - tBlockSize));
    }
  }

  bool ApplyTile16Simd(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength,
                       std::size_t pRotation) const {
    alignas(16) std::uint8_t aLaneMap[16];
    FillLaneMapSimd(aLaneMap, pRotation);
    const __m128i aLanes =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(aLaneMap));
    const __m128i aMask = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask =
        _mm_set1_epi8(static_cast<char>(static_cast<unsigned char>(~mMask)));

    std::size_t aOffset = 0;
    for (; aOffset + 16 <= pLength; aOffset += 16) {
      const __m128i aSource =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSource + aOffset));
      const __m128i aRotated = _mm_shuffle_epi8(aSource, aLanes);
      const __m128i aBase = _mm_and_si128(aSource, aAntimask);
      const __m128i aMasked = _mm_and_si128(aRotated, aMask);
      const __m128i aResult = _mm_or_si128(aBase, aMasked);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aOffset),
                       aResult);
    }

    if (aOffset < pLength) {
      return ApplyScalar(pSource + aOffset, pDestination + aOffset,
                         pLength - aOffset, pRotation);
    }

    return true;
  }

#if defined(__AVX2__)
  bool ApplyTile32Avx2(const unsigned char* pSource,
                       unsigned char* pDestination,
                       std::size_t pLength,
                       std::size_t pRotation) const {
    alignas(32) std::uint8_t aLaneMap[32];
    FillLaneMapSimd(aLaneMap, pRotation);
    const __m256i aLanes =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aLaneMap));
    const __m256i aMask =
        _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntimask =
        _mm256_set1_epi8(static_cast<char>(static_cast<unsigned char>(~mMask)));

    std::size_t aOffset = 0;
    for (; aOffset + 32 <= pLength; aOffset += 32) {
      const __m256i aSource =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pSource + aOffset));
      const __m256i aRotated = _mm256_shuffle_epi8(aSource, aLanes);
      const __m256i aBase = _mm256_and_si256(aSource, aAntimask);
      const __m256i aMasked = _mm256_and_si256(aRotated, aMask);
      const __m256i aResult = _mm256_or_si256(aBase, aMasked);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aOffset),
                         aResult);
    }

    if (aOffset < pLength) {
      return ApplyScalar(pSource + aOffset, pDestination + aOffset,
                         pLength - aOffset, pRotation);
    }

    return true;
  }
#endif
#endif

  std::uint8_t mMask;
  int mShift;
};

using RotateMaskBlockCipher08 = RotateMaskBlockCipher<8>;
using RotateMaskBlockCipher12 = RotateMaskBlockCipher<12>;
using RotateMaskBlockCipher16 = RotateMaskBlockCipher<16>;
using RotateMaskBlockCipher24 = RotateMaskBlockCipher<24>;
using RotateMaskBlockCipher32 = RotateMaskBlockCipher<32>;
using RotateMaskBlockCipher48 = RotateMaskBlockCipher<48>;

}  // namespace peanutbutter

#endif  // JELLY_ROTATE_MASK_BLOCK_CIPHER_HPP_
