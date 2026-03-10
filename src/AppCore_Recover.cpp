#include "AppCore_Helpers.hpp"

namespace peanutbutter::detail {

PreflightResult CheckRecoverJob(FileSystem& pFileSystem, const RecoverRequest& pRequest) {
  if (!pRequest.mRecoveryStartFilePath.empty() && !pFileSystem.IsFile(pRequest.mRecoveryStartFilePath)) {
    return MakeInvalid("Recover Failed", "Recover failed: recovery start file does not exist.");
  }
  return CheckDecodeJob(pFileSystem,
                        "Recover",
                        pRequest.mArchiveDirectory,
                        pRequest.mRecoveryStartFilePath,
                        pRequest.mDestinationDirectory);
}

OperationResult RunRecoverJob(FileSystem& pFileSystem,
                              const Crypt& pCrypt,
                              Logger& pLogger,
                              const RuntimeSettings& pSettings,
                              const RecoverRequest& pRequest,
                              DestinationAction pAction) {
  const PreflightResult aPreflight = CheckRecoverJob(pFileSystem, pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Recover Canceled", "Recover canceled."};
  }
  return RunDecodeJob(pFileSystem,
                      pCrypt,
                      pLogger,
                      pSettings,
                      "Recover",
                      pRequest.mArchiveDirectory,
                      pRequest.mRecoveryStartFilePath,
                      pRequest.mDestinationDirectory,
                      pRequest.mUseEncryption,
                      true,
                      pAction);
}

}  // namespace peanutbutter::detail
