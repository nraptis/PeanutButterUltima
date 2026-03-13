#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_BUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_BUNDLE_HPP_

#include <vector>

#include "AppShell_Common.hpp"
#include "AppShell_Types.hpp"
#include "Encryption/Crypt.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

OperationResult DiscoverBundlePlan(const BundleRequest& pRequest,
                                   const std::vector<SourceEntry>& pSourceEntries,
                                   FileSystem& pFileSystem,
                                   Logger& pLogger,
                                   BundleDiscovery& pOutDiscovery,
                                   CancelCoordinator* pCancelCoordinator = nullptr);

OperationResult PerformBundleFlight(const BundleRequest& pRequest,
                                    const BundleDiscovery& pDiscovery,
                                    FileSystem& pFileSystem,
                                    const Crypt& pCrypt,
                                    Logger& pLogger,
                                    CancelCoordinator* pCancelCoordinator = nullptr);

OperationResult Bundle(const BundleRequest& pRequest,
                       const std::vector<SourceEntry>& pSourceEntries,
                       FileSystem& pFileSystem,
                       const Crypt& pCrypt,
                       Logger& pLogger,
                       CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_BUNDLE_HPP_
