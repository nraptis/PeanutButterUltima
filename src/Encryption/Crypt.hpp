#ifndef PEANUT_BUTTER_ULTIMA_ENCRYPTION_CRYPT_HPP_
#define PEANUT_BUTTER_ULTIMA_ENCRYPTION_CRYPT_HPP_

#include <cstddef>
#include <string>

namespace peanutbutter {

enum class CryptMode {
  kNormal = 0,
  kSimd = 1,
  kNeon = 2,
};

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

class PassthroughCrypt final : public Crypt {
 public:
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
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_ENCRYPTION_CRYPT_HPP_
