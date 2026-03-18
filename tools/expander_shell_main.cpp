// Standalone console shell for the sister password-expansion library.
//
// This file is intentionally not wired into the main CMake build in this repo,
// because the actual Launch(...) implementation and shared table storage live in
// the sister library.
//
// Expected sister-library exports:
//
//   bool Launch(unsigned char* pPassword,
//               int pPasswordLength,
//               std::uint8_t pExpanderVersion,
//               peanutbutter::ExpansionStrength pExpansionStrength,
//               peanutbutter::Logger* pLogger,
//               const char* pModeName,
//               peanutbutter::ProgressProfileKind pProgressProfile,
//               bool (*pShouldCancel)(void*),
//               void* pCancelUserData);
//
//   extern unsigned char gTableL1_A[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_B[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_C[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_D[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_E[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_F[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_G[BLOCK_SIZE_L1];
//   extern unsigned char gTableL1_H[BLOCK_SIZE_L1];
//   extern unsigned char gTableL2_A[BLOCK_SIZE_L2];
//   extern unsigned char gTableL2_B[BLOCK_SIZE_L2];
//   extern unsigned char gTableL2_C[BLOCK_SIZE_L2];
//   extern unsigned char gTableL2_D[BLOCK_SIZE_L2];
//   extern unsigned char gTableL2_E[BLOCK_SIZE_L2];
//   extern unsigned char gTableL2_F[BLOCK_SIZE_L2];
//   extern unsigned char gTableL3_A[BLOCK_SIZE_L3];
//   extern unsigned char gTableL3_B[BLOCK_SIZE_L3];
//   extern unsigned char gTableL3_C[BLOCK_SIZE_L3];
//   extern unsigned char gTableL3_D[BLOCK_SIZE_L3];
//
// Example usage:
//
//   ./expander_shell_main "my password"
//   ./expander_shell_main "my password" Bundle high 1

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include "AppShell_Common.hpp"
#include "AppShell_Types.hpp"
#include "PeanutButter.hpp"

extern unsigned char gTableL1_A[BLOCK_SIZE_L1];
extern unsigned char gTableL1_B[BLOCK_SIZE_L1];
extern unsigned char gTableL1_C[BLOCK_SIZE_L1];
extern unsigned char gTableL1_D[BLOCK_SIZE_L1];
extern unsigned char gTableL1_E[BLOCK_SIZE_L1];
extern unsigned char gTableL1_F[BLOCK_SIZE_L1];
extern unsigned char gTableL1_G[BLOCK_SIZE_L1];
extern unsigned char gTableL1_H[BLOCK_SIZE_L1];

extern unsigned char gTableL2_A[BLOCK_SIZE_L2];
extern unsigned char gTableL2_B[BLOCK_SIZE_L2];
extern unsigned char gTableL2_C[BLOCK_SIZE_L2];
extern unsigned char gTableL2_D[BLOCK_SIZE_L2];
extern unsigned char gTableL2_E[BLOCK_SIZE_L2];
extern unsigned char gTableL2_F[BLOCK_SIZE_L2];

extern unsigned char gTableL3_A[BLOCK_SIZE_L3];
extern unsigned char gTableL3_B[BLOCK_SIZE_L3];
extern unsigned char gTableL3_C[BLOCK_SIZE_L3];
extern unsigned char gTableL3_D[BLOCK_SIZE_L3];

bool Launch(unsigned char* pPassword,
            int pPasswordLength,
            std::uint8_t pExpanderVersion,
            peanutbutter::ExpansionStrength pExpansionStrength,
            peanutbutter::Logger* pLogger,
            const char* pModeName,
            peanutbutter::ProgressProfileKind pProgressProfile,
            bool (*pShouldCancel)(void*),
            void* pCancelUserData);

namespace {

class ConsoleLogger final : public peanutbutter::Logger {
 public:
  void LogStatus(const std::string& pMessage) override {
    std::cout << pMessage << "\n";
  }

  void LogError(const std::string& pMessage) override {
    std::cerr << pMessage << "\n";
  }

  void LogProgress(const peanutbutter::ProgressInfo& pProgress) override {
    std::cout << "[Progress][" << pProgress.mModeName << "]["
              << peanutbutter::ProgressPhaseToString(pProgress.mPhase) << "] "
              << std::fixed << std::setprecision(1)
              << (pProgress.mOverallFraction * 100.0) << "%";
    if (!pProgress.mDetail.empty()) {
      std::cout << " " << pProgress.mDetail;
    }
    std::cout << "\n";
  }
};

bool NeverCancel(void*) {
  return false;
}

peanutbutter::ExpansionStrength ParseStrength(const std::string& pText) {
  if (pText == "high" || pText == "HIGH" || pText == "High") {
    return peanutbutter::ExpansionStrength::kHigh;
  }
  if (pText == "medium" || pText == "MEDIUM" || pText == "Medium") {
    return peanutbutter::ExpansionStrength::kMedium;
  }
  if (pText == "low" || pText == "LOW" || pText == "Low") {
    return peanutbutter::ExpansionStrength::kLow;
  }
  return peanutbutter::ExpansionStrength::kHigh;
}

peanutbutter::ProgressProfileKind ParseProfile(const std::string& pModeName) {
  if (pModeName == "Bundle") {
    return peanutbutter::ProgressProfileKind::kBundle;
  }
  return peanutbutter::ProgressProfileKind::kUnbundle;
}

void PrintUsage(const char* pProgramName) {
  std::cerr << "Usage: " << pProgramName
            << " <password> [mode-name] [strength] [expander-version]\n";
  std::cerr << "Example: " << pProgramName
            << " \"my password\" Bundle high 1\n";
}

void PrintSuccessSummary() {
  std::cout << "[Expansion][109] Launch succeeded. Shared tables are populated.\n";
  std::cout << "[Expansion][109] L1 tables: 8 x " << BLOCK_SIZE_L1 << " bytes\n";
  std::cout << "[Expansion][109] L2 tables: 6 x " << BLOCK_SIZE_L2 << " bytes\n";
  std::cout << "[Expansion][109] L3 tables: 4 x " << BLOCK_SIZE_L3 << " bytes\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string aPasswordText = argv[1];
  const std::string aModeName = (argc >= 3) ? argv[2] : "Bundle";
  const std::string aStrengthText = (argc >= 4) ? argv[3] : "high";
  const std::uint8_t aExpanderVersion =
      static_cast<std::uint8_t>((argc >= 5) ? std::atoi(argv[4]) : 1);

  peanutbutter::ExpansionStrength aStrength = ParseStrength(aStrengthText);
  peanutbutter::ProgressProfileKind aProfile = ParseProfile(aModeName);

  ConsoleLogger aLogger;
  std::string aPasswordStorage = aPasswordText;
  unsigned char* aPasswordBytes =
      aPasswordStorage.empty()
          ? nullptr
          : reinterpret_cast<unsigned char*>(&aPasswordStorage[0]);

  aLogger.LogStatus("[Expansion][100] Starting expander shell.");

  const bool aSucceeded = Launch(
      aPasswordBytes,
      static_cast<int>(aPasswordStorage.size()),
      aExpanderVersion,
      aStrength,
      &aLogger,
      aModeName.c_str(),
      aProfile,
      &NeverCancel,
      nullptr);

  if (!aSucceeded) {
    aLogger.LogError("[Expansion][198] Launch failed.");
    return 1;
  }

  PrintSuccessSummary();
  return 0;
}
