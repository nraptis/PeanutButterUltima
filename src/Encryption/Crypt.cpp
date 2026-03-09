#include "Encryption/Crypt.hpp"

namespace peanutbutter::ultima {

namespace {

bool Fail(const std::string& pMessage, std::string* pErrorMessage) {
  if (pErrorMessage != nullptr) {
    *pErrorMessage = pMessage;
  }
  return false;
}

bool HasUsableBuffers(const unsigned char* pSource,
                      unsigned char* pWorker,
                      unsigned char* pDestination,
                      std::size_t pLength) {
  if (pLength == 0) {
    return true;
  }
  return pSource != nullptr && pWorker != nullptr && pDestination != nullptr;
}

}  // namespace

bool PassthroughCrypt::SealData(const unsigned char* pSource,
                                unsigned char* pWorker,
                                unsigned char* pDestination,
                                std::size_t pLength,
                                std::string* pErrorMessage,
                                CryptMode) const {
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("encrypt failed: invalid source, worker, or destination buffer.", pErrorMessage);
  }
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    pDestination[aIndex] = pSource[aIndex];
  }
  return true;
}

bool PassthroughCrypt::UnsealData(const unsigned char* pSource,
                                  unsigned char* pWorker,
                                  unsigned char* pDestination,
                                  std::size_t pLength,
                                  std::string* pErrorMessage,
                                  CryptMode) const {
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("decrypt failed: invalid source, worker, or destination buffer.", pErrorMessage);
  }
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    pDestination[aIndex] = pSource[aIndex];
  }
  return true;
}

bool XorCrypt::SealData(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength,
                        std::string* pErrorMessage,
                        CryptMode) const {
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("encrypt failed: invalid source, worker, or destination buffer.", pErrorMessage);
  }
  return Fail("encrypt failed: XorCrypt requires key material, but the Crypt interface no longer accepts it.",
              pErrorMessage);
}

bool XorCrypt::UnsealData(const unsigned char* pSource,
                          unsigned char* pWorker,
                          unsigned char* pDestination,
                          std::size_t pLength,
                          std::string* pErrorMessage,
                          CryptMode pMode) const {
  if (!HasUsableBuffers(pSource, pWorker, pDestination, pLength)) {
    return Fail("decrypt failed: invalid source, worker, or destination buffer.", pErrorMessage);
  }
  return Fail("decrypt failed: XorCrypt requires key material, but the Crypt interface no longer accepts it.",
              pErrorMessage);
}

}  // namespace peanutbutter::ultima
