#include "LayeredCrypt.hpp"

#include <vector>

#include "../PeanutButter.hpp"

namespace peanutbutter {

bool LayeredCrypt::ValidateTopLevel(const unsigned char* pSource,
                                    unsigned char* pWorker,
                                    unsigned char* pDestination,
                                    std::size_t pLength) {
  if (pLength == 0) {
    return true;
  }
  if (pLength != BLOCK_SIZE_L3) {
    return false;
  }
  if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
    return false;
  }
  if (pSource == pDestination || pSource == pWorker || pDestination == pWorker) {
    return false;
  }
  return true;
}

bool LayeredCrypt::SealData(const unsigned char* pSource,
                            unsigned char* pWorker,
                            unsigned char* pDestination,
                            std::size_t pLength,
                            std::string* pErrorMessage,
                            CryptMode pMode) const {
  if (!ValidateTopLevel(pSource, pWorker, pDestination, pLength)) {
    if (pErrorMessage != nullptr && pLength != 0) {
      *pErrorMessage =
          "LayeredCrypt::SealData requires distinct non-null buffers and "
          "BLOCK_SIZE_L3 length";
    }
    return pLength == 0;
  }
  if (pLength == 0) {
    return true;
  }

  std::vector<unsigned char> aStageWorker(pLength);

  for (std::size_t aIndex = 0; aIndex < 4; ++aIndex) {
    const std::size_t aOffset = BLOCK_SIZE_L1 * aIndex;
    if (!mLayer1.SealData(pSource + aOffset, pWorker + aOffset,
                          pDestination + aOffset, BLOCK_SIZE_L1, pErrorMessage,
                          pMode)) {
      return false;
    }
  }

  for (std::size_t aIndex = 0; aIndex < 2; ++aIndex) {
    const std::size_t aOffset = BLOCK_SIZE_L2 * aIndex;
    if (!mLayer2.SealData(pDestination + aOffset, aStageWorker.data() + aOffset,
                          pWorker + aOffset, BLOCK_SIZE_L2, pErrorMessage,
                          pMode)) {
      return false;
    }
  }

  if (!mLayer3.SealData(pWorker, aStageWorker.data(), pDestination, BLOCK_SIZE_L3,
                        pErrorMessage, pMode)) {
    return false;
  }

  return true;
}

bool LayeredCrypt::UnsealData(const unsigned char* pSource,
                              unsigned char* pWorker,
                              unsigned char* pDestination,
                              std::size_t pLength,
                              std::string* pErrorMessage,
                              CryptMode pMode) const {
  if (!ValidateTopLevel(pSource, pWorker, pDestination, pLength)) {
    if (pErrorMessage != nullptr && pLength != 0) {
      *pErrorMessage =
          "LayeredCrypt::UnsealData requires distinct non-null buffers and "
          "BLOCK_SIZE_L3 length";
    }
    return pLength == 0;
  }
  if (pLength == 0) {
    return true;
  }

  std::vector<unsigned char> aStageWorker(pLength);

  if (!mLayer3.UnsealData(pSource, pWorker, pDestination, BLOCK_SIZE_L3,
                          pErrorMessage, pMode)) {
    return false;
  }

  for (std::size_t aIndex = 0; aIndex < 2; ++aIndex) {
    const std::size_t aOffset = BLOCK_SIZE_L2 * aIndex;
    if (!mLayer2.UnsealData(pDestination + aOffset, aStageWorker.data() + aOffset,
                            pWorker + aOffset, BLOCK_SIZE_L2, pErrorMessage,
                            pMode)) {
      return false;
    }
  }

  for (std::size_t aIndex = 0; aIndex < 4; ++aIndex) {
    const std::size_t aOffset = BLOCK_SIZE_L1 * aIndex;
    if (!mLayer1.UnsealData(pWorker + aOffset, aStageWorker.data() + aOffset,
                            pDestination + aOffset, BLOCK_SIZE_L1, pErrorMessage,
                            pMode)) {
      return false;
    }
  }

  return true;
}

}  // namespace peanutbutter
