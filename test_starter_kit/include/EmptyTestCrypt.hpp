#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_EMPTY_TEST_CRYPT_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_EMPTY_TEST_CRYPT_HPP_

#include <cstddef>

#include "Encryption/Crypt.hpp"

namespace peanutbutter {
namespace testkit {

class EmptyTestCrypt final : public Crypt {
 public:
  void SetFailSeal(bool pFail);
  void SetFailUnseal(bool pFail);

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
  bool mFailSeal = false;
  bool mFailUnseal = false;
};

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_EMPTY_TEST_CRYPT_HPP_
