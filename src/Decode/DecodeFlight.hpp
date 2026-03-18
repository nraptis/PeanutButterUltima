#ifndef PEANUT_BUTTER_ULTIMA_DECODE_DECODE_FLIGHT_HPP_
#define PEANUT_BUTTER_ULTIMA_DECODE_DECODE_FLIGHT_HPP_

#include <string>
#include <vector>

#include "AppShell_Unbundle.hpp"

namespace peanutbutter {
namespace decode_internal {

OperationResult RunDecodeCore(const UnbundleRequest& pRequest,
                              const std::vector<std::string>& pArchiveFileList,
                              bool pRecoverMode,
                              FileSystem& pFileSystem,
                              Logger& pLogger,
                              CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace decode_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_DECODE_DECODE_FLIGHT_HPP_
