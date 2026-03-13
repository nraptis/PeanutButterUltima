#include "EmptyTestCrypt.hpp"

#include <cstring>

namespace peanutbutter {
namespace testkit {

void EmptyTestCrypt::SetFailSeal(bool pFail) {
  mFailSeal = pFail;
}

void EmptyTestCrypt::SetFailUnseal(bool pFail) {
  mFailUnseal = pFail;
}

bool EmptyTestCrypt::SealData(const unsigned char* pSource,
                              unsigned char*,
                              unsigned char* pDestination,
                              std::size_t pLength,
                              std::string* pErrorMessage,
                              CryptMode) const {
  if (mFailSeal) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EmptyTestCrypt configured to fail SealData.";
    }
    return false;
  }
  if (pLength == 0u) {
    return true;
  }
  if (pSource == nullptr || pDestination == nullptr) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EmptyTestCrypt received null source or destination.";
    }
    return false;
  }
  std::memcpy(pDestination, pSource, pLength);
  if (pErrorMessage != nullptr) {
    pErrorMessage->clear();
  }
  return true;
}

bool EmptyTestCrypt::UnsealData(const unsigned char* pSource,
                                unsigned char*,
                                unsigned char* pDestination,
                                std::size_t pLength,
                                std::string* pErrorMessage,
                                CryptMode) const {
  if (mFailUnseal) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EmptyTestCrypt configured to fail UnsealData.";
    }
    return false;
  }
  if (pLength == 0u) {
    return true;
  }
  if (pSource == nullptr || pDestination == nullptr) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EmptyTestCrypt received null source or destination.";
    }
    return false;
  }
  std::memcpy(pDestination, pSource, pLength);
  if (pErrorMessage != nullptr) {
    pErrorMessage->clear();
  }
  return true;
}

}  // namespace testkit
}  // namespace peanutbutter
