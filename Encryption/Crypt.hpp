#ifndef PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_
#define PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_

#include <cstddef>
#include <string>

#include "CryptMode.hpp"

namespace peanutbutter {

class Crypt {
 public:
  virtual ~Crypt() = default;
  virtual bool SealData(const unsigned char* pSource,
                        unsigned char* pWorker,
                        unsigned char* pDestination,
                        std::size_t pLength,
                        std::string* pErrorMessage,
                        CryptMode pMode) const = 0;
  virtual bool UnsealData(const unsigned char* pSource,
                          unsigned char* pWorker,
                          unsigned char* pDestination,
                          std::size_t pLength,
                          std::string* pErrorMessage,
                          CryptMode pMode) const = 0;
};

}  // namespace peanutbutter

#endif  // PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_
