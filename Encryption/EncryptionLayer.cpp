#include "EncryptionLayer.hpp"

#include <cstring>
#include <vector>

#include "../PeanutButter.hpp"

namespace peanutbutter {

namespace {

bool ValidateNonEmptyInputs(const unsigned char* pSource,
                            const unsigned char* pWorker,
                            const unsigned char* pDestination) {
  if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
    return false;
  }
  if (pSource == pDestination || pSource == pWorker || pDestination == pWorker) {
    return false;
  }
  return true;
}

}  // namespace

void EncryptionLayer::AddCipher(std::unique_ptr<Crypt> pCipher) {
  if (pCipher) {
    mCiphers.push_back(std::move(pCipher));
  }
}

void EncryptionLayer::ClearCiphers() { mCiphers.clear(); }

std::size_t EncryptionLayer::CipherCount() const { return mCiphers.size(); }

bool EncryptionLayer::SealData(const unsigned char* pSource,
                               unsigned char* pWorker,
                               unsigned char* pDestination,
                               std::size_t pLength,
                               std::string* pErrorMessage,
                               CryptMode pMode) const {
  return ApplyForward(pSource, pWorker, pDestination, pLength, pErrorMessage,
                      pMode);
}

bool EncryptionLayer::UnsealData(const unsigned char* pSource,
                                 unsigned char* pWorker,
                                 unsigned char* pDestination,
                                 std::size_t pLength,
                                 std::string* pErrorMessage,
                                 CryptMode pMode) const {
  return ApplyReverse(pSource, pWorker, pDestination, pLength, pErrorMessage,
                      pMode);
}

bool EncryptionLayer::ApplyForward(const unsigned char* pSource,
                                   unsigned char* pWorker,
                                   unsigned char* pDestination,
                                   std::size_t pLength,
                                   std::string* pErrorMessage,
                                   CryptMode pMode) const {
  if (pLength == 0) {
    return true;
  }
  if ((pLength % BLOCK_GRANULARITY) != 0) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage =
          "EncryptionLayer::SealData requires BLOCK_GRANULARITY alignment";
    }
    return false;
  }
  if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EncryptionLayer::SealData received a null buffer";
    }
    return false;
  }

  if (mCiphers.empty()) {
    if (pSource != pDestination) {
      std::memcpy(pDestination, pSource, pLength);
    }
    return true;
  }

  if (!ValidateNonEmptyInputs(pSource, pWorker, pDestination)) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage =
          "EncryptionLayer::SealData requires distinct source, worker, and "
          "destination buffers";
    }
    return false;
  }

  std::vector<unsigned char> aScratch(pLength);
  const unsigned char* aInput = pSource;
  unsigned char* aOutput =
      ((mCiphers.size() & 1u) == 0u) ? aScratch.data() : pDestination;

  for (const std::unique_ptr<Crypt>& aCipher : mCiphers) {
    if (!aCipher) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "EncryptionLayer::SealData encountered a null cipher";
      }
      return false;
    }
    if (!aCipher->SealData(aInput, pWorker, aOutput, pLength, pErrorMessage,
                           pMode)) {
      return false;
    }
    aInput = aOutput;
    aOutput = (aOutput == pDestination) ? aScratch.data() : pDestination;
  }

  return true;
}

bool EncryptionLayer::ApplyReverse(const unsigned char* pSource,
                                   unsigned char* pWorker,
                                   unsigned char* pDestination,
                                   std::size_t pLength,
                                   std::string* pErrorMessage,
                                   CryptMode pMode) const {
  if (pLength == 0) {
    return true;
  }
  if ((pLength % BLOCK_GRANULARITY) != 0) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage =
          "EncryptionLayer::UnsealData requires BLOCK_GRANULARITY alignment";
    }
    return false;
  }
  if (pSource == nullptr || pWorker == nullptr || pDestination == nullptr) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "EncryptionLayer::UnsealData received a null buffer";
    }
    return false;
  }

  if (mCiphers.empty()) {
    if (pSource != pDestination) {
      std::memcpy(pDestination, pSource, pLength);
    }
    return true;
  }

  if (!ValidateNonEmptyInputs(pSource, pWorker, pDestination)) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage =
          "EncryptionLayer::UnsealData requires distinct source, worker, and "
          "destination buffers";
    }
    return false;
  }

  std::vector<unsigned char> aScratch(pLength);
  const unsigned char* aInput = pSource;
  unsigned char* aOutput =
      ((mCiphers.size() & 1u) == 0u) ? aScratch.data() : pDestination;

  for (auto aIt = mCiphers.rbegin(); aIt != mCiphers.rend(); ++aIt) {
    const std::unique_ptr<Crypt>& aCipher = *aIt;
    if (!aCipher) {
      if (pErrorMessage != nullptr) {
        *pErrorMessage = "EncryptionLayer::UnsealData encountered a null cipher";
      }
      return false;
    }
    if (!aCipher->UnsealData(aInput, pWorker, aOutput, pLength, pErrorMessage,
                             pMode)) {
      return false;
    }
    aInput = aOutput;
    aOutput = (aOutput == pDestination) ? aScratch.data() : pDestination;
  }

  return true;
}

}  // namespace peanutbutter
