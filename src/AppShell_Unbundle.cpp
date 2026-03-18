#include "AppShell_Unbundle.hpp"

#include "Decode/DecodeFlight.hpp"

namespace peanutbutter {

OperationResult Unbundle(const UnbundleRequest& pRequest,
                         const std::vector<std::string>& pArchiveFileList,
                         FileSystem& pFileSystem,
                         Logger& pLogger,
                         CancelCoordinator* pCancelCoordinator) {
  return decode_internal::RunDecodeCore(pRequest,
                                        pArchiveFileList,
                                        false,
                                        pFileSystem,
                                        pLogger,
                                        pCancelCoordinator);
}

OperationResult Recover(const UnbundleRequest& pRequest,
                        const std::vector<std::string>& pArchiveFileList,
                        FileSystem& pFileSystem,
                        Logger& pLogger,
                        CancelCoordinator* pCancelCoordinator) {
  return decode_internal::RunDecodeCore(pRequest,
                                        pArchiveFileList,
                                        true,
                                        pFileSystem,
                                        pLogger,
                                        pCancelCoordinator);
}

}  // namespace peanutbutter
