#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_VALIDATE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_VALIDATE_HPP_

#include "AppCore.hpp"

namespace peanutbutter::detail {

PreflightResult CheckValidateJob(const FileSystem& pFileSystem, const ValidateRequest& pRequest);
OperationResult RunValidateJob(FileSystem& pFileSystem, Logger& pLogger, const ValidateRequest& pRequest);

}  // namespace peanutbutter::detail

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_VALIDATE_HPP_
