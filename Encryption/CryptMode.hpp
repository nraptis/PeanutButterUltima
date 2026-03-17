#ifndef PEANUTBUTTER_ENCRYPTION_CRYPT_MODE_HPP_
#define PEANUTBUTTER_ENCRYPTION_CRYPT_MODE_HPP_

#include <vector>

#include "../PeanutButter.hpp"

namespace peanutbutter {

enum class CryptMode {
  kNormal = 0,
  kSimd = 1,
  kNeon = 2,
};

inline const char* GetCryptModeName(CryptMode pMode) {
  switch (pMode) {
    case CryptMode::kNormal:
      return "Normal";
    case CryptMode::kSimd:
      return "Simd";
    case CryptMode::kNeon:
      return "Neon";
  }
  return "Unknown";
}

inline bool IsCryptModeAvailable(CryptMode pMode) {
  if (PEANUTBUTTER_SOFTWARE_ONLY_MODE) {
    return pMode == CryptMode::kNormal;
  }

  switch (pMode) {
    case CryptMode::kNormal:
      return true;
    case CryptMode::kSimd:
#if defined(__AVX2__) || defined(__SSSE3__)
      return true;
#else
      return false;
#endif
    case CryptMode::kNeon:
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__ARM_FEATURE_NEON) || \
    defined(__aarch64__) || defined(_M_ARM64)
      return true;
#else
      return false;
#endif
  }
  return false;
}

inline std::vector<CryptMode> GetAvailableCryptModes() {
  std::vector<CryptMode> aModes;
  aModes.push_back(CryptMode::kNormal);
  if (IsCryptModeAvailable(CryptMode::kSimd)) {
    aModes.push_back(CryptMode::kSimd);
  }
  if (IsCryptModeAvailable(CryptMode::kNeon)) {
    aModes.push_back(CryptMode::kNeon);
  }
  return aModes;
}

}  // namespace peanutbutter

#endif  // PEANUTBUTTER_ENCRYPTION_CRYPT_MODE_HPP_
