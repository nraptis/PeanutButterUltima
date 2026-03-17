#ifndef PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_
#define PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_

#include <cstddef>
#include <functional>
#include <memory>
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

enum class CryptGenerationStage : std::uint8_t {
  kExpansion = 0u,
  kLayerCake = 1u,
};

struct CryptGeneratorRequest {
  EncryptionStrength mEncryptionStrength = EncryptionStrength::kHigh;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
  bool mRecoverMode = false;
  std::function<void(const std::string& pMessage)> mLogStatus;
  std::function<void(CryptGenerationStage pStage, double pStageFraction)> mReportProgress;
};

using CryptGenerator =
    std::function<std::unique_ptr<Crypt>(const CryptGeneratorRequest& pRequest,
                                         std::string* pErrorMessage)>;

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

#endif  // PEANUTBUTTER_ENCRYPTION_CRYPT_HPP_
