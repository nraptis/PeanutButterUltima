#include "Test_Execute_Unbundle.hpp"

namespace peanutbutter::testing {

OperationResult ExecuteUnbundle(ApplicationCore& pCore, const UnbundleExecutionSpec& pSpec) {
  UnbundleRequest aRequest;
  aRequest.mArchiveDirectory = pSpec.mArchiveDirectory;
  aRequest.mDestinationDirectory = pSpec.mDestinationDirectory;
  aRequest.mUseEncryption = pSpec.mUseEncryption;
  return pCore.RunUnbundle(aRequest, pSpec.mAction);
}

}  // namespace peanutbutter::testing
