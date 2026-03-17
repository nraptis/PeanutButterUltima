#ifndef JELLY_PASSWORD_CIPHER_HPP_
#define JELLY_PASSWORD_CIPHER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__AVX2__) || defined(__SSSE3__)
#include <immintrin.h>
#endif

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class PasswordCipher final : public Crypt {
 public:
  explicit PasswordCipher(const std::string& pPassword =
                              "catdogCATDOGdogcatFROGpigMOOSE")
      : mPasswordBytes(BuildPasswordBytes(
            reinterpret_cast<const unsigned char*>(pPassword.data()),
            pPassword.size())) {}

  explicit PasswordCipher(const std::vector<unsigned char>& pPasswordBytes)
      : mPasswordBytes(BuildPasswordBytes(pPasswordBytes.data(),
                                          pPasswordBytes.size())) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, pMode);
  }

 private:
  static std::vector<unsigned char> BuildPasswordBytes(
      const unsigned char* pPassword,
      std::size_t pLength) {
    if (pPassword == nullptr || pLength == 0) {
      return {};
    }
    // Match the Swift seed offset cheaply: start at half and wrap.
    const std::size_t aStart = pLength >> 1;
    std::vector<unsigned char> aResult(pLength);
    const std::size_t aHead = pLength - aStart;
    std::memcpy(aResult.data(), pPassword + aStart, aHead);
    if (aStart > 0) {
      std::memcpy(aResult.data() + aHead, pPassword, aStart);
    }
    return aResult;
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pWorker,
             unsigned char* pDestination,
             std::size_t pLength,
             CryptMode pMode) const {
    if (pLength == 0) {
      return true;
    }
    if ((pLength % BLOCK_GRANULARITY) != 0) {
      return false;
    }
    if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
      return false;
    }
    if (mPasswordBytes.empty()) {
      return false;
    }
    if (pSource == pDestination || pSource == pWorker || pDestination == pWorker) {
      return false;
    }

    ExpandPasswordToWorker(pWorker, pLength);

    switch (pMode) {
      case CryptMode::kNormal:
        ApplySoftware(pSource, pWorker, pDestination, pLength);
        return true;
      case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
        ApplySimd(pSource, pWorker, pDestination, pLength);
        return true;
#else
        return false;
#endif
      case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        ApplyNeon(pSource, pWorker, pDestination, pLength);
        return true;
#else
        return false;
#endif
    }
    return false;
  }

  void ExpandPasswordToWorker(unsigned char* pWorker,
                              std::size_t pLength) const {
    const std::size_t aPasswordLength = mPasswordBytes.size();
    const std::size_t aFirst = std::min(aPasswordLength, pLength);
    std::memcpy(pWorker, mPasswordBytes.data(), aFirst);
    std::size_t aFilled = aFirst;
    while (aFilled < pLength) {
      const std::size_t aCopy = std::min(aFilled, pLength - aFilled);
      std::memcpy(pWorker + aFilled, pWorker, aCopy);
      aFilled += aCopy;
    }
  }

  static void ApplySoftware(const unsigned char* pSource,
                            const unsigned char* pWorker,
                            unsigned char* pDestination,
                            std::size_t pLength) {
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] =
          static_cast<unsigned char>(pSource[aIndex] ^ pWorker[aIndex]);
    }
  }

#if defined(__AVX2__) || defined(__SSSE3__)
  static void ApplySimd(const unsigned char* pSource,
                        const unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    std::size_t aIndex = 0;
#if defined(__AVX2__)
    for (; aIndex + 32 <= pLength; aIndex += 32) {
      const __m256i aSource = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pSource + aIndex));
      const __m256i aPassword = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>(pWorker + aIndex));
      const __m256i aOutput = _mm256_xor_si256(aSource, aPassword);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(pDestination + aIndex),
                         aOutput);
    }
#endif
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const __m128i aSource =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(pSource + aIndex));
      const __m128i aPassword =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(pWorker + aIndex));
      const __m128i aOutput = _mm_xor_si128(aSource, aPassword);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(pDestination + aIndex),
                       aOutput);
    }
    for (; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] =
          static_cast<unsigned char>(pSource[aIndex] ^ pWorker[aIndex]);
    }
  }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  static void ApplyNeon(const unsigned char* pSource,
                        const unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength) {
    std::size_t aIndex = 0;
    for (; aIndex + 16 <= pLength; aIndex += 16) {
      const uint8x16_t aSource = vld1q_u8(pSource + aIndex);
      const uint8x16_t aPassword = vld1q_u8(pWorker + aIndex);
      const uint8x16_t aOutput = veorq_u8(aSource, aPassword);
      vst1q_u8(pDestination + aIndex, aOutput);
    }
    for (; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] =
          static_cast<unsigned char>(pSource[aIndex] ^ pWorker[aIndex]);
    }
  }
#endif

  std::vector<unsigned char> mPasswordBytes;
};

}  // namespace peanutbutter

#endif  // JELLY_PASSWORD_CIPHER_HPP_
