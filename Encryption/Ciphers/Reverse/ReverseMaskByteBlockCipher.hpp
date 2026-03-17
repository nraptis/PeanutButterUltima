#ifndef JELLY_REVERSE_MASK_BYTE_BLOCK_CIPHER_HPP_
#define JELLY_REVERSE_MASK_BYTE_BLOCK_CIPHER_HPP_

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
class ReverseMaskByteBlockCipher final : public Crypt {
 public:
  explicit ReverseMaskByteBlockCipher(std::uint8_t pMask) : mMask(pMask) {}

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
    if ((pLength % tBlockSize) != 0) {
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
    const std::size_t aBlockCount = pLength / tBlockSize;
    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aBase = aBlock * tBlockSize;
      for (std::size_t aByte = 0; aByte < tBlockSize; ++aByte) {
        const unsigned char aBaseByte =
            static_cast<unsigned char>(pSource[aBase + aByte] & aAntimask);
        const unsigned char aMaskedByte = static_cast<unsigned char>(
            pSource[aBase + (tBlockSize - 1 - aByte)] & mMask);
        pDestination[aBase + aByte] =
            static_cast<unsigned char>(aBaseByte | aMaskedByte);
      }
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    if constexpr (tBlockSize != 16 && tBlockSize != 32 && tBlockSize != 48) {
      ApplySoftware(pSource, pDestination, pLength);
      return;
    }

    const __m128i aLanes =
        _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask128 = _mm_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));

    for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
      if constexpr (tBlockSize == 16) {
        const __m128i aBase0 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase));
        const __m128i aMasked0 = _mm_shuffle_epi8(aBase0, aLanes);
        const __m128i aResult0 =
            _mm_or_si128(_mm_and_si128(aBase0, aAntimask128),
                         _mm_and_si128(aMasked0, aMask128));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase),
                         aResult0);
      } else if constexpr (tBlockSize == 32) {
        const __m128i aBase0 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase));
        const __m128i aBase1 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase + 16));
        const __m128i aMasked0 = _mm_shuffle_epi8(aBase1, aLanes);
        const __m128i aMasked1 = _mm_shuffle_epi8(aBase0, aLanes);
        const __m128i aResult0 =
            _mm_or_si128(_mm_and_si128(aBase0, aAntimask128),
                         _mm_and_si128(aMasked0, aMask128));
        const __m128i aResult1 =
            _mm_or_si128(_mm_and_si128(aBase1, aAntimask128),
                         _mm_and_si128(aMasked1, aMask128));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase),
                         aResult0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase + 16),
                         aResult1);
      } else if constexpr (tBlockSize == 48) {
        const __m128i aBase0 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase));
        const __m128i aBase1 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase + 16));
        const __m128i aBase2 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBase + 32));
        const __m128i aMasked0 = _mm_shuffle_epi8(aBase2, aLanes);
        const __m128i aMasked1 = _mm_shuffle_epi8(aBase1, aLanes);
        const __m128i aMasked2 = _mm_shuffle_epi8(aBase0, aLanes);
        const __m128i aResult0 =
            _mm_or_si128(_mm_and_si128(aBase0, aAntimask128),
                         _mm_and_si128(aMasked0, aMask128));
        const __m128i aResult1 =
            _mm_or_si128(_mm_and_si128(aBase1, aAntimask128),
                         _mm_and_si128(aMasked1, aMask128));
        const __m128i aResult2 =
            _mm_or_si128(_mm_and_si128(aBase2, aAntimask128),
                         _mm_and_si128(aMasked2, aMask128));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase),
                         aResult0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase + 16),
                         aResult1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBase + 32),
                         aResult2);
      }
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    if constexpr (tBlockSize != 16 && tBlockSize != 32 && tBlockSize != 48) {
      ApplySoftware(pSource, pDestination, pLength);
      return;
    }

    alignas(16) static const unsigned char kLaneMap[16] = {15, 14, 13, 12, 11,
                                                            10, 9,  8,  7,  6,
                                                            5,  4,  3,  2,  1,
                                                            0};
    const uint8x16_t aLanes = vld1q_u8(kLaneMap);
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    for (std::size_t aBase = 0; aBase < pLength; aBase += tBlockSize) {
      if constexpr (tBlockSize == 16) {
        const uint8x16_t aBase0 = vld1q_u8(pSource + aBase);
        const uint8x16_t aMasked0 = vqtbl1q_u8(aBase0, aLanes);
        const uint8x16_t aResult0 = vorrq_u8(vandq_u8(aBase0, aAntimask),
                                             vandq_u8(aMasked0, aMask));
        vst1q_u8(pDestination + aBase, aResult0);
      } else if constexpr (tBlockSize == 32) {
        const uint8x16_t aBase0 = vld1q_u8(pSource + aBase);
        const uint8x16_t aBase1 = vld1q_u8(pSource + aBase + 16);
        const uint8x16_t aMasked0 = vqtbl1q_u8(aBase1, aLanes);
        const uint8x16_t aMasked1 = vqtbl1q_u8(aBase0, aLanes);
        const uint8x16_t aResult0 = vorrq_u8(vandq_u8(aBase0, aAntimask),
                                             vandq_u8(aMasked0, aMask));
        const uint8x16_t aResult1 = vorrq_u8(vandq_u8(aBase1, aAntimask),
                                             vandq_u8(aMasked1, aMask));
        vst1q_u8(pDestination + aBase, aResult0);
        vst1q_u8(pDestination + aBase + 16, aResult1);
      } else if constexpr (tBlockSize == 48) {
        const uint8x16_t aBase0 = vld1q_u8(pSource + aBase);
        const uint8x16_t aBase1 = vld1q_u8(pSource + aBase + 16);
        const uint8x16_t aBase2 = vld1q_u8(pSource + aBase + 32);
        const uint8x16_t aMasked0 = vqtbl1q_u8(aBase2, aLanes);
        const uint8x16_t aMasked1 = vqtbl1q_u8(aBase1, aLanes);
        const uint8x16_t aMasked2 = vqtbl1q_u8(aBase0, aLanes);
        const uint8x16_t aResult0 = vorrq_u8(vandq_u8(aBase0, aAntimask),
                                             vandq_u8(aMasked0, aMask));
        const uint8x16_t aResult1 = vorrq_u8(vandq_u8(aBase1, aAntimask),
                                             vandq_u8(aMasked1, aMask));
        const uint8x16_t aResult2 = vorrq_u8(vandq_u8(aBase2, aAntimask),
                                             vandq_u8(aMasked2, aMask));
        vst1q_u8(pDestination + aBase, aResult0);
        vst1q_u8(pDestination + aBase + 16, aResult1);
        vst1q_u8(pDestination + aBase + 32, aResult2);
      }
    }
  }
#endif

  std::uint8_t mMask;
};

using ReverseMaskByteBlockCipher08 = ReverseMaskByteBlockCipher<8>;
using ReverseMaskByteBlockCipher12 = ReverseMaskByteBlockCipher<12>;
using ReverseMaskByteBlockCipher16 = ReverseMaskByteBlockCipher<16>;
using ReverseMaskByteBlockCipher24 = ReverseMaskByteBlockCipher<24>;
using ReverseMaskByteBlockCipher32 = ReverseMaskByteBlockCipher<32>;
using ReverseMaskByteBlockCipher48 = ReverseMaskByteBlockCipher<48>;

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_MASK_BYTE_BLOCK_CIPHER_HPP_
