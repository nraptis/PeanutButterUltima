#ifndef JELLY_WEAVE_CIPHER_HPP_
#define JELLY_WEAVE_CIPHER_HPP_

#include <algorithm>
#include <cstddef>
#include <vector>

#include "../../Crypt.hpp"
#include "../../../PeanutButter.hpp"

namespace peanutbutter {

class WeaveCipher final : public Crypt {
 public:
  WeaveCipher(int pCount, int pFrontStride, int pBackStride)
      : mCount(pCount),
        mFrontStride(pFrontStride),
        mBackStride(pBackStride) {}

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength);
  }

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override {
    (void)pMode;
    return Apply(pSource, pDestination, pLength);
  }

 private:
  bool Apply(const unsigned char* pSource,
             unsigned char* pDestination,
             std::size_t pLength) const {
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

    const std::vector<std::size_t>& aMap = GetMap(pLength);
    for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
      pDestination[aIndex] = pSource[aMap[aIndex]];
    }
    return true;
  }

  const std::vector<std::size_t>& GetMap(std::size_t pLength) const {
    if (mCachedMapLength != pLength) {
      mCachedMap = BuildMap(pLength, mCount, mFrontStride, mBackStride);
      mCachedMapLength = pLength;
    }
    return mCachedMap;
  }

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

  int mCount;
  int mFrontStride;
  int mBackStride;
  mutable std::size_t mCachedMapLength = static_cast<std::size_t>(-1);
  mutable std::vector<std::size_t> mCachedMap;
};

}  // namespace peanutbutter

#endif  // JELLY_WEAVE_CIPHER_HPP_
