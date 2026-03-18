#ifndef PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_DISCOVERY_HPP_
#define PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_DISCOVERY_HPP_

#include <vector>

#include "AppShell_Bundle.hpp"

namespace peanutbutter {
namespace bundle_internal {

OperationResult DiscoverBundlePlanCore(const BundleRequest& pRequest,
                                       const std::vector<SourceEntry>& pSourceEntries,
                                       FileSystem& pFileSystem,
                                       Logger& pLogger,
                                       BundleDiscovery& pOutDiscovery,
                                       CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace bundle_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_DISCOVERY_HPP_
