#include "AppCore_Fences.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>

namespace peanutbutter::detail {
namespace {

std::uint64_t SaturatingAdd(std::uint64_t pLeft, std::uint64_t pRight) {
  if (std::numeric_limits<std::uint64_t>::max() - pLeft < pRight) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return pLeft + pRight;
}

std::uint64_t CursorAbsolutePosition(const FenceDomain& pDomain, const FenceCursor& pCursor) {
  if (pCursor.mArchiveIndex >= pDomain.mArchives.size()) {
    return pDomain.mTotalAbsoluteLength;
  }
  const FenceDomainArchive& aArchive = pDomain.mArchives[pCursor.mArchiveIndex];
  const std::uint64_t aPhysicalOffset =
      std::min<std::uint64_t>(pCursor.mPhysicalOffset, static_cast<std::uint64_t>(aArchive.mPayloadLength));
  return aArchive.mAbsoluteArchiveStart + static_cast<std::uint64_t>(kArchiveHeaderLength) + aPhysicalOffset;
}

std::uint32_t ClassifyAbsoluteTargetFlags(const FenceDomain& pDomain, std::uint64_t pAbsoluteTarget) {
  if (pDomain.mArchives.empty()) {
    return kFenceOutsidePayloadRange | kFenceInArchiveHeader;
  }

  const auto aIt = std::upper_bound(
      pDomain.mArchives.begin(),
      pDomain.mArchives.end(),
      pAbsoluteTarget,
      [](std::uint64_t pTarget, const FenceDomainArchive& pArchive) { return pTarget < pArchive.mAbsoluteArchiveStart; });

  const std::size_t aArchiveIndex =
      (aIt == pDomain.mArchives.begin()) ? 0 : static_cast<std::size_t>(std::distance(pDomain.mArchives.begin(), aIt - 1));
  const FenceDomainArchive& aArchive = pDomain.mArchives[aArchiveIndex];
  const std::uint64_t aArchiveFileLength =
      static_cast<std::uint64_t>(kArchiveHeaderLength) + static_cast<std::uint64_t>(aArchive.mPayloadLength);
  const std::uint64_t aArchiveOffset = (pAbsoluteTarget >= aArchive.mAbsoluteArchiveStart)
                                           ? pAbsoluteTarget - aArchive.mAbsoluteArchiveStart
                                           : 0;

  std::uint32_t aFlags = kFenceNone;
  if (aArchive.mMissingGap) {
    aFlags |= kFenceInGapArchive;
  }
  if (aArchiveOffset < static_cast<std::uint64_t>(kArchiveHeaderLength)) {
    aFlags |= kFenceInArchiveHeader;
  } else {
    const std::uint64_t aPayloadOffset = aArchiveOffset - static_cast<std::uint64_t>(kArchiveHeaderLength);
    if ((aPayloadOffset % static_cast<std::uint64_t>(kBlockLength)) < static_cast<std::uint64_t>(kRecoveryHeaderLength)) {
      aFlags |= kFenceInRecoveryHeader;
    }
  }
  if (pAbsoluteTarget >= pDomain.mTotalAbsoluteLength || aArchiveOffset >= aArchiveFileLength ||
      aArchiveOffset >= static_cast<std::uint64_t>(kArchiveHeaderLength) + static_cast<std::uint64_t>(aArchive.mPayloadLength)) {
    aFlags |= kFenceOutsidePayloadRange;
  }
  return aFlags;
}

std::uint32_t BuildLogicalOverflowFlags(const FenceDomain& pDomain,
                                        const FenceCursor& pCursor,
                                        std::uint64_t pLength) {
  std::uint32_t aFlags = kFenceOutsidePayloadRange;
  if (pCursor.mArchiveIndex >= pDomain.mArchives.size()) {
    return aFlags | kFenceInArchiveHeader;
  }

  const std::uint64_t aStartLogical = CursorLogicalOffset(pDomain, pCursor);
  const FenceDomainArchive& aArchive = pDomain.mArchives[pCursor.mArchiveIndex];
  if (aArchive.mReadableLogicalEnd < pDomain.mTotalLogicalLength &&
      SaturatingAdd(aStartLogical, pLength) > aArchive.mReadableLogicalEnd) {
    aFlags |= kFenceInGapArchive;
  }
  if (pLength > 0) {
    aFlags |= ClassifyAbsoluteTargetFlags(pDomain, SaturatingAdd(CursorAbsolutePosition(pDomain, pCursor), pLength));
  }
  return aFlags;
}

std::uint32_t BuildPathLengthFlags(const FenceDomain& pDomain,
                                   const FenceCursor& pCursor,
                                   std::uint64_t pLength) {
  std::uint32_t aFlags = kFenceNone;
  if (pLength == 0) {
    aFlags |= kFencePathLengthZero;
  }
  if (pLength > static_cast<std::uint64_t>(peanutbutter::MAX_VALID_FILE_PATH_LENGTH)) {
    aFlags |= kFencePathLengthExceedsMax;
  }
  if (pLength > ReadableLogicalBytesFromCursor(pDomain, pCursor)) {
    aFlags |= BuildLogicalOverflowFlags(pDomain, pCursor, pLength);
  }
  return aFlags;
}

std::uint32_t BuildContentLengthFlags(const FenceDomain& pDomain,
                                      const FenceCursor& pCursor,
                                      std::uint64_t pLength) {
  if (pLength <= ReadableLogicalBytesFromCursor(pDomain, pCursor)) {
    return kFenceNone;
  }
  return BuildLogicalOverflowFlags(pDomain, pCursor, pLength);
}

std::uint32_t BuildRecoveryDistanceFlags(const FenceDomain& pDomain,
                                         std::size_t pArchiveIndex,
                                         std::size_t pBlockStartOffset,
                                         std::uint64_t pDistance) {
  if (pArchiveIndex >= pDomain.mArchives.size()) {
    return kFenceOutsidePayloadRange | kFenceInArchiveHeader;
  }

  const FenceDomainArchive& aArchive = pDomain.mArchives[pArchiveIndex];
  const std::uint64_t aHeaderEndAbsolute =
      aArchive.mAbsoluteArchiveStart + static_cast<std::uint64_t>(kArchiveHeaderLength) +
      static_cast<std::uint64_t>(pBlockStartOffset) + static_cast<std::uint64_t>(kRecoveryHeaderLength);
  const std::uint64_t aTargetAbsolute = SaturatingAdd(aHeaderEndAbsolute, pDistance);
  std::uint32_t aFlags = ClassifyAbsoluteTargetFlags(pDomain, aTargetAbsolute);

  if (aArchive.mMissingGap) {
    aFlags |= kFenceInGapArchive;
  }

  const std::uint64_t aCandidatePhysicalOffset =
      static_cast<std::uint64_t>(pBlockStartOffset) + static_cast<std::uint64_t>(kRecoveryHeaderLength) + pDistance;
  if (aCandidatePhysicalOffset >= static_cast<std::uint64_t>(aArchive.mPayloadLength)) {
    aFlags |= kFenceOutsidePayloadRange;
  } else if ((aCandidatePhysicalOffset % static_cast<std::uint64_t>(kBlockLength)) <
             static_cast<std::uint64_t>(kRecoveryHeaderLength)) {
    aFlags |= kFenceInRecoveryHeader;
  }

  return aFlags;
}

FenceCode FenceCodeForRule(FenceRule pRule) {
  switch (pRule) {
    case FenceRule::kRecoveryDistance:
      return FenceCode::kRecoveryHeader;
    case FenceRule::kPathLength:
      return FenceCode::kPathLength;
    case FenceRule::kFileContentLength:
      return FenceCode::kFileContentLength;
  }
  return FenceCode::kPathLength;
}

}  // namespace

bool HasFenceFlag(std::uint32_t pFlags, FenceFlag pFlag) {
  return (pFlags & static_cast<std::uint32_t>(pFlag)) != 0;
}

std::string FenceCodeName(FenceCode pCode) {
  switch (pCode) {
    case FenceCode::kRecoveryHeader:
      return "UNP_RHD_FENCE";
    case FenceCode::kPathLength:
      return "UNP_FNL_FENCE";
    case FenceCode::kFileContentLength:
      return "UNP_FDL_FENCE";
  }
  return "UNK_SYS_001";
}

std::string FenceFlagsToString(std::uint32_t pFlags) {
  if (pFlags == kFenceNone) {
    return "FENCE_NONE";
  }

  const std::array<std::pair<std::uint32_t, const char*>, 6> kOrderedFlags = {{
      {kFenceOutsidePayloadRange, "FENCE_OUTSIDE_PAYLOAD_RANGE"},
      {kFenceInRecoveryHeader, "FENCE_IN_RECOVERY_HEADER"},
      {kFenceInArchiveHeader, "FENCE_IN_ARCHIVE_HEADER"},
      {kFenceInGapArchive, "FENCE_IN_GAP_ARCHIVE"},
      {kFencePathLengthZero, "FENCE_PATH_LENGTH_ZERO"},
      {kFencePathLengthExceedsMax, "FENCE_PATH_LENGTH_EXCEEDS_MAX"},
  }};

  std::ostringstream aStream;
  bool aFirst = true;
  for (const auto& aFlag : kOrderedFlags) {
    if ((pFlags & aFlag.first) == 0) {
      continue;
    }
    if (!aFirst) {
      aStream << " | ";
    }
    aStream << aFlag.second;
    aFirst = false;
  }
  return aStream.str();
}

FenceDomain BuildFenceDomain(const std::vector<ArchiveHeaderRecord>& pArchives) {
  FenceDomain aDomain;
  aDomain.mArchives.reserve(pArchives.size());

  std::uint64_t aLogicalPrefix = 0;
  std::uint64_t aAbsolutePrefix = 0;
  for (const ArchiveHeaderRecord& aArchive : pArchives) {
    FenceDomainArchive aEntry;
    aEntry.mPayloadLength = aArchive.mPayloadLength;
    aEntry.mLogicalStart = aLogicalPrefix;
    aEntry.mLogicalLength = static_cast<std::uint64_t>(LogicalCapacityForPhysicalLength(aArchive.mPayloadLength));
    aEntry.mAbsoluteArchiveStart = aAbsolutePrefix;
    aEntry.mMissingGap = aArchive.mMissingGap;
    aDomain.mArchives.push_back(aEntry);

    aLogicalPrefix += aEntry.mLogicalLength;
    aAbsolutePrefix += static_cast<std::uint64_t>(kArchiveHeaderLength) +
                       static_cast<std::uint64_t>(aArchive.mPayloadLength);
  }

  aDomain.mTotalLogicalLength = aLogicalPrefix;
  aDomain.mTotalAbsoluteLength = aAbsolutePrefix;

  std::uint64_t aReadableEnd = aDomain.mTotalLogicalLength;
  for (std::size_t aIndex = aDomain.mArchives.size(); aIndex > 0; --aIndex) {
    FenceDomainArchive& aArchive = aDomain.mArchives[aIndex - 1];
    /*
    if (aArchive.mMissingGap) {
      aReadableEnd = aArchive.mLogicalStart;
    }
    */
    aArchive.mReadableLogicalEnd = aReadableEnd;
  }

  return aDomain;
}

std::uint64_t LogicalOffsetWithinArchive(std::size_t pPhysicalOffset) {
  const std::size_t aBlockIndex = pPhysicalOffset / kBlockLength;
  const std::size_t aOffsetInBlock = pPhysicalOffset % kBlockLength;
  if (aOffsetInBlock <= kRecoveryHeaderLength) {
    return static_cast<std::uint64_t>(aBlockIndex) * static_cast<std::uint64_t>(kPayloadBytesPerBlock);
  }
  return (static_cast<std::uint64_t>(aBlockIndex) * static_cast<std::uint64_t>(kPayloadBytesPerBlock)) +
         static_cast<std::uint64_t>(aOffsetInBlock - kRecoveryHeaderLength);
}

std::uint64_t CursorLogicalOffset(const FenceDomain& pDomain, const FenceCursor& pCursor) {
  if (pCursor.mArchiveIndex >= pDomain.mArchives.size()) {
    return pDomain.mTotalLogicalLength;
  }
  const FenceDomainArchive& aArchive = pDomain.mArchives[pCursor.mArchiveIndex];
  const std::uint64_t aLocalOffset =
      std::min<std::uint64_t>(LogicalOffsetWithinArchive(pCursor.mPhysicalOffset), aArchive.mLogicalLength);
  return aArchive.mLogicalStart + aLocalOffset;
}

std::uint64_t ReadableLogicalBytesFromCursor(const FenceDomain& pDomain, const FenceCursor& pCursor) {
  if (pCursor.mArchiveIndex >= pDomain.mArchives.size()) {
    return 0;
  }
  const FenceDomainArchive& aArchive = pDomain.mArchives[pCursor.mArchiveIndex];
  if (aArchive.mMissingGap) {
    return 0;
  }
  const std::uint64_t aStart = CursorLogicalOffset(pDomain, pCursor);
  if (aArchive.mReadableLogicalEnd <= aStart) {
    return 0;
  }
  return aArchive.mReadableLogicalEnd - aStart;
}

bool FenceCheck(const FenceDomain& pDomain,
                const FenceProbe& pProbe,
                FenceViolation* pViolation) {
  FenceCode aCode = FenceCodeForRule(pProbe.mRule);
  std::uint32_t aFlags = kFenceNone;
  switch (pProbe.mRule) {
    case FenceRule::kRecoveryDistance:
      aFlags = BuildRecoveryDistanceFlags(pDomain, pProbe.mArchiveIndex, pProbe.mBlockStartOffset, pProbe.mValue);
      break;
    case FenceRule::kPathLength:
      aFlags = BuildPathLengthFlags(pDomain, pProbe.mCursor, pProbe.mValue);
      break;
    case FenceRule::kFileContentLength:
      aFlags = BuildContentLengthFlags(pDomain, pProbe.mCursor, pProbe.mValue);
      break;
  }

  if (aFlags == kFenceNone) {
    return false;
  }

  if (pViolation != nullptr) {
    pViolation->mCode = aCode;
    pViolation->mFlags = aFlags;
  }
  return true;
}

FenceResult FenceDetails(const FenceProbe& pProbe, const FenceViolation& pViolation) {
  FenceResult aResult;
  aResult.mCode = pViolation.mCode;
  aResult.mFlags = pViolation.mFlags;
  aResult.mCodeName = FenceCodeName(pViolation.mCode);

  std::string aDetail;
  switch (pProbe.mRule) {
    case FenceRule::kPathLength:
      if (HasFenceFlag(pViolation.mFlags, kFencePathLengthZero)) {
        aDetail = "decoded path length is zero where a path record is required.";
      } else if (HasFenceFlag(pViolation.mFlags, kFencePathLengthExceedsMax) &&
                 (pViolation.mFlags & ~static_cast<std::uint32_t>(kFencePathLengthExceedsMax)) == kFenceNone) {
        aDetail = "decoded path length exceeded MAX_VALID_FILE_PATH_LENGTH.";
      } else {
        aDetail = "decoded path length crossed a fence.";
      }
      break;
    case FenceRule::kFileContentLength:
      aDetail = "decoded file content length crossed a fence.";
      break;
    case FenceRule::kRecoveryDistance:
      aDetail = "decoded recovery-header distance crossed a fence.";
      break;
  }

  aResult.mMessage = aResult.mCodeName + ": " + aDetail;
  if (aResult.mFlags != kFenceNone) {
    aResult.mMessage += " [" + FenceFlagsToString(aResult.mFlags) + "]";
  }
  return aResult;
}

}  // namespace peanutbutter::detail
