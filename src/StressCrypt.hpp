#ifndef PEANUT_BUTTER_ULTIMA_STRESS_CRYPT_HPP_
#define PEANUT_BUTTER_ULTIMA_STRESS_CRYPT_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Encryption/Crypt.hpp"
#include "Encryption/LayeredCrypt.hpp"
#include "Encryption/Ciphers/Invert/InvertCipher.hpp"
#include "Encryption/Ciphers/Invert/InvertMaskCipher.hpp"
#include "Encryption/Ciphers/Password/PasswordCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseBlockByteCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseBlockCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseMaskBlockCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseMaskByteBlockCipher.hpp"
#include "Encryption/Ciphers/Reverse/ReverseMaskCipher.hpp"
#include "Encryption/Ciphers/Ripple/RippleBlockCipher.hpp"
#include "Encryption/Ciphers/Ripple/RippleCipher.hpp"
#include "Encryption/Ciphers/Ripple/RippleMaskBlockCipher.hpp"
#include "Encryption/Ciphers/Ripple/RippleMaskCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateBlockByteCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateBlockCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateMaskBlockCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateMaskByteBlockCipher.hpp"
#include "Encryption/Ciphers/Rotation/RotateMaskCipher.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridH16.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridH64.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridMaskH16.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridMaskH64.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridMaskV16.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridMaskV64.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridV16.hpp"
#include "Encryption/Ciphers/SpiralGrid/SpiralGridV64.hpp"
#include "Encryption/Ciphers/Splint/SplintBlockCipher.hpp"
#include "Encryption/Ciphers/Splint/SplintByteBlockCipher.hpp"
#include "Encryption/Ciphers/Splint/SplintCipher.hpp"
#include "Encryption/Ciphers/Splint/SplintMaskBlockCipher.hpp"
#include "Encryption/Ciphers/Splint/SplintMaskByteBlockCipher.hpp"
#include "Encryption/Ciphers/Splint/SplintMaskCipher.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridHH16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridHH64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridHV16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridHV64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskHH16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskHH64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskHV16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskHV64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskVH16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskVH64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskVV16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridMaskVV64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridVH16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridVH64.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridVV16.hpp"
#include "Encryption/Ciphers/SwapGrid/SwapGridVV64.hpp"
#include "Encryption/Ciphers/Weave/WeaveBlockCipher.hpp"
#include "Encryption/Ciphers/Weave/WeaveByteBlockCipher.hpp"
#include "Encryption/Ciphers/Weave/WeaveCipher.hpp"
#include "Encryption/Ciphers/Weave/WeaveMaskBlockCipher.hpp"
#include "Encryption/Ciphers/Weave/WeaveMaskByteBlockCipher.hpp"
#include "Encryption/Ciphers/Weave/WeaveMaskCipher.hpp"

namespace peanutbutter {

namespace {

inline constexpr std::array<std::size_t, 6> kStressBlockSizes = {
    8u, 12u, 16u, 24u, 32u, 48u};

class StressSeedCursor {
 public:
  explicit StressSeedCursor(std::size_t pSeed = 0u)
      : mMaskIndex(pSeed),
        mShiftIndex(pSeed * 3u + 1u),
        mRoundIndex(pSeed * 5u + 2u),
        mAmountIndex(pSeed * 7u + 3u),
        mCountIndex(pSeed * 11u + 4u),
        mStrideIndex(pSeed * 13u + 5u),
        mPasswordIndex(pSeed * 17u + 6u) {}

  std::uint8_t NextMask() {
    const std::uint8_t aValue = kMasks[mMaskIndex % kMasks.size()];
    ++mMaskIndex;
    return aValue;
  }

  int NextShift() {
    const int aValue = kShifts[mShiftIndex % kShifts.size()];
    ++mShiftIndex;
    return aValue;
  }

  int NextRoundCount() {
    const int aValue = kRounds[mRoundIndex % kRounds.size()];
    ++mRoundIndex;
    return aValue;
  }

  int NextAmount() {
    const int aValue = kAmounts[mAmountIndex % kAmounts.size()];
    ++mAmountIndex;
    return aValue;
  }

  int NextCount() {
    const int aValue = kCounts[mCountIndex % kCounts.size()];
    ++mCountIndex;
    return aValue;
  }

  int NextStride() {
    const int aValue = kStrides[mStrideIndex % kStrides.size()];
    ++mStrideIndex;
    return aValue;
  }

  std::string NextPassword() {
    const char* aBase = kPasswords[mPasswordIndex % kPasswords.size()];
    ++mPasswordIndex;
    return std::string(aBase);
  }

  std::vector<unsigned char> NextPasswordBytes() {
    const std::string aPassword = NextPassword();
    return std::vector<unsigned char>(aPassword.begin(), aPassword.end());
  }

 private:
  inline static constexpr std::array<std::uint8_t, 20> kMasks = {
      0x13u, 0x27u, 0x39u, 0x4Du, 0x5Fu, 0x6Bu, 0x71u, 0x82u, 0x94u, 0xA5u,
      0xB7u, 0xC9u, 0xDAu, 0xE3u, 0xF1u, 0x1Cu, 0x2Eu, 0x47u, 0x58u, 0x6Du};
  inline static constexpr std::array<int, 20> kShifts = {
      1, -3, 5, -7, 9, -11, 13, -15, 17, -19, 21, -23, 25, -27, 29, -31, 33,
      -35, 37, -39};
  inline static constexpr std::array<int, 12> kRounds = {
      1, 2, 3, 4, 5, 7, 9, 11, 13, 15, 17, 19};
  inline static constexpr std::array<int, 12> kAmounts = {
      1, -1, 2, -2, 3, -3, 5, -5, 7, -7, 9, -9};
  inline static constexpr std::array<int, 12> kCounts = {
      1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 15};
  inline static constexpr std::array<int, 12> kStrides = {
      0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 15};
  inline static const std::array<const char*, 10> kPasswords = {{
      "pb-stress-a13f59",
      "pb-stress-b27c6e",
      "pb-stress-c39d71",
      "pb-stress-d4af82",
      "pb-stress-e5b794",
      "pb-stress-f6c9a5",
      "pb-stress-07dad3",
      "pb-stress-18e3f1",
      "pb-stress-29f41c",
      "pb-stress-3a0547",
  }};

  std::size_t mMaskIndex = 0u;
  std::size_t mShiftIndex = 0u;
  std::size_t mRoundIndex = 0u;
  std::size_t mAmountIndex = 0u;
  std::size_t mCountIndex = 0u;
  std::size_t mStrideIndex = 0u;
  std::size_t mPasswordIndex = 0u;
};

template <typename TCipher, typename... TArgs>
void AddCipher(EncryptionLayer& pLayer, TArgs&&... pArgs) {
  pLayer.AddCipher(
      std::make_unique<TCipher>(std::forward<TArgs>(pArgs)...));
}

template <typename TFactory>
void AddSizedCipherSet(EncryptionLayer& pLayer, TFactory&& pFactory) {
  for (std::size_t aBlockSize : kStressBlockSizes) {
    pLayer.AddCipher(pFactory(aBlockSize));
  }
}

inline void AddReverseCipherSet(EncryptionLayer& pLayer,
                                StressSeedCursor& pSeed) {
  AddCipher<ReverseCipher>(pLayer);
  AddCipher<ReverseMaskCipher>(pLayer, pSeed.NextMask());
  AddCipher<ReverseBlockCipher08>(pLayer);
  AddCipher<ReverseBlockCipher12>(pLayer);
  AddCipher<ReverseBlockCipher16>(pLayer);
  AddCipher<ReverseBlockCipher24>(pLayer);
  AddCipher<ReverseBlockCipher32>(pLayer);
  AddCipher<ReverseBlockCipher48>(pLayer);
  AddCipher<ReverseBlockByteCipher08>(pLayer);
  AddCipher<ReverseBlockByteCipher12>(pLayer);
  AddCipher<ReverseBlockByteCipher16>(pLayer);
  AddCipher<ReverseBlockByteCipher24>(pLayer);
  AddCipher<ReverseBlockByteCipher32>(pLayer);
  AddCipher<ReverseBlockByteCipher48>(pLayer);
  AddCipher<ReverseMaskBlockCipher08>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskBlockCipher12>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskBlockCipher16>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskBlockCipher24>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskBlockCipher32>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskBlockCipher48>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher08>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher12>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher16>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher24>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher32>(pLayer, pSeed.NextMask());
  AddCipher<ReverseMaskByteBlockCipher48>(pLayer, pSeed.NextMask());
}

inline void AddRippleCipherSet(EncryptionLayer& pLayer,
                               StressSeedCursor& pSeed) {
  AddCipher<RippleCipher>(pLayer, pSeed.NextRoundCount());
  AddCipher<RippleMaskCipher>(pLayer, pSeed.NextMask(), pSeed.NextRoundCount());
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<RippleBlockCipher>(pBlockSize, pSeed.NextRoundCount());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<RippleMaskBlockCipher>(
        pBlockSize, pSeed.NextMask(), pSeed.NextRoundCount());
  });
}

inline void AddRotationCipherSet(EncryptionLayer& pLayer,
                                 StressSeedCursor& pSeed) {
  AddCipher<RotateCipher>(pLayer, pSeed.NextShift());
  AddCipher<RotateMaskCipher>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<RotateBlockCipher>(pBlockSize, pSeed.NextShift());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<RotateBlockByteCipher>(pBlockSize, pSeed.NextShift());
  });
  AddCipher<RotateMaskBlockCipher08>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskBlockCipher12>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskBlockCipher16>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskBlockCipher24>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskBlockCipher32>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskBlockCipher48>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<8>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<12>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<16>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<24>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<32>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
  AddCipher<RotateMaskByteBlockCipher<48>>(pLayer, pSeed.NextMask(), pSeed.NextShift());
}

inline void AddSpiralCipherSet(EncryptionLayer& pLayer,
                               StressSeedCursor& pSeed) {
  AddCipher<SpiralGridH16>(pLayer, pSeed.NextAmount());
  AddCipher<SpiralGridH64>(pLayer, pSeed.NextAmount());
  AddCipher<SpiralGridV16>(pLayer, pSeed.NextAmount());
  AddCipher<SpiralGridV64>(pLayer, pSeed.NextAmount());
  AddCipher<SpiralGridMaskH16>(pLayer, pSeed.NextMask(), pSeed.NextAmount());
  AddCipher<SpiralGridMaskH64>(pLayer, pSeed.NextMask(), pSeed.NextAmount());
  AddCipher<SpiralGridMaskV16>(pLayer, pSeed.NextMask(), pSeed.NextAmount());
  AddCipher<SpiralGridMaskV64>(pLayer, pSeed.NextMask(), pSeed.NextAmount());
}

inline void AddSplintCipherSet(EncryptionLayer& pLayer,
                               StressSeedCursor& pSeed) {
  AddCipher<SplintCipher>(pLayer);
  AddCipher<SplintMaskCipher>(pLayer, pSeed.NextMask());
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<SplintBlockCipher>(pBlockSize);
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<SplintByteBlockCipher>(pBlockSize);
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<SplintMaskBlockCipher>(pBlockSize, pSeed.NextMask());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<SplintMaskByteBlockCipher>(pBlockSize, pSeed.NextMask());
  });
}

inline void AddSwapGridCipherSet(EncryptionLayer& pLayer,
                                 StressSeedCursor& pSeed) {
  AddCipher<SwapGridHH16>(pLayer);
  AddCipher<SwapGridHH64>(pLayer);
  AddCipher<SwapGridHV16>(pLayer);
  AddCipher<SwapGridHV64>(pLayer);
  AddCipher<SwapGridVH16>(pLayer);
  AddCipher<SwapGridVH64>(pLayer);
  AddCipher<SwapGridVV16>(pLayer);
  AddCipher<SwapGridVV64>(pLayer);
  AddCipher<SwapGridMaskHH16>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskHH64>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskHV16>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskHV64>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskVH16>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskVH64>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskVV16>(pLayer, pSeed.NextMask());
  AddCipher<SwapGridMaskVV64>(pLayer, pSeed.NextMask());
}

inline void AddWeaveCipherSet(EncryptionLayer& pLayer,
                              StressSeedCursor& pSeed) {
  AddCipher<WeaveCipher>(pLayer, pSeed.NextCount(), pSeed.NextStride(), pSeed.NextStride());
  AddCipher<WeaveMaskCipher>(
      pLayer, pSeed.NextMask(), pSeed.NextCount(), pSeed.NextStride(), pSeed.NextStride());
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<WeaveBlockCipher>(
        pBlockSize, pSeed.NextCount(), pSeed.NextStride(), pSeed.NextStride());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<WeaveByteBlockCipher>(
        pBlockSize, pSeed.NextCount(), pSeed.NextStride(), pSeed.NextStride());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<WeaveMaskBlockCipher>(
        pBlockSize, pSeed.NextMask(), pSeed.NextCount(), pSeed.NextStride(),
        pSeed.NextStride());
  });
  AddSizedCipherSet(pLayer, [&](std::size_t pBlockSize) {
    return std::make_unique<WeaveMaskByteBlockCipher>(
        pBlockSize, pSeed.NextMask(), pSeed.NextCount(), pSeed.NextStride(),
        pSeed.NextStride());
  });
}

inline void PopulateStressLayer(EncryptionLayer& pLayer,
                                std::size_t pSeed) {
  StressSeedCursor aSeed(pSeed);

  AddCipher<InvertCipher>(pLayer);
  AddCipher<InvertMaskCipher>(pLayer, aSeed.NextMask());
  AddCipher<PasswordCipher>(pLayer, aSeed.NextPassword());
  AddCipher<PasswordCipher>(pLayer, aSeed.NextPasswordBytes());

  AddReverseCipherSet(pLayer, aSeed);
  AddRippleCipherSet(pLayer, aSeed);
  AddRotationCipherSet(pLayer, aSeed);
  AddSpiralCipherSet(pLayer, aSeed);
  AddSplintCipherSet(pLayer, aSeed);
  AddSwapGridCipherSet(pLayer, aSeed);
  AddWeaveCipherSet(pLayer, aSeed);
}

inline void PopulatePresetLayer1(EncryptionLayer& pLayer,
                                 std::size_t pSeed,
                                 std::size_t pTargetCipherCount) {
  StressSeedCursor aSeed(pSeed);
  std::size_t aAdded = 0u;

  AddCipher<InvertCipher>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<InvertMaskCipher>(pLayer, aSeed.NextMask());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<PasswordCipher>(pLayer, aSeed.NextPassword());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<ReverseMaskBlockCipher16>(pLayer, aSeed.NextMask());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RippleMaskCipher>(pLayer, aSeed.NextMask(), aSeed.NextRoundCount());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RotateMaskCipher>(pLayer, aSeed.NextMask(), aSeed.NextShift());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SpiralGridMaskH64>(pLayer, aSeed.NextMask(), aSeed.NextAmount());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SplintMaskBlockCipher>(pLayer, 24u, aSeed.NextMask());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SwapGridMaskHV64>(pLayer, aSeed.NextMask());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<WeaveMaskBlockCipher>(
      pLayer, 32u, aSeed.NextMask(), aSeed.NextCount(), aSeed.NextStride(), aSeed.NextStride());
}

inline void PopulatePresetLayer2(EncryptionLayer& pLayer,
                                 std::size_t pSeed,
                                 std::size_t pTargetCipherCount) {
  StressSeedCursor aSeed(pSeed);
  std::size_t aAdded = 0u;

  AddCipher<PasswordCipher>(pLayer, aSeed.NextPasswordBytes());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<ReverseBlockByteCipher24>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RippleBlockCipher>(pLayer, 48u, aSeed.NextRoundCount());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RotateBlockByteCipher>(pLayer, 32u, aSeed.NextShift());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SpiralGridV64>(pLayer, aSeed.NextAmount());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SplintByteBlockCipher>(pLayer, 16u);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SwapGridHH64>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<WeaveByteBlockCipher>(
      pLayer, 48u, aSeed.NextCount(), aSeed.NextStride(), aSeed.NextStride());
}

inline void PopulatePresetLayer3(EncryptionLayer& pLayer,
                                 std::size_t pSeed,
                                 std::size_t pTargetCipherCount) {
  StressSeedCursor aSeed(pSeed);
  std::size_t aAdded = 0u;

  AddCipher<ReverseCipher>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RippleCipher>(pLayer, aSeed.NextRoundCount());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<RotateCipher>(pLayer, aSeed.NextShift());
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SplintCipher>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<SwapGridVV64>(pLayer);
  if (++aAdded == pTargetCipherCount) {
    return;
  }
  AddCipher<WeaveCipher>(pLayer, aSeed.NextCount(), aSeed.NextStride(), aSeed.NextStride());
}

}  // namespace

class LayerCakePresetCrypt : public Crypt {
 public:
  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    return mCrypt.SealData(pSource, pWorker, pDestination, pLength, pErrorMessage, pMode);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    return mCrypt.UnsealData(pSource, pWorker, pDestination, pLength, pErrorMessage, pMode);
  }

 protected:
  LayeredCrypt mCrypt;
};

class StressCrypt final : public LayerCakePresetCrypt {
 public:
  StressCrypt() {
    PopulateStressLayer(mCrypt.Layer1(), 0u);
    PopulateStressLayer(mCrypt.Layer2(), 23u);
    PopulateStressLayer(mCrypt.Layer3(), 47u);
  }
};

class HighCrypt final : public LayerCakePresetCrypt {
 public:
  HighCrypt() {
    PopulatePresetLayer1(mCrypt.Layer1(), 0u, 10u);
    PopulatePresetLayer2(mCrypt.Layer2(), 23u, 8u);
    PopulatePresetLayer3(mCrypt.Layer3(), 47u, 6u);
  }
};

class MediumCrypt final : public LayerCakePresetCrypt {
 public:
  MediumCrypt() {
    PopulatePresetLayer1(mCrypt.Layer1(), 0u, 8u);
    PopulatePresetLayer2(mCrypt.Layer2(), 23u, 6u);
    PopulatePresetLayer3(mCrypt.Layer3(), 47u, 4u);
  }
};

class LowCrypt final : public LayerCakePresetCrypt {
 public:
  LowCrypt() {
    PopulatePresetLayer1(mCrypt.Layer1(), 0u, 6u);
    PopulatePresetLayer2(mCrypt.Layer2(), 23u, 4u);
    PopulatePresetLayer3(mCrypt.Layer3(), 47u, 2u);
  }
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_STRESS_CRYPT_HPP_
