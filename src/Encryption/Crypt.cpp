#include "Encryption/Crypt.hpp"

#include <algorithm>
#include <cstring>

#include "PeanutButter.hpp"

namespace peanutbutter {

namespace {

bool Fail(const std::string& pMessage, std::string* pErrorMessage) {
  if (pErrorMessage != nullptr) {
    *pErrorMessage = pMessage;
  }
  return false;
}

bool HasUsableBuffers(const unsigned char* pSource,
                      unsigned char*,
                      unsigned char* pDestination,
                      std::size_t pLength) {
  if (pLength == 0) {
    return true;
  }
  return pSource != nullptr && pDestination != nullptr;
}

void CopyWithFixedL3Blocks(const unsigned char* pSource,
                           unsigned char* pDestination,
                           std::size_t pLength) {
  std::size_t aOffset = 0;
  while (aOffset < pLength) {
    const std::size_t aChunkLength = std::min(kBlockSizeL3, pLength - aOffset);
    L3BlockBuffer aBlock{};
    std::memcpy(aBlock.Data(), pSource + aOffset, aChunkLength);
    std::memcpy(pDestination + aOffset, aBlock.Data(), aChunkLength);
    aOffset += aChunkLength;
  }
}

}  // namespace

bool PassthroughCrypt::SealData(const unsigned char* pSource,
                                unsigned char* pWorker,
                                unsigned char* pDestination,
                                std::size_t pLength,
                                std::string* pErrorMessage,
                                CryptMode) const {
  (void)pWorker;
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("encrypt failed: invalid source or destination buffer.", pErrorMessage);
  }
  CopyWithFixedL3Blocks(pSource, pDestination, pLength);
  return true;
}

bool PassthroughCrypt::UnsealData(const unsigned char* pSource,
                                  unsigned char* pWorker,
                                  unsigned char* pDestination,
                                  std::size_t pLength,
                                  std::string* pErrorMessage,
                                  CryptMode) const {
  (void)pWorker;
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("decrypt failed: invalid source or destination buffer.", pErrorMessage);
  }
  CopyWithFixedL3Blocks(pSource, pDestination, pLength);
  return true;
}

}  // namespace peanutbutter
