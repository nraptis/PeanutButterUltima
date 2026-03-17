#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_UNBUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_UNBUNDLE_HPP_

#include <string>
#include <vector>

#include "AppShell_Common.hpp"
#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

OperationResult Unbundle(const UnbundleRequest& pRequest,
                         const std::vector<std::string>& pArchiveFileList,
                         FileSystem& pFileSystem,
                         Logger& pLogger,
                         CancelCoordinator* pCancelCoordinator = nullptr);

OperationResult Recover(const UnbundleRequest& pRequest,
                        const std::vector<std::string>& pArchiveFileList,
                        FileSystem& pFileSystem,
                        Logger& pLogger,
                        CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_UNBUNDLE_HPP_
