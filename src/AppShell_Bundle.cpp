#include "AppShell_Bundle.hpp"

#include "Bundle/BundleDiscovery.hpp"
#include "Bundle/BundleFlight.hpp"

namespace peanutbutter {

OperationResult DiscoverBundlePlan(const BundleRequest& pRequest,
                                   const std::vector<SourceEntry>& pSourceEntries,
                                   FileSystem& pFileSystem,
                                   Logger& pLogger,
                                   BundleDiscovery& pOutDiscovery,
                                   CancelCoordinator* pCancelCoordinator) {
  return bundle_internal::DiscoverBundlePlanCore(pRequest,
                                                 pSourceEntries,
                                                 pFileSystem,
                                                 pLogger,
                                                 pOutDiscovery,
                                                 pCancelCoordinator);
}

OperationResult PerformBundleFlight(const BundleRequest& pRequest,
                                    const BundleDiscovery& pDiscovery,
                                    FileSystem& pFileSystem,
                                    Logger& pLogger,
                                    CancelCoordinator* pCancelCoordinator) {
  return bundle_internal::PerformBundleFlightCore(pRequest,
                                                  pDiscovery,
                                                  pFileSystem,
                                                  pLogger,
                                                  pCancelCoordinator);
}

OperationResult Bundle(const BundleRequest& pRequest,
                       const std::vector<SourceEntry>& pSourceEntries,
                       FileSystem& pFileSystem,
                       Logger& pLogger,
                       CancelCoordinator* pCancelCoordinator) {
  ReportProgress(pLogger,
                 "Bundle",
                 ProgressProfileKind::kBundle,
                 ProgressPhase::kPreflight,
                 1.0,
                 "Bundle preflight complete.");
  BundleDiscovery aDiscovery;
  OperationResult aResult = DiscoverBundlePlan(pRequest,
                                               pSourceEntries,
                                               pFileSystem,
                                               pLogger,
                                               aDiscovery,
                                               pCancelCoordinator);
  if (!aResult.mSucceeded) {
    return aResult;
  }
  return PerformBundleFlight(pRequest,
                             aDiscovery,
                             pFileSystem,
                             pLogger,
                             pCancelCoordinator);
}

}  // namespace peanutbutter
