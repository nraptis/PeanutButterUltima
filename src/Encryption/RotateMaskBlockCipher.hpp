#ifndef PEANUT_BUTTER_ULTIMA_ENCRYPTION_ROTATE_MASK_BLOCK_CIPHER_HPP_
#define PEANUT_BUTTER_ULTIMA_ENCRYPTION_ROTATE_MASK_BLOCK_CIPHER_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "Encryption/Crypt.hpp"
#include "PeanutButter.hpp"

namespace peanutbutter {

class RotateMaskBlockCipher final : public Crypt {
 public:
  static constexpr std::size_t kBlockSize = kBlockSizeL3;

  RotateMaskBlockCipher(std::uint8_t pMask, int pShift) : mMask(pMask), mShift(pShift) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, NormalizeShift(mShift), pErrorMessage, "encrypt");
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode) const override {
    return Apply(pSource, pWorker, pDestination, pLength, NormalizeShift(-mShift), pErrorMessage, "decrypt");
  }

 private:
  static std::size_t NormalizeShift(int pShift) {
    int aRotation = pShift % static_cast<int>(kBlockSize);
    if (aRotation < 0) {
      aRotation += static_cast<int>(kBlockSize);
    }
    return static_cast<std::size_t>(aRotation);
  }

  bool Apply(const unsigned char* pSource,
             unsigned char* pWorker,
             unsigned char* pDestination,
             std::size_t pLength,
             std::size_t pRotation,
             std::string* pErrorMessage,
             const char* pOperation) const {
    (void)pWorker;
    if (pLength == 0) {
      return true;
    }
    if (pSource == nullptr || pDestination == nullptr) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = std::string(pOperation) + " failed: invalid source or destination buffer.";
      }
      return false;
    }
    if ((pLength % kBlockSize) != 0) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = std::string(pOperation) + " failed: input length must be divisible by block size " +
                         std::to_string(kBlockSize) + ".";
      }
      return false;
    }

    const bool aAliasedIo = (pSource == pDestination);
    std::unique_ptr<L3BlockBuffer> aAliasedInput;
    if (aAliasedIo) {
      aAliasedInput = std::unique_ptr<L3BlockBuffer>(new (std::nothrow) L3BlockBuffer());
      if (!aAliasedInput) {
        if (pErrorMessage != nullptr) {
          *pErrorMessage = std::string(pOperation) + " failed: insufficient memory for alias-safe block temp.";
        }
        return false;
      }
    }

    const unsigned char aAntimask = static_cast<unsigned char>(~mMask);
    for (std::size_t aBlockBase = 0; aBlockBase < pLength; aBlockBase += kBlockSize) {
      const unsigned char* aInputBlock = pSource + aBlockBase;
      if (aAliasedIo) {
        std::memcpy(aAliasedInput->Data(), aInputBlock, kBlockSize);
        aInputBlock = aAliasedInput->Data();
      }
      unsigned char* aOutputBlock = pDestination + aBlockBase;
      for (std::size_t aIndex = 0; aIndex < kBlockSize; ++aIndex) {
        const std::size_t aSourceIndex =
            aIndex + pRotation < kBlockSize ? aIndex + pRotation : aIndex + pRotation - kBlockSize;
        const unsigned char aBaseByte = static_cast<unsigned char>(aInputBlock[aIndex] & aAntimask);
        const unsigned char aMasked = static_cast<unsigned char>(aInputBlock[aSourceIndex] & mMask);
        aOutputBlock[aIndex] = static_cast<unsigned char>(aBaseByte | aMasked);
      }
    }
    return true;
  }

  std::uint8_t mMask = 0;
  int mShift = 0;
};

using RotateMaskBlockCipher12 = RotateMaskBlockCipher;

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_ENCRYPTION_ROTATE_MASK_BLOCK_CIPHER_HPP_
