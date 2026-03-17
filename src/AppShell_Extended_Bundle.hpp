#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_BUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_BUNDLE_HPP_

#include <vector>

#include "AppShell_Common.hpp"
#include "AppShell_Extended_Types.hpp"
#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

OperationResult DiscoverBundlePlanWithMutations(
    const BundleRequest& pRequest,
    const std::vector<SourceEntry>& pSourceEntries,
    const std::vector<CreateFileMutation>& pCreateMutations,
    const std::vector<DeleteFileMutation>& pDeleteMutations,
    FileSystem& pFileSystem,
    BundleDiscovery& pOutDiscovery,
    CancelCoordinator* pCancelCoordinator = nullptr);

OperationResult PerformBundleFlightWithMutations(
    const BundleRequest& pRequest,
    const BundleDiscovery& pDiscovery,
    const std::vector<DataMutation>& pDataMutations,
    FileSystem& pFileSystem,
    CancelCoordinator* pCancelCoordinator = nullptr);

OperationResult BundleWithMutations(
    const BundleRequest& pRequest,
    const std::vector<SourceEntry>& pSourceEntries,
    const std::vector<DataMutation>& pDataMutations,
    const std::vector<CreateFileMutation>& pCreateMutations,
    const std::vector<DeleteFileMutation>& pDeleteMutations,
    FileSystem& pFileSystem,
    CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_BUNDLE_HPP_
