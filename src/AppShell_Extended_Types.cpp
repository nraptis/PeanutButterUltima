#include "AppShell_Extended_Types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace peanutbutter {
namespace {

OperationResult MakeSuccess() {
  OperationResult aResult;
  aResult.mSucceeded = true;
  aResult.mErrorCode = ErrorCode::kNone;
  return aResult;
}

OperationResult MakeFailure(ErrorCode pCode, const std::string& pMessage) {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mErrorCode = pCode;
  aResult.mFailureMessage = pMessage;
  return aResult;
}

}  // namespace

OperationResult ApplyFileMutations(const std::vector<SourceEntry>& pSourceEntries,
                                   const std::vector<CreateFileMutation>& pCreateMutations,
                                   const std::vector<DeleteFileMutation>& pDeleteMutations,
                                   std::vector<SourceEntry>& pOutEntries) {
  pOutEntries = pSourceEntries;

  std::vector<std::size_t> aDeleteIndices;
  aDeleteIndices.reserve(pDeleteMutations.size());
  for (const DeleteFileMutation& aDeleteMutation : pDeleteMutations) {
    aDeleteIndices.push_back(aDeleteMutation.mDeleteIndex);
  }
  std::sort(aDeleteIndices.begin(), aDeleteIndices.end());

  if (std::adjacent_find(aDeleteIndices.begin(), aDeleteIndices.end()) != aDeleteIndices.end()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "delete file mutations contain duplicate indices.");
  }

  for (auto aDeleteIt = aDeleteIndices.rbegin(); aDeleteIt != aDeleteIndices.rend(); ++aDeleteIt) {
    const std::size_t aDeleteIndex = *aDeleteIt;
    if (aDeleteIndex >= pOutEntries.size()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "delete file mutation index is outside source entry bounds.");
    }
    pOutEntries.erase(pOutEntries.begin() + static_cast<std::ptrdiff_t>(aDeleteIndex));
  }

  for (const CreateFileMutation& aCreateMutation : pCreateMutations) {
    if (aCreateMutation.mInsertIndex > pOutEntries.size()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "create file mutation insert index is outside source entry bounds.");
    }
    if (aCreateMutation.mEntry.mRelativePath.empty()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "create file mutation requires a non-empty relative path.");
    }
    if (!aCreateMutation.mEntry.mIsDirectory && aCreateMutation.mEntry.mSourcePath.empty()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "create file mutation for file requires a source path.");
    }

    pOutEntries.insert(pOutEntries.begin() + static_cast<std::ptrdiff_t>(aCreateMutation.mInsertIndex),
                       aCreateMutation.mEntry);
  }

  if (pOutEntries.empty()) {
    return MakeFailure(ErrorCode::kInvalidRequest, "file mutations removed all source entries.");
  }
  return MakeSuccess();
}

OperationResult ValidateDataMutations(const std::vector<DataMutation>& pDataMutations,
                                      std::uint64_t pMaxLogicalBytes) {
  for (const DataMutation& aDataMutation : pDataMutations) {
    if (aDataMutation.mOverwriteBytes.empty()) {
      return MakeFailure(ErrorCode::kInvalidRequest, "data mutation overwrite bytes cannot be empty.");
    }
    if (aDataMutation.mLogicalOffset >= pMaxLogicalBytes) {
      return MakeFailure(ErrorCode::kInvalidRequest, "data mutation offset is outside payload bounds.");
    }

    const std::uint64_t aMutationLength =
        static_cast<std::uint64_t>(aDataMutation.mOverwriteBytes.size());
    if (aMutationLength > pMaxLogicalBytes) {
      return MakeFailure(ErrorCode::kInvalidRequest, "data mutation length is outside payload bounds.");
    }

    if (aDataMutation.mLogicalOffset > (pMaxLogicalBytes - aMutationLength)) {
      return MakeFailure(ErrorCode::kInvalidRequest, "data mutation range is outside payload bounds.");
    }
  }
  return MakeSuccess();
}

}  // namespace peanutbutter
