#include "AppCore.hpp"

#include <utility>

#include "AppCore_Helpers.hpp"
#include "AppCore_Validate.hpp"

namespace peanutbutter {

ApplicationCore::ApplicationCore(FileSystem& pFileSystem, Crypt& pCrypt, Logger& pLogger, RuntimeSettings pSettings)
    : mFileSystem(pFileSystem),
      mCrypt(pCrypt),
      mLogger(pLogger),
      mSettings(std::move(pSettings)) {}

PreflightResult ApplicationCore::CheckBundle(const BundleRequest& pRequest) const {
  return detail::CheckBundleJob(mFileSystem, mSettings, pRequest);
}

PreflightResult ApplicationCore::CheckUnbundle(const UnbundleRequest& pRequest) const {
  return detail::CheckUnbundleJob(mFileSystem, pRequest);
}

PreflightResult ApplicationCore::CheckRecover(const RecoverRequest& pRequest) const {
  return detail::CheckRecoverJob(mFileSystem, pRequest);
}

PreflightResult ApplicationCore::CheckValidate(const ValidateRequest& pRequest) const {
  return detail::CheckValidateJob(mFileSystem, pRequest);
}

void ApplicationCore::SetSettings(RuntimeSettings pSettings) {
  mSettings = std::move(pSettings);
}

OperationResult ApplicationCore::RunBundle(const BundleRequest& pRequest, DestinationAction pAction) {
  return detail::RunBundleJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunUnbundle(const UnbundleRequest& pRequest, DestinationAction pAction) {
  return detail::RunUnbundleJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunRecover(const RecoverRequest& pRequest, DestinationAction pAction) {
  return detail::RunRecoverJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunValidate(const ValidateRequest& pRequest) {
  return detail::RunValidateJob(mFileSystem, mLogger, pRequest);
}

}  // namespace peanutbutter
