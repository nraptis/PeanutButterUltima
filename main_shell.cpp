#include <iostream>
#include <string>

#include "AppCore.hpp"
#include "Encryption/RotateMaskBlockCipher.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using namespace peanutbutter::ultima;

constexpr unsigned char kShellRotateMask = 0xAA;
constexpr int kShellRotateShift = 3;

void PrintUsage() {
  std::cerr
      << "Usage:\n"
      << "  PeanutButterUltimaShell bundle <source_dir> <archive_dir> <prefix> <suffix> <archive_size>\n"
      << "  PeanutButterUltimaShell unbundle <archive_dir> <output_dir>\n"
      << "  PeanutButterUltimaShell recover <archive_dir> <recovery_start_file> <output_dir>\n"
      << "  PeanutButterUltimaShell sanity <left_dir> <right_dir>\n";
}

class StdoutLogger final : public Logger {
 public:
  void LogStatus(const std::string& pMessage) override {
    std::cout << pMessage << "\n";
  }

  void LogError(const std::string& pMessage) override {
    std::cerr << "[error] " << pMessage << "\n";
  }
};

int RunBundle(LocalFileSystem& pFileSystem,
              RotateMaskBlockCipher12& pCrypt,
              Logger& pLogger,
              int argc,
              char* argv[]) {
  if (argc < 7) {
    PrintUsage();
    return 2;
  }

  RuntimeSettings aSettings;
  aSettings.mArchiveFileLength = static_cast<std::size_t>(std::stoull(argv[6]));
  ApplicationCore aCore(pFileSystem, pCrypt, pLogger, aSettings);

  BundleRequest aRequest;
  aRequest.mSourceDirectory = argv[2];
  aRequest.mDestinationDirectory = argv[3];
  aRequest.mArchivePrefix = argv[4];
  aRequest.mArchiveSuffix = argv[5];
  aRequest.mUseEncryption = true;

  const OperationResult aResult = aCore.RunBundle(aRequest, DestinationAction::Clear);
  if (!aResult.mSucceeded) {
    std::cerr << aResult.mMessage << "\n";
    return 1;
  }
  std::cout << aResult.mMessage << "\n";
  return 0;
}

int RunUnbundle(LocalFileSystem& pFileSystem,
                RotateMaskBlockCipher12& pCrypt,
                Logger& pLogger,
                int argc,
                char* argv[]) {
  if (argc < 4) {
    PrintUsage();
    return 2;
  }

  RuntimeSettings aSettings;
  ApplicationCore aCore(pFileSystem, pCrypt, pLogger, aSettings);

  UnbundleRequest aRequest;
  aRequest.mArchiveDirectory = argv[2];
  aRequest.mDestinationDirectory = argv[3];
  aRequest.mUseEncryption = true;

  const OperationResult aResult = aCore.RunUnbundle(aRequest, DestinationAction::Clear);
  if (!aResult.mSucceeded) {
    std::cerr << aResult.mMessage << "\n";
    return 1;
  }
  std::cout << aResult.mMessage << "\n";
  return 0;
}

int RunRecover(LocalFileSystem& pFileSystem,
               RotateMaskBlockCipher12& pCrypt,
               Logger& pLogger,
               int argc,
               char* argv[]) {
  if (argc < 5) {
    PrintUsage();
    return 2;
  }

  RuntimeSettings aSettings;
  ApplicationCore aCore(pFileSystem, pCrypt, pLogger, aSettings);

  RecoverRequest aRequest;
  aRequest.mArchiveDirectory = argv[2];
  aRequest.mRecoveryStartFilePath = argv[3];
  aRequest.mDestinationDirectory = argv[4];
  aRequest.mUseEncryption = true;

  const OperationResult aResult = aCore.RunRecover(aRequest, DestinationAction::Clear);
  if (!aResult.mSucceeded) {
    std::cerr << aResult.mMessage << "\n";
    return 1;
  }
  std::cout << aResult.mMessage << "\n";
  return 0;
}

int RunSanity(LocalFileSystem& pFileSystem,
              RotateMaskBlockCipher12& pCrypt,
              Logger& pLogger,
              int argc,
              char* argv[]) {
  if (argc < 4) {
    PrintUsage();
    return 2;
  }

  RuntimeSettings aSettings;
  ApplicationCore aCore(pFileSystem, pCrypt, pLogger, aSettings);

  ValidateRequest aRequest;
  aRequest.mLeftDirectory = argv[2];
  aRequest.mRightDirectory = argv[3];

  const OperationResult aResult = aCore.RunValidate(aRequest);
  if (!aResult.mSucceeded) {
    std::cerr << aResult.mMessage << "\n";
    return 1;
  }
  std::cout << aResult.mMessage << "\n";
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  LocalFileSystem aFileSystem;
  RotateMaskBlockCipher12 aCrypt(kShellRotateMask, kShellRotateShift);
  StdoutLogger aLogger;
  const std::string aCommand = argv[1];

  if (aCommand == "bundle") {
    return RunBundle(aFileSystem, aCrypt, aLogger, argc, argv);
  }
  if (aCommand == "unbundle") {
    return RunUnbundle(aFileSystem, aCrypt, aLogger, argc, argv);
  }
  if (aCommand == "recover") {
    return RunRecover(aFileSystem, aCrypt, aLogger, argc, argv);
  }
  if (aCommand == "sanity") {
    return RunSanity(aFileSystem, aCrypt, aLogger, argc, argv);
  }

  PrintUsage();
  return 2;
}
