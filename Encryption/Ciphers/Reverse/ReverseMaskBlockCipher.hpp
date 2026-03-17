#ifndef JELLY_REVERSE_MASK_BLOCK_CIPHER_HPP_
#define JELLY_REVERSE_MASK_BLOCK_CIPHER_HPP_

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
class ReverseMaskBlockCipher final : public Crypt {
 public:
  explicit ReverseMaskBlockCipher(std::uint8_t pMask) : mMask(pMask) {}

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
      const std::size_t aBaseOffset = aBlock * tBlockSize;
      const std::size_t aMirrorOffset =
          (aBlockCount - 1 - aBlock) * tBlockSize;
      for (std::size_t aByte = 0; aByte < tBlockSize; ++aByte) {
        const unsigned char aBase =
            static_cast<unsigned char>(pSource[aBaseOffset + aByte] & aAntimask);
        const unsigned char aMasked =
            static_cast<unsigned char>(pSource[aMirrorOffset + aByte] & mMask);
        pDestination[aBaseOffset + aByte] =
            static_cast<unsigned char>(aBase | aMasked);
      }
    }
  }

#if defined(__SSSE3__) || defined(__AVX2__)
  void ApplySimd(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    if constexpr ((tBlockSize % 16) != 0) {
      ApplySoftware(pSource, pDestination, pLength);
      return;
    }

    const std::size_t aBlockCount = pLength / tBlockSize;
#if defined(__AVX2__)
    const __m256i aMask256 = _mm256_set1_epi8(static_cast<char>(mMask));
    const __m256i aAntimask256 = _mm256_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));
#endif
    const __m128i aMask128 = _mm_set1_epi8(static_cast<char>(mMask));
    const __m128i aAntimask128 = _mm_set1_epi8(
        static_cast<char>(static_cast<unsigned char>(~mMask)));

    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aBaseOffset = aBlock * tBlockSize;
      const std::size_t aMirrorOffset =
          (aBlockCount - 1 - aBlock) * tBlockSize;
      std::size_t aByte = 0;
#if defined(__AVX2__)
      for (; aByte + 32 <= tBlockSize; aByte += 32) {
        const __m256i aBase = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aBaseOffset + aByte));
        const __m256i aMasked = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(pSource + aMirrorOffset + aByte));
        const __m256i aResult =
            _mm256_or_si256(_mm256_and_si256(aBase, aAntimask256),
                            _mm256_and_si256(aMasked, aMask256));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination +
                                                       aBaseOffset + aByte),
                           aResult);
      }
#endif
      for (; aByte + 16 <= tBlockSize; aByte += 16) {
        const __m128i aBase = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aBaseOffset + aByte));
        const __m128i aMasked = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(pSource + aMirrorOffset + aByte));
        const __m128i aResult =
            _mm_or_si128(_mm_and_si128(aBase, aAntimask128),
                         _mm_and_si128(aMasked, aMask128));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aBaseOffset +
                                                    aByte),
                         aResult);
      }
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  void ApplyNeon(const unsigned char* pSource,
                 unsigned char* pDestination,
                 std::size_t pLength) const {
    if constexpr ((tBlockSize % 16) != 0) {
      ApplySoftware(pSource, pDestination, pLength);
      return;
    }

    const std::size_t aBlockCount = pLength / tBlockSize;
    const uint8x16_t aMask = vdupq_n_u8(mMask);
    const uint8x16_t aAntimask = vmvnq_u8(aMask);

    for (std::size_t aBlock = 0; aBlock < aBlockCount; ++aBlock) {
      const std::size_t aBaseOffset = aBlock * tBlockSize;
      const std::size_t aMirrorOffset =
          (aBlockCount - 1 - aBlock) * tBlockSize;
      for (std::size_t aByte = 0; aByte < tBlockSize; aByte += 16) {
        const uint8x16_t aBase = vld1q_u8(pSource + aBaseOffset + aByte);
        const uint8x16_t aMasked = vld1q_u8(pSource + aMirrorOffset + aByte);
        const uint8x16_t aResult =
            vorrq_u8(vandq_u8(aBase, aAntimask), vandq_u8(aMasked, aMask));
        vst1q_u8(pDestination + aBaseOffset + aByte, aResult);
      }
    }
  }
#endif

  std::uint8_t mMask;
};

using ReverseMaskBlockCipher08 = ReverseMaskBlockCipher<8>;
using ReverseMaskBlockCipher12 = ReverseMaskBlockCipher<12>;
using ReverseMaskBlockCipher16 = ReverseMaskBlockCipher<16>;
using ReverseMaskBlockCipher24 = ReverseMaskBlockCipher<24>;
using ReverseMaskBlockCipher32 = ReverseMaskBlockCipher<32>;
using ReverseMaskBlockCipher48 = ReverseMaskBlockCipher<48>;

}  // namespace peanutbutter

#endif  // JELLY_REVERSE_MASK_BLOCK_CIPHER_HPP_
