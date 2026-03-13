#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_TYPES_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_TYPES_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "AppShell_Types.hpp"

namespace peanutbutter {

struct DataMutation {
  std::uint64_t mLogicalOffset = 0;
  std::vector<unsigned char> mOverwriteBytes;
};

struct CreateFileMutation {
  std::size_t mInsertIndex = 0;
  SourceEntry mEntry;
};

struct DeleteFileMutation {
  std::size_t mDeleteIndex = 0;
};

OperationResult ApplyFileMutations(const std::vector<SourceEntry>& pSourceEntries,
                                   const std::vector<CreateFileMutation>& pCreateMutations,
                                   const std::vector<DeleteFileMutation>& pDeleteMutations,
                                   std::vector<SourceEntry>& pOutEntries);

OperationResult ValidateDataMutations(const std::vector<DataMutation>& pDataMutations,
                                      std::uint64_t pMaxLogicalBytes);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_EXTENDED_TYPES_HPP_
