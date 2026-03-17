#ifndef JELLY_CORE_LAYERED_CRYPT_HPP_
#define JELLY_CORE_LAYERED_CRYPT_HPP_

#include <cstddef>

#include "EncryptionLayer.hpp"

namespace peanutbutter {

class LayeredCrypt final : public Crypt {
 public:
  LayeredCrypt() = default;

  EncryptionLayer& Layer1() { return mLayer1; }
  EncryptionLayer& Layer2() { return mLayer2; }
  EncryptionLayer& Layer3() { return mLayer3; }

  const EncryptionLayer& Layer1() const { return mLayer1; }
  const EncryptionLayer& Layer2() const { return mLayer2; }
  const EncryptionLayer& Layer3() const { return mLayer3; }

  bool SealData(const unsigned char* pSource,
                unsigned char* pWorker,
                unsigned char* pDestination,
                std::size_t pLength,
                std::string* pErrorMessage,
                CryptMode pMode) const override;

  bool UnsealData(const unsigned char* pSource,
                  unsigned char* pWorker,
                  unsigned char* pDestination,
                  std::size_t pLength,
                  std::string* pErrorMessage,
                  CryptMode pMode) const override;

 private:
  static bool ValidateTopLevel(const unsigned char* pSource,
                               unsigned char* pWorker,
                               unsigned char* pDestination,
                               std::size_t pLength);

  EncryptionLayer mLayer1;
  EncryptionLayer mLayer2;
  EncryptionLayer mLayer3;
};

namespace layered {
using Crypt = ::peanutbutter::LayeredCrypt;
}

}  // namespace peanutbutter

#endif  // JELLY_CORE_LAYERED_CRYPT_HPP_
