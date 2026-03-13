#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_SANITY_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_SANITY_HPP_

#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

class CancelCoordinator;

OperationResult ValidateSanityInputs(const ValidateRequest& pRequest);
OperationResult RunSanity(const ValidateRequest& pRequest,
                          FileSystem& pFileSystem,
                          Logger& pLogger,
                          CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_SANITY_HPP_
