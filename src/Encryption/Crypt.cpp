#include "Crypt.hpp"

#include <cstring>

#include "../PeanutButter.hpp"

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
  if (pLength != 0 && pSource != pDestination) {
    std::memcpy(pDestination, pSource, pLength);
  }
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
  if (pLength != 0 && pSource != pDestination) {
    std::memcpy(pDestination, pSource, pLength);
  }
  return true;
}

}  // namespace peanutbutter
