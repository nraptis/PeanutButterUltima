#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_UNVERIFIED_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_UNVERIFIED_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppCore_Helpers.hpp"

namespace peanutbutter::detail {

enum FenceFlag : std::uint32_t {
  kFenceNone = 0u,
  kFenceOutsidePayloadRange = 1u << 0,
  kFenceInRecoveryHeader = 1u << 1,
  kFenceInArchiveHeader = 1u << 2,
  kFenceInGapArchive = 1u << 3,
  kFencePathLengthZero = 1u << 4,
  kFencePathLengthExceedsMax = 1u << 5,
};

enum class FenceCode {
  kRecoveryHeader,
  kPathLength,
  kFileContentLength,
};

enum class FenceRule {
  kRecoveryDistance,
  kPathLength,
  kFileContentLength,
};

struct FenceCursor {
  std::size_t mArchiveIndex = 0;
  std::size_t mPhysicalOffset = 0;
};

struct FenceDomainArchive {
  std::size_t mPayloadLength = 0;
  std::uint64_t mLogicalStart = 0;
  std::uint64_t mLogicalLength = 0;
  std::uint64_t mReadableLogicalEnd = 0;
  std::uint64_t mAbsoluteArchiveStart = 0;
  bool mMissingGap = false;
};

struct FenceDomain {
  std::vector<FenceDomainArchive> mArchives;
  std::uint64_t mTotalLogicalLength = 0;
  std::uint64_t mTotalAbsoluteLength = 0;
};

struct FenceViolation {
  FenceCode mCode = FenceCode::kPathLength;
  std::uint32_t mFlags = kFenceNone;
};

struct FenceProbe {
  FenceRule mRule = FenceRule::kPathLength;
  FenceCursor mCursor;
  std::size_t mArchiveIndex = 0;
  std::size_t mBlockStartOffset = 0;
  std::uint64_t mValue = 0;
};

struct FenceResult {
  FenceCode mCode = FenceCode::kPathLength;
  std::uint32_t mFlags = kFenceNone;
  std::string mCodeName;
  std::string mMessage;
};

bool HasFenceFlag(std::uint32_t pFlags, FenceFlag pFlag);
std::string FenceCodeName(FenceCode pCode);
std::string FenceFlagsToString(std::uint32_t pFlags);

FenceDomain BuildFenceDomain(const std::vector<ArchiveHeaderRecord>& pArchives);
std::uint64_t LogicalOffsetWithinArchive(std::size_t pPhysicalOffset);
std::uint64_t CursorLogicalOffset(const FenceDomain& pDomain, const FenceCursor& pCursor);
std::uint64_t ReadableLogicalBytesFromCursor(const FenceDomain& pDomain, const FenceCursor& pCursor);

bool FenceCheck(const FenceDomain& pDomain,
                const FenceProbe& pProbe,
                FenceViolation* pViolation = nullptr);
FenceResult FenceDetails(const FenceProbe& pProbe, const FenceViolation& pViolation);

PreflightResult CheckValidateJob(const FileSystem& pFileSystem, const ValidateRequest& pRequest);
OperationResult RunValidateJob(FileSystem& pFileSystem, Logger& pLogger, const ValidateRequest& pRequest);

}  // namespace peanutbutter::detail

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_UNVERIFIED_HPP_
