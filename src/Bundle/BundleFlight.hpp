#ifndef PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_FLIGHT_HPP_
#define PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_FLIGHT_HPP_

#include "AppShell_Bundle.hpp"

namespace peanutbutter {
namespace bundle_internal {

OperationResult PerformBundleFlightCore(const BundleRequest& pRequest,
                                        const BundleDiscovery& pDiscovery,
                                        FileSystem& pFileSystem,
                                        Logger& pLogger,
                                        CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace bundle_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_BUNDLE_BUNDLE_FLIGHT_HPP_
