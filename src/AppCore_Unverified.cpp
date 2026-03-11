#include "AppCore_Unverified.hpp"
#include "AppCore.hpp"
#include "AppCore_Helpers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

// Role: ApplicationCore facade entry points.
namespace peanutbutter {

ApplicationCore::ApplicationCore(FileSystem& pFileSystem, Crypt& pCrypt, Logger& pLogger, RuntimeSettings pSettings)
    : mFileSystem(pFileSystem),
      mCrypt(pCrypt),
      mLogger(pLogger),
      mSettings(std::move(pSettings)) {}

PreflightResult ApplicationCore::CheckBundle(const BundleRequest& pRequest) const {
  return detail::CheckBundleJob(mFileSystem, mSettings, pRequest);
}

PreflightResult ApplicationCore::CheckUnbundle(const UnbundleRequest& pRequest) const {
  return detail::CheckUnbundleJob(mFileSystem, pRequest);
}

PreflightResult ApplicationCore::CheckRecover(const RecoverRequest& pRequest) const {
  return detail::CheckRecoverJob(mFileSystem, pRequest);
}

PreflightResult ApplicationCore::CheckValidate(const ValidateRequest& pRequest) const {
  return detail::CheckValidateJob(mFileSystem, pRequest);
}

void ApplicationCore::SetSettings(RuntimeSettings pSettings) {
  mSettings = std::move(pSettings);
}

OperationResult ApplicationCore::RunBundle(const BundleRequest& pRequest, DestinationAction pAction) {
  return detail::RunBundleJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunUnbundle(const UnbundleRequest& pRequest, DestinationAction pAction) {
  return detail::RunUnbundleJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunRecover(const RecoverRequest& pRequest, DestinationAction pAction) {
  return detail::RunRecoverJob(mFileSystem, mCrypt, mLogger, mSettings, pRequest, pAction);
}

OperationResult ApplicationCore::RunValidate(const ValidateRequest& pRequest) {
  return detail::RunValidateJob(mFileSystem, mLogger, pRequest);
}

}  // namespace peanutbutter


// Role: Fence modeling and fence-code classification.
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
  const bool aWithinArchiveFile =
      (pAbsoluteTarget < pDomain.mTotalAbsoluteLength) && (aArchiveOffset < aArchiveFileLength);

  std::uint32_t aFlags = kFenceNone;
  if (aArchive.mMissingGap) {
    aFlags |= kFenceInGapArchive;
  }
  if (aWithinArchiveFile) {
    if (aArchiveOffset < static_cast<std::uint64_t>(kArchiveHeaderLength)) {
      aFlags |= kFenceInArchiveHeader;
    } else {
      const std::uint64_t aPayloadOffset = aArchiveOffset - static_cast<std::uint64_t>(kArchiveHeaderLength);
      if ((aPayloadOffset % static_cast<std::uint64_t>(kBlockLength)) <
          static_cast<std::uint64_t>(kRecoveryHeaderLength)) {
        aFlags |= kFenceInRecoveryHeader;
      }
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
    const std::uint64_t aAbsoluteTarget = SaturatingAdd(CursorAbsolutePosition(pDomain, pCursor), pLength);
    aFlags |= ClassifyAbsoluteTargetFlags(pDomain, aAbsoluteTarget);
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
    if (aArchive.mMissingGap) {
      aReadableEnd = aArchive.mLogicalStart;
    }
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


namespace peanutbutter::detail {
namespace {

PreflightResult RoleBundleCheck(FileSystem& pFileSystem,
                                   const RuntimeSettings& pSettings,
                                   const BundleRequest& pRequest);
OperationResult RoleBundleRun(FileSystem& pFileSystem,
                                 const Crypt& pCrypt,
                                 Logger& pLogger,
                                 const RuntimeSettings& pSettings,
                                 const BundleRequest& pRequest,
                                 DestinationAction pAction);

}  // namespace

// Role: Bundle orchestration entry points.
PreflightResult CheckBundleJob(FileSystem& pFileSystem,
                               const RuntimeSettings& pSettings,
                               const BundleRequest& pRequest) {
  return RoleBundleCheck(pFileSystem, pSettings, pRequest);
}

OperationResult RunBundleJob(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const RuntimeSettings& pSettings,
                             const BundleRequest& pRequest,
                             DestinationAction pAction) {
  return RoleBundleRun(pFileSystem, pCrypt, pLogger, pSettings, pRequest, pAction);
}

namespace {

struct PlannedArchiveLayout {
  std::size_t mLogicalStart = 0;
  std::size_t mLogicalEnd = 0;
  std::size_t mUsedPayloadLength = 0;
};

struct PackWorkspace {
  unsigned char mLogicalChunk[kPageLength] = {};
  unsigned char mPageBuffer[kPageLength] = {};
  unsigned char mWorker[kPageLength] = {};
  unsigned char mDestination[kPageLength] = {};
};

std::uint64_t SerializedRecordLength(const SourceFileEntry& pEntry) {
  return 2ULL + static_cast<std::uint64_t>(pEntry.mRelativePath.size()) + 6ULL + pEntry.mContentLength;
}

std::uint64_t SerializedDirectoryRecordLength(const std::string& pPath) {
  return 2ULL + static_cast<std::uint64_t>(pPath.size()) + 6ULL;
}

std::vector<PlannedArchiveLayout> BuildArchiveLayouts(std::size_t pLogicalByteLength,
                                                      std::size_t pLogicalCapacity) {
  std::vector<PlannedArchiveLayout> aLayouts;
  if (pLogicalByteLength == 0 || pLogicalCapacity == 0) {
    return aLayouts;
  }

  const std::size_t aArchiveCount = (pLogicalByteLength + pLogicalCapacity - 1) / pLogicalCapacity;
  aLayouts.reserve(aArchiveCount);
  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aArchiveCount; ++aArchiveIndex) {
    PlannedArchiveLayout aLayout;
    aLayout.mLogicalStart = aArchiveIndex * pLogicalCapacity;
    aLayout.mLogicalEnd = std::min(pLogicalByteLength, aLayout.mLogicalStart + pLogicalCapacity);
    aLayout.mUsedPayloadLength = PhysicalLengthForLogicalLength(aLayout.mLogicalEnd - aLayout.mLogicalStart);
    aLayouts.push_back(aLayout);
  }
  return aLayouts;
}

std::vector<std::size_t> BuildRecordPhysicalOffsetsForLayout(
    const PlannedArchiveLayout& pLayout,
    const std::vector<std::size_t>& pRecordStartLogicalOffsets) {
  std::vector<std::size_t> aPhysicalOffsets;
  const auto aBegin = std::lower_bound(
      pRecordStartLogicalOffsets.begin(), pRecordStartLogicalOffsets.end(), pLayout.mLogicalStart);
  for (auto aIt = aBegin; aIt != pRecordStartLogicalOffsets.end() && *aIt < pLayout.mLogicalEnd; ++aIt) {
    aPhysicalOffsets.push_back(PhysicalOffsetForLogicalOffset(*aIt - pLayout.mLogicalStart));
  }
  return aPhysicalOffsets;
}

bool CopyLogicalBytesIntoPage(unsigned char* pPageBuffer,
                              std::size_t pPageLength,
                              std::size_t pPageLogicalOffset,
                              const unsigned char* pSourceBytes,
                              std::size_t pSourceLength) {
  std::size_t aCopied = 0;
  std::size_t aLogicalOffset = pPageLogicalOffset;
  while (aCopied < pSourceLength) {
    const std::size_t aPhysicalOffset = PhysicalOffsetForLogicalOffset(aLogicalOffset);
    if (aPhysicalOffset >= pPageLength) {
      return false;
    }

    const std::size_t aOffsetInBlockPayload = aLogicalOffset % kPayloadBytesPerBlock;
    const std::size_t aPayloadSpace = kPayloadBytesPerBlock - aOffsetInBlockPayload;
    const std::size_t aPhysicalSpace = pPageLength - aPhysicalOffset;
    const std::size_t aSpan = std::min({pSourceLength - aCopied, aPayloadSpace, aPhysicalSpace});
    std::memcpy(pPageBuffer + aPhysicalOffset, pSourceBytes + aCopied, aSpan);
    aCopied += aSpan;
    aLogicalOffset += aSpan;
  }
  return true;
}

void InitializePageRecoveryHeaders(unsigned char* pPageBuffer,
                                   std::size_t pPageLength,
                                   std::size_t pPageStartOffset,
                                   const std::vector<std::size_t>& pRecordStartPhysicalOffsets) {
  std::memset(pPageBuffer, 0, pPageLength);
  std::size_t aNextRecordOffsetIndex = std::lower_bound(
      pRecordStartPhysicalOffsets.begin(), pRecordStartPhysicalOffsets.end(), pPageStartOffset + kRecoveryHeaderLength) -
                                       pRecordStartPhysicalOffsets.begin();

  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = pPageStartOffset + (aBlockIndex * kBlockLength);
    if (aBlockStart + kRecoveryHeaderLength > pPageStartOffset + pPageLength) {
      break;
    }

    const std::size_t aRecoveryEnd = aBlockStart + kRecoveryHeaderLength;
    while (aNextRecordOffsetIndex < pRecordStartPhysicalOffsets.size() &&
           pRecordStartPhysicalOffsets[aNextRecordOffsetIndex] < aRecoveryEnd) {
      ++aNextRecordOffsetIndex;
    }

    RecoveryHeader aHeader{};
    if (aNextRecordOffsetIndex < pRecordStartPhysicalOffsets.size()) {
      aHeader.mDistanceToNextRecord =
          static_cast<std::uint64_t>(pRecordStartPhysicalOffsets[aNextRecordOffsetIndex] - aRecoveryEnd);
    }
    std::memcpy(pPageBuffer + (aBlockIndex * kBlockLength), &aHeader, sizeof(aHeader));
  }
}

void FinalizePageRecoveryHeaders(unsigned char* pPageBuffer, std::size_t pPageLength) {
  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = aBlockIndex * kBlockLength;
    if (aBlockStart + kRecoveryHeaderLength > pPageLength) {
      break;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, pPageBuffer + aBlockStart, sizeof(aHeader));
    unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
    GenerateChecksum(pPageBuffer + aBlockStart, aChecksum);
    std::memcpy(&aHeader.mChecksum, aChecksum, peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH);
    std::memcpy(pPageBuffer + aBlockStart, &aHeader, sizeof(aHeader));
  }
}

bool WriteArchivePage(FileWriteStream& pStream,
                      const Crypt& pCrypt,
                      PackWorkspace& pWorkspace,
                      std::size_t pPageLength,
                      bool pUseEncryption,
                      std::string* pErrorMessage) {
  if (!pUseEncryption) {
    return pStream.Write(pWorkspace.mPageBuffer, pPageLength);
  }

  std::memset(pWorkspace.mWorker, 0, kPageLength);
  std::memset(pWorkspace.mDestination, 0, kPageLength);
  if (!pCrypt.SealData(pWorkspace.mPageBuffer,
                       pWorkspace.mWorker,
                       pWorkspace.mDestination,
                       kPageLength,
                       pErrorMessage,
                       CryptMode::kNormal)) {
    return false;
  }
  return pStream.Write(pWorkspace.mDestination, pPageLength);
}

class LogicalRecordStreamer {
 public:
  LogicalRecordStreamer(const FileSystem& pFileSystem,
                        const std::vector<SourceFileEntry>& pFiles,
                        const std::vector<std::string>& pEmptyDirectories)
      : mFileSystem(pFileSystem),
        mFiles(pFiles),
        mEmptyDirectories(pEmptyDirectories) {}

  bool Read(unsigned char* pBuffer, std::size_t pMaxBytes, std::size_t& pBytesRead) {
    pBytesRead = 0;
    while (pBytesRead < pMaxBytes) {
      if (mPhase == Phase::kPrepareRecord) {
        if (!PrepareNextRecord()) {
          return false;
        }
        continue;
      }

      if (mPhase == Phase::kPathLengthBytes) {
        const std::size_t aRemainingSource = 2 - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mLengthBytes + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == 2) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kEndMarker) {
            mPhase = Phase::kDone;
          } else {
            mPhase = Phase::kPathBytes;
          }
        }
        continue;
      }

      if (mPhase == Phase::kPathBytes) {
        const std::size_t aRemainingSource = mCurrentPath.size() - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mCurrentPath.data() + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == mCurrentPath.size()) {
          mPhase = Phase::kContentLengthBytes;
          mPhaseOffset = 0;
        }
        continue;
      }

      if (mPhase == Phase::kContentLengthBytes) {
        const std::size_t aRemainingSource = 6 - mPhaseOffset;
        const std::size_t aRemainingDest = pMaxBytes - pBytesRead;
        const std::size_t aCopyLength = std::min(aRemainingSource, aRemainingDest);
        if (aCopyLength > 0) {
          std::memcpy(pBuffer + pBytesRead, mContentLengthBytes + mPhaseOffset, aCopyLength);
          pBytesRead += aCopyLength;
          mPhaseOffset += aCopyLength;
        }
        if (mPhaseOffset == 6) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kFile) {
            mContentOffset = 0;
            mPhase = Phase::kContentBytes;
          } else if (mCurrentKind == RecordKind::kDirectory) {
            ++mDirectoryIndex;
            mPhase = Phase::kPrepareRecord;
          } else {
            return false;
          }
        }
        continue;
      }

      if (mPhase == Phase::kContentBytes) {
        const SourceFileEntry& aFile = mFiles[mFileIndex];
        const std::size_t aBytesToRead = static_cast<std::size_t>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(pMaxBytes - pBytesRead),
                                    aFile.mContentLength - mContentOffset));
        if (aBytesToRead > 0 &&
            !mReadStream->Read(static_cast<std::size_t>(mContentOffset), pBuffer + pBytesRead, aBytesToRead)) {
          return false;
        }
        pBytesRead += aBytesToRead;
        mContentOffset += aBytesToRead;
        if (mContentOffset == aFile.mContentLength) {
          mReadStream.reset();
          ++mFileIndex;
          mPhase = Phase::kPrepareRecord;
        }
        continue;
      }

      if (mPhase == Phase::kDone) {
        return true;
      }
    }

    return true;
  }

 private:
  enum class RecordKind {
    kNone,
    kFile,
    kDirectory,
    kEndMarker,
  };

  enum class Phase {
    kPrepareRecord,
    kPathLengthBytes,
    kPathBytes,
    kContentLengthBytes,
    kContentBytes,
    kDone,
  };

  bool PrepareNextRecord() {
    mPhaseOffset = 0;
    mCurrentPath.clear();
    mCurrentKind = RecordKind::kNone;

    if (mFileIndex < mFiles.size()) {
      const SourceFileEntry& aFile = mFiles[mFileIndex];
      if (aFile.mRelativePath.size() > std::numeric_limits<std::uint16_t>::max() ||
          aFile.mRelativePath.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
        return false;
      }
      mCurrentKind = RecordKind::kFile;
      mCurrentPath = aFile.mRelativePath;
      WriteLeToBytes(mLengthBytes, static_cast<std::uint64_t>(mCurrentPath.size()), 2);
      WriteLeToBytes(mContentLengthBytes, aFile.mContentLength, 6);
      mReadStream = mFileSystem.OpenReadStream(aFile.mSourcePath);
      if (mReadStream == nullptr || !mReadStream->IsReady() ||
          mReadStream->GetLength() != static_cast<std::size_t>(aFile.mContentLength)) {
        return false;
      }
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (mDirectoryIndex < mEmptyDirectories.size()) {
      const std::string& aDirectory = mEmptyDirectories[mDirectoryIndex];
      if (aDirectory.size() > std::numeric_limits<std::uint16_t>::max() ||
          aDirectory.size() > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
        return false;
      }
      mCurrentKind = RecordKind::kDirectory;
      mCurrentPath = aDirectory;
      WriteLeToBytes(mLengthBytes, static_cast<std::uint64_t>(mCurrentPath.size()), 2);
      WriteLeToBytes(mContentLengthBytes, kDirectoryRecordContentMarker, 6);
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (!mEndMarkerWritten) {
      mCurrentKind = RecordKind::kEndMarker;
      mLengthBytes[0] = 0;
      mLengthBytes[1] = 0;
      mEndMarkerWritten = true;
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    mPhase = Phase::kDone;
    return true;
  }

  const FileSystem& mFileSystem;
  const std::vector<SourceFileEntry>& mFiles;
  const std::vector<std::string>& mEmptyDirectories;
  std::size_t mFileIndex = 0;
  std::size_t mDirectoryIndex = 0;
  std::size_t mPhaseOffset = 0;
  std::uint64_t mContentOffset = 0;
  bool mEndMarkerWritten = false;
  RecordKind mCurrentKind = RecordKind::kNone;
  Phase mPhase = Phase::kPrepareRecord;
  std::string mCurrentPath;
  unsigned char mLengthBytes[2] = {};
  unsigned char mContentLengthBytes[6] = {};
  std::unique_ptr<FileReadStream> mReadStream;
};

PreflightResult RoleBundleCheck(FileSystem& pFileSystem,
                                   const RuntimeSettings& pSettings,
                                   const BundleRequest& pRequest) {
  const std::optional<BundleInputSelection> aSourceSelection =
      ResolveBundleInputSelection(pFileSystem, pRequest.mSourceDirectory);
  if (!aSourceSelection.has_value()) {
    return MakeInvalid("Bundle Failed", "Bundle failed: source file or folder does not exist.");
  }

  RuntimeSettings aBundleSettings;
  std::string aErrorMessage;
  if (!TryBuildBundleSettings(pSettings, pRequest, aBundleSettings, &aErrorMessage)) {
    return MakeInvalid("Bundle Failed", aErrorMessage);
  }
  if (EffectiveArchiveLogicalPayloadLength(aBundleSettings) == 0) {
    return MakeInvalid("Bundle Failed", "Bundle failed: archive file length is too small.");
  }
  if (pFileSystem.DirectoryHasEntries(pRequest.mDestinationDirectory)) {
    return MakeNeedsDestination("Bundle Destination", "Bundle destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult RoleBundleRun(FileSystem& pFileSystem,
                                 const Crypt& pCrypt,
                                 Logger& pLogger,
                                 const RuntimeSettings& pSettings,
                                 const BundleRequest& pRequest,
                                 DestinationAction pAction) {
  if (pAction == DestinationAction::Cancel) {
    return {false, "Bundle Canceled", "Bundle canceled."};
  }

  // 1) Validate bundle settings and source selection.
  RuntimeSettings aBundleSettings;
  std::string aSettingsError;
  if (!TryBuildBundleSettings(pSettings, pRequest, aBundleSettings, &aSettingsError)) {
    return MakeFailure(pLogger, "Bundle Failed", aSettingsError);
  }

  const std::optional<BundleInputSelection> aSourceSelection =
      ResolveBundleInputSelection(pFileSystem, pRequest.mSourceDirectory);
  if (!aSourceSelection.has_value()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: source file or folder does not exist.");
  }

  // 2) Build source file/folder manifest once.
  const std::vector<SourceFileEntry> aFiles = CollectSourceEntries(pFileSystem, aSourceSelection.value());
  const std::vector<std::string> aEmptyDirectories = CollectEmptyDirectoryEntries(pFileSystem, aSourceSelection.value());
  if (aFiles.empty() && aEmptyDirectories.empty()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not read source files.");
  }

  // 3) Prepare destination after source validation succeeded.
  if (!ApplyDestinationAction(pFileSystem, pRequest.mDestinationDirectory, pAction)) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not prepare destination directory.");
  }

  pLogger.LogStatus("Bundle job starting...");
  pLogger.LogStatus("Found " + std::to_string(aFiles.size()) + " files and " +
                    std::to_string(aEmptyDirectories.size()) + " empty folders to bundle.");

  const std::size_t aLogicalCapacity = EffectiveArchiveLogicalPayloadLength(aBundleSettings);
  if (aLogicalCapacity == 0) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: archive file length is too small.");
  }

  std::vector<std::size_t> aRecordStartLogicalOffsets;
  std::vector<std::size_t> aFileEndLogicalOffsets;
  aRecordStartLogicalOffsets.reserve(aFiles.size() + aEmptyDirectories.size() + 1);
  aFileEndLogicalOffsets.reserve(aFiles.size());

  std::size_t aTotalLogicalLength = 0;
  std::uint64_t aTotalContentBytes = 0;
  for (const SourceFileEntry& aFile : aFiles) {
    aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalLength += static_cast<std::size_t>(SerializedRecordLength(aFile));
    aFileEndLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalContentBytes += aFile.mContentLength;
  }
  for (const std::string& aDirectory : aEmptyDirectories) {
    aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalLength += static_cast<std::size_t>(SerializedDirectoryRecordLength(aDirectory));
  }
  aRecordStartLogicalOffsets.push_back(aTotalLogicalLength);
  aTotalLogicalLength += 2;

  const std::vector<PlannedArchiveLayout> aLayouts = BuildArchiveLayouts(aTotalLogicalLength, aLogicalCapacity);
  if (aLayouts.empty()) {
    return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: no archive payloads were generated.");
  }

  const std::uint64_t aArchiveIdentifier = GenerateArchiveIdentifier();
  const std::string aSourceStem = pFileSystem.StemName(aSourceSelection->mSourcePath);
  std::unique_ptr<PackWorkspace> aWorkspace = std::make_unique<PackWorkspace>();
  LogicalRecordStreamer aStreamer(pFileSystem, aFiles, aEmptyDirectories);

  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aNextCompletedFileIndex = 0;
  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aLayouts.size(); ++aArchiveIndex) {
    const PlannedArchiveLayout& aLayout = aLayouts[aArchiveIndex];
    const std::vector<std::size_t> aRecordStartPhysicalOffsets =
        BuildRecordPhysicalOffsetsForLayout(aLayout, aRecordStartLogicalOffsets);
    ArchiveHeader aHeader;
    aHeader.mIdentifier = aArchiveIdentifier;
    aHeader.mArchiveIndex = static_cast<std::uint32_t>(aArchiveIndex);
    aHeader.mArchiveCount = static_cast<std::uint32_t>(aLayouts.size());
    aHeader.mPayloadLength = static_cast<std::uint32_t>(aLayout.mUsedPayloadLength);
    aHeader.mRecordCountMod256 = static_cast<std::uint8_t>(aFiles.size() & 0xFFu);
    aHeader.mFolderCountMod256 = static_cast<std::uint8_t>(aEmptyDirectories.size() & 0xFFu);

    const std::string aArchiveName =
        MakeArchiveName(aSourceStem, pRequest.mArchivePrefix, pRequest.mArchiveSuffix, aArchiveIndex + 1, aLayouts.size());
    std::unique_ptr<FileWriteStream> aArchiveStream =
        pFileSystem.OpenWriteStream(pFileSystem.JoinPath(pRequest.mDestinationDirectory, aArchiveName));
    if (aArchiveStream == nullptr || !aArchiveStream->IsReady()) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not write archive " + aArchiveName);
    }

    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    WriteArchiveHeaderBytes(aHeader, aHeaderBytes);
    if (!aArchiveStream->Write(aHeaderBytes, kArchiveHeaderLength)) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not write archive " + aArchiveName);
    }

    const std::size_t aArchiveLogicalLength = aLayout.mLogicalEnd - aLayout.mLogicalStart;
    std::size_t aArchiveLogicalWritten = 0;
    for (std::size_t aPageStart = 0; aPageStart < aLayout.mUsedPayloadLength; aPageStart += kPageLength) {
      const std::size_t aPageLength = std::min(kPageLength, aLayout.mUsedPayloadLength - aPageStart);
      InitializePageRecoveryHeaders(aWorkspace->mPageBuffer,
                                    aPageLength,
                                    aPageStart,
                                    aRecordStartPhysicalOffsets);

      const std::size_t aPageLogicalTarget =
          std::min(LogicalCapacityForPhysicalLength(aPageLength), aArchiveLogicalLength - aArchiveLogicalWritten);
      std::size_t aPageLogicalWritten = 0;
      while (aPageLogicalWritten < aPageLogicalTarget) {
        const std::size_t aChunkTarget = std::min(kPageLength, aPageLogicalTarget - aPageLogicalWritten);
        std::size_t aChunkBytesRead = 0;
        if (!aStreamer.Read(aWorkspace->mLogicalChunk, aChunkTarget, aChunkBytesRead) || aChunkBytesRead == 0) {
          return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not stream source bytes.");
        }
        if (!CopyLogicalBytesIntoPage(aWorkspace->mPageBuffer,
                                      aPageLength,
                                      aPageLogicalWritten,
                                      aWorkspace->mLogicalChunk,
                                      aChunkBytesRead)) {
          return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: page packing overflowed unexpectedly.");
        }
        aPageLogicalWritten += aChunkBytesRead;
      }

      FinalizePageRecoveryHeaders(aWorkspace->mPageBuffer, aPageLength);
      std::string aCryptError;
      if (!WriteArchivePage(*aArchiveStream,
                            pCrypt,
                            *aWorkspace,
                            aPageLength,
                            pRequest.mUseEncryption,
                            &aCryptError)) {
        return MakeFailure(pLogger,
                           "Bundle Failed",
                           aCryptError.empty() ? "Bundle failed: could not write archive " + aArchiveName
                                               : "Bundle failed: " + aCryptError);
      }
      aArchiveLogicalWritten += aPageLogicalWritten;
    }

    if (!aArchiveStream->Close()) {
      return MakeFailure(pLogger, "Bundle Failed", "Bundle failed: could not finalize archive " + aArchiveName);
    }

    while (aNextCompletedFileIndex < aFileEndLogicalOffsets.size() &&
           aFileEndLogicalOffsets[aNextCompletedFileIndex] <= aLayout.mLogicalEnd) {
      ++aFilesProcessed;
      aProcessedBytes += aFiles[aNextCompletedFileIndex].mContentLength;
      ++aNextCompletedFileIndex;
    }

    pLogger.LogStatus("Bundled archive " + std::to_string(aArchiveIndex + 1) + " / " +
                      std::to_string(aLayouts.size()) + ", " + std::to_string(aFilesProcessed) +
                      " files written, " + FormatBytes(aProcessedBytes) + " / " +
                      FormatBytes(aTotalContentBytes) + ".");
  }

  pLogger.LogStatus("Bundle job complete.");
  return {true, "Bundle Complete", "Bundle completed successfully."};
}

}  // namespace
}  // namespace peanutbutter::detail


namespace peanutbutter::detail {
namespace {

struct DecodeSummary {
  std::uint64_t mProcessedBytes = 0;
  std::size_t mFilesProcessed = 0;
  std::size_t mEmptyDirectoriesProcessed = 0;
};

struct DecodeWorkspace {
  unsigned char mSource[kPageLength] = {};
  unsigned char mWorker[kPageLength] = {};
  unsigned char mPage[kPageLength] = {};
  unsigned char mPathBytes[peanutbutter::MAX_VALID_FILE_PATH_LENGTH] = {};
  unsigned char mLengthBytes[8] = {};
  unsigned char mChunk[kFixedIoChunkLength] = {};
  bool mBadBlocks[kPageBlockCount] = {};
};

struct ArchiveDiscoveryResult {
  std::vector<ArchiveHeaderRecord> mArchives;
  std::size_t mScannedFileCount = 0;
  std::size_t mSelectedArchiveOffset = 0;
  bool mSelectedArchiveOffsetValid = false;
};

std::string RoleTextToLowerAscii(std::string pText) {
  std::transform(
      pText.begin(), pText.end(), pText.begin(), [](const unsigned char pCh) { return static_cast<char>(std::tolower(pCh)); });
  return pText;
}

bool DiscoverArchiveSet(FileSystem& pFileSystem,
                        Logger& pLogger,
                        const RuntimeSettings& pSettings,
                        const std::string& pJobName,
                        const std::string& pArchivePathOrDirectory,
                        const std::string& pSelectedArchiveFilePath,
                        ArchiveDiscoveryResult& pResult,
                        std::string& pErrorMessage) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, pSelectedArchiveFilePath);
  if (!aInputSelection.has_value()) {
    pErrorMessage = pJobName + " failed: archive path does not exist.";
    return false;
  }

  const std::vector<DirectoryEntry> aArchiveFiles = CollectArchiveFilesByHeaderScan(pFileSystem, aInputSelection.value());
  if (aArchiveFiles.empty()) {
    pErrorMessage = pJobName + " failed: no files were found to scan for archive headers.";
    return false;
  }

  pLogger.LogStatus("Discovered " + std::to_string(aArchiveFiles.size()) + " files, scanning archive headers.");

  std::vector<ArchiveHeaderRecord> aDecodedHeaders;
  std::map<std::uint32_t, ArchiveHeaderRecord> aByArchiveIndex;
  std::uint64_t aSelectedIdentifier = 0;
  bool aSelectedIdentifierSet = false;
  std::uint32_t aSelectedArchiveIndex = 0;
  bool aSelectedArchiveIndexSet = false;
  std::size_t aGapPayloadLengthHint = EffectiveArchivePhysicalPayloadLength(pSettings);

  pResult.mScannedFileCount = 0;
  for (const DirectoryEntry& aEntry : aArchiveFiles) {
    ++pResult.mScannedFileCount;
    if ((pResult.mScannedFileCount % kScanLogThrottleFileCount) == 0 || pResult.mScannedFileCount == aArchiveFiles.size()) {
      pLogger.LogStatus("Scanned " + std::to_string(pResult.mScannedFileCount) + " of " +
                        std::to_string(aArchiveFiles.size()) + " files...");
    }

    ArchiveHeader aHeader{};
    std::size_t aFileLength = 0;
    if (!TryReadArchiveHeader(pFileSystem, aEntry.mPath, aHeader, aFileLength)) {
      continue;
    }

    ArchiveHeaderRecord aRecord;
    aRecord.mPath = aEntry.mPath;
    aRecord.mName = pFileSystem.FileName(aEntry.mPath);
    aRecord.mHeader = aHeader;
    aRecord.mPayloadLength = static_cast<std::size_t>(aHeader.mPayloadLength);
    if (aRecord.mPayloadLength == 0 && aFileLength >= kArchiveHeaderLength) {
      aRecord.mPayloadLength = aFileLength - kArchiveHeaderLength;
    }
    if (aGapPayloadLengthHint == 0 && aRecord.mPayloadLength > 0) {
      aGapPayloadLengthHint = aRecord.mPayloadLength;
    }
    if (!aInputSelection->mSelectedFilePath.empty() && aEntry.mPath == aInputSelection->mSelectedFilePath) {
      aSelectedIdentifier = aHeader.mIdentifier;
      aSelectedIdentifierSet = true;
      aSelectedArchiveIndex = aHeader.mArchiveIndex;
      aSelectedArchiveIndexSet = true;
    }
    aDecodedHeaders.push_back(std::move(aRecord));
  }

  if (!aSelectedIdentifierSet && !aDecodedHeaders.empty()) {
    aSelectedIdentifier = aDecodedHeaders.front().mHeader.mIdentifier;
    aSelectedIdentifierSet = true;
  }
  if (!aSelectedIdentifierSet) {
    pErrorMessage = pJobName + " failed: no valid archive headers were found.";
    return false;
  }

  for (const ArchiveHeaderRecord& aRecord : aDecodedHeaders) {
    if (aRecord.mHeader.mIdentifier != aSelectedIdentifier) {
      continue;
    }
    if (aByArchiveIndex.find(aRecord.mHeader.mArchiveIndex) == aByArchiveIndex.end()) {
      aByArchiveIndex.emplace(aRecord.mHeader.mArchiveIndex, aRecord);
    }
  }

  if (!aInputSelection->mSelectedFilePath.empty()) {
    pLogger.LogStatus("Selected archive file = " + pFileSystem.FileName(aInputSelection->mSelectedFilePath) + ".");
  }

  if (aByArchiveIndex.empty()) {
    pErrorMessage = pJobName + " failed: no archive set matched selected archive header.";
    return false;
  }

  const std::uint32_t aMinArchiveIndex = aByArchiveIndex.begin()->first;
  std::uint32_t aMaxArchiveIndex = aByArchiveIndex.rbegin()->first;
  for (const auto& aPair : aByArchiveIndex) {
    const ArchiveHeaderRecord& aRecord = aPair.second;
    if (aRecord.mHeader.mArchiveCount == 0) {
      continue;
    }
    // mArchiveCount is the total archive count in the set (0-based indices),
    // not a relative count from each record's own index.
    const std::uint64_t aDeclaredEnd = static_cast<std::uint64_t>(aRecord.mHeader.mArchiveCount - 1);
    if (aDeclaredEnd > aMaxArchiveIndex) {
      aMaxArchiveIndex = static_cast<std::uint32_t>(
          std::min<std::uint64_t>(aDeclaredEnd, std::numeric_limits<std::uint32_t>::max()));
    }
  }

  std::vector<std::pair<std::uint32_t, std::uint32_t>> aMissingRanges;
  bool aInMissingRange = false;
  std::uint32_t aMissingRangeStart = 0;
  pResult.mArchives.clear();
  for (std::uint32_t aArchiveIndex = aMinArchiveIndex; aArchiveIndex <= aMaxArchiveIndex; ++aArchiveIndex) {
    const auto aIt = aByArchiveIndex.find(aArchiveIndex);
    if (aIt != aByArchiveIndex.end()) {
      if (aInMissingRange) {
        aMissingRanges.push_back({aMissingRangeStart, static_cast<std::uint32_t>(aArchiveIndex - 1)});
        aInMissingRange = false;
      }
      pResult.mArchives.push_back(aIt->second);
      if (aSelectedArchiveIndexSet && aArchiveIndex == aSelectedArchiveIndex) {
        pResult.mSelectedArchiveOffset = pResult.mArchives.size() - 1;
        pResult.mSelectedArchiveOffsetValid = true;
      }
      continue;
    }

    if (!aInMissingRange) {
      aInMissingRange = true;
      aMissingRangeStart = aArchiveIndex;
    }
    ArchiveHeaderRecord aGapRecord;
    aGapRecord.mName = "missing_archive_" + std::to_string(aArchiveIndex);
    aGapRecord.mHeader.mIdentifier = aSelectedIdentifier;
    aGapRecord.mHeader.mArchiveIndex = aArchiveIndex;
    aGapRecord.mHeader.mPayloadLength = static_cast<std::uint32_t>(aGapPayloadLengthHint);
    aGapRecord.mPayloadLength = aGapPayloadLengthHint;
    aGapRecord.mMissingGap = true;
    pResult.mArchives.push_back(std::move(aGapRecord));
  }
  if (aInMissingRange) {
    aMissingRanges.push_back({aMissingRangeStart, aMaxArchiveIndex});
  }
  LogMissingArchiveRanges(pLogger, aMissingRanges);
  return true;
}

bool LoadArchivePage(FileSystem& pFileSystem,
                     const Crypt& pCrypt,
                     const ArchiveHeaderRecord& pArchive,
                     bool pUseEncryption,
                     std::size_t pPageStart,
                     unsigned char* pSource,
                     unsigned char* pWorker,
                     unsigned char* pDestination,
                     std::size_t& pPageLength,
                     std::string& pErrorMessage) {
  auto EnsureArchiveReadStream = [&](std::unique_ptr<FileReadStream>& pReadStream) -> bool {
    if (pReadStream != nullptr) {
      return true;
    }
    pReadStream = pFileSystem.OpenReadStream(pArchive.mPath);
    if (pReadStream == nullptr || !pReadStream->IsReady() ||
        pReadStream->GetLength() < (kArchiveHeaderLength + pArchive.mPayloadLength)) {
      pErrorMessage = "could not open archive " + pArchive.mName;
      pReadStream.reset();
      return false;
    }
    return true;
  };

  auto LoadArchivePageFromReadStream = [&](FileReadStream& pReadStream) -> bool {
    pPageLength = 0;
    if (pPageStart >= pArchive.mPayloadLength) {
      return true;
    }

    std::memset(pSource, 0, kPageLength);
    std::memset(pWorker, 0, kPageLength);
    std::memset(pDestination, 0, kPageLength);
    pPageLength = std::min(kPageLength, pArchive.mPayloadLength - pPageStart);
    if (!pReadStream.Read(kArchiveHeaderLength + pPageStart, pSource, pPageLength)) {
      pErrorMessage = "could not read archive " + pArchive.mName;
      return false;
    }

    if (!pUseEncryption) {
      std::memcpy(pDestination, pSource, pPageLength);
      return true;
    }

    if (!pCrypt.UnsealData(pSource,
                           pWorker,
                           pDestination,
                           kPageLength,
                           &pErrorMessage,
                           CryptMode::kNormal)) {
      if (pErrorMessage.empty()) {
        pErrorMessage = "could not decrypt archive " + pArchive.mName;
      } else {
        pErrorMessage = "could not decrypt archive " + pArchive.mName + ": " + pErrorMessage;
      }
      return false;
    }
    return true;
  };

  std::unique_ptr<FileReadStream> aReadStream;
  if (!EnsureArchiveReadStream(aReadStream)) {
    return false;
  }
  return LoadArchivePageFromReadStream(*aReadStream);
}

bool ArchiveHasNonZeroLogicalPayloadBytes(FileSystem& pFileSystem,
                                          const Crypt& pCrypt,
                                          const ArchiveHeaderRecord& pArchive,
                                          bool pUseEncryption,
                                          std::size_t pStartPhysicalOffset,
                                          bool& pHasNonZeroBytes,
                                          std::string& pErrorMessage) {
  pHasNonZeroBytes = false;
  if (pArchive.mMissingGap || pStartPhysicalOffset >= pArchive.mPayloadLength) {
    return true;
  }

  std::array<unsigned char, kPageLength> aSource = {};
  std::array<unsigned char, kPageLength> aWorker = {};
  std::array<unsigned char, kPageLength> aPage = {};
  std::unique_ptr<FileReadStream> aReadStream = pFileSystem.OpenReadStream(pArchive.mPath);
  if (aReadStream == nullptr || !aReadStream->IsReady() ||
      aReadStream->GetLength() < (kArchiveHeaderLength + pArchive.mPayloadLength)) {
    pErrorMessage = "could not open archive " + pArchive.mName;
    return false;
  }

  std::size_t aPageStart = (pStartPhysicalOffset / kPageLength) * kPageLength;
  while (aPageStart < pArchive.mPayloadLength) {
    std::size_t aPageLength = 0;
    std::memset(aSource.data(), 0, kPageLength);
    std::memset(aWorker.data(), 0, kPageLength);
    std::memset(aPage.data(), 0, kPageLength);
    aPageLength = std::min(kPageLength, pArchive.mPayloadLength - aPageStart);
    if (!aReadStream->Read(kArchiveHeaderLength + aPageStart, aSource.data(), aPageLength)) {
      pErrorMessage = "could not read archive " + pArchive.mName;
      return false;
    }
    if (pUseEncryption) {
      std::string aCryptError;
      if (!pCrypt.UnsealData(aSource.data(),
                             aWorker.data(),
                             aPage.data(),
                             kPageLength,
                             &aCryptError,
                             CryptMode::kNormal)) {
        pErrorMessage = aCryptError.empty() ? ("could not decrypt archive " + pArchive.mName)
                                            : ("could not decrypt archive " + pArchive.mName + ": " + aCryptError);
        return false;
      }
    } else {
      std::memcpy(aPage.data(), aSource.data(), aPageLength);
    }

    const std::size_t aLocalStart = (pStartPhysicalOffset > aPageStart) ? (pStartPhysicalOffset - aPageStart) : 0;
    for (std::size_t aLocalOffset = aLocalStart; aLocalOffset < aPageLength; ++aLocalOffset) {
      const std::size_t aPayloadOffset = aPageStart + aLocalOffset;
      const std::size_t aOffsetInBlock = aPayloadOffset % kBlockLength;
      if (aOffsetInBlock < kRecoveryHeaderLength) {
        continue;
      }
      if (aPage[aLocalOffset] != 0) {
        pHasNonZeroBytes = true;
        return true;
      }
    }
    aPageStart += kPageLength;
  }
  return true;
}

enum class LogicalEndTailClassification {
  kNone,
  kDanglingBytes,
  kDanglingArchives,
};

bool ClassifyLogicalEndTail(FileSystem& pFileSystem,
                            const Crypt& pCrypt,
                            const std::vector<ArchiveHeaderRecord>& pArchives,
                            const FenceCursor& pCursor,
                            bool pUseEncryption,
                            LogicalEndTailClassification& pClassification,
                            std::string& pErrorMessage) {
  pClassification = LogicalEndTailClassification::kNone;
  if (pCursor.mArchiveIndex >= pArchives.size()) {
    return true;
  }

  for (std::size_t aArchiveIndex = pCursor.mArchiveIndex + 1; aArchiveIndex < pArchives.size(); ++aArchiveIndex) {
    if (!pArchives[aArchiveIndex].mMissingGap) {
      pClassification = LogicalEndTailClassification::kDanglingArchives;
      return true;
    }
  }

  bool aHasTrailingBytes = false;
  if (!ArchiveHasNonZeroLogicalPayloadBytes(pFileSystem,
                                            pCrypt,
                                            pArchives[pCursor.mArchiveIndex],
                                            pUseEncryption,
                                            pCursor.mPhysicalOffset,
                                            aHasTrailingBytes,
                                            pErrorMessage)) {
    return false;
  }
  if (aHasTrailingBytes) {
    pClassification = LogicalEndTailClassification::kDanglingBytes;
  }
  return true;
}

enum class ReaderFailureMode {
  kNone,
  kFatal,
  kRecoverable,
};

std::size_t BlockStartForPhysicalOffset(std::size_t pPhysicalOffset) {
  return (pPhysicalOffset / kBlockLength) * kBlockLength;
}

std::size_t NextBlockStartForPhysicalOffset(std::size_t pPhysicalOffset) {
  return BlockStartForPhysicalOffset(pPhysicalOffset) + kBlockLength;
}

void MarkBadBlocksForPage(const unsigned char* pPage, std::size_t pPageLength, bool* pBadBlocks) {
  std::memset(pBadBlocks, 0, sizeof(bool) * kPageBlockCount);
  for (std::size_t aBlockIndex = 0; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
    const std::size_t aBlockStart = aBlockIndex * kBlockLength;
    if (aBlockStart + kBlockLength > pPageLength) {
      break;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, pPage + aBlockStart, sizeof(aHeader));
    if (!ChecksumMatches(pPage + aBlockStart, reinterpret_cast<const unsigned char*>(&aHeader.mChecksum))) {
      pBadBlocks[aBlockIndex] = true;
    }
  }
}

bool NormalizeArchiveCursor(const std::vector<ArchiveHeaderRecord>& pArchives, FenceCursor& pCursor) {
  while (pCursor.mArchiveIndex < pArchives.size()) {
    const ArchiveHeaderRecord& aArchive = pArchives[pCursor.mArchiveIndex];
    if (pCursor.mPhysicalOffset >= aArchive.mPayloadLength) {
      ++pCursor.mArchiveIndex;
      pCursor.mPhysicalOffset = 0;
      continue;
    }

    const std::size_t aOffsetInBlock = pCursor.mPhysicalOffset % kBlockLength;
    if (aOffsetInBlock < kRecoveryHeaderLength) {
      const std::size_t aSkip = kRecoveryHeaderLength - aOffsetInBlock;
      if (pCursor.mPhysicalOffset + aSkip > aArchive.mPayloadLength) {
        ++pCursor.mArchiveIndex;
        pCursor.mPhysicalOffset = 0;
        continue;
      }
      pCursor.mPhysicalOffset += aSkip;
      continue;
    }
    return true;
  }
  return true;
}

bool TryReadVerifiedSpanAt(FileSystem& pFileSystem,
                           const Crypt& pCrypt,
                           const std::vector<ArchiveHeaderRecord>& pArchives,
                           bool pUseEncryption,
                           DecodeWorkspace& pWorkspace,
                           FenceCursor& pCursor,
                           unsigned char* pDestination,
                           std::size_t pLength) {
  std::unique_ptr<FileReadStream> aReadStream;
  std::size_t aOpenedArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t aLoadedArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t aLoadedPageStart = 0;
  std::size_t aLoadedPageLength = 0;
  std::size_t aWritten = 0;

  while (aWritten < pLength) {
    if (!NormalizeArchiveCursor(pArchives, pCursor) || pCursor.mArchiveIndex >= pArchives.size()) {
      return false;
    }

    const ArchiveHeaderRecord& aArchive = pArchives[pCursor.mArchiveIndex];
    if (aArchive.mMissingGap) {
      return false;
    }

    if (aOpenedArchiveIndex != pCursor.mArchiveIndex) {
      aReadStream = pFileSystem.OpenReadStream(aArchive.mPath);
      if (aReadStream == nullptr || !aReadStream->IsReady() ||
          aReadStream->GetLength() < (kArchiveHeaderLength + aArchive.mPayloadLength)) {
        return false;
      }
      aOpenedArchiveIndex = pCursor.mArchiveIndex;
      aLoadedArchiveIndex = std::numeric_limits<std::size_t>::max();
    }

    const std::size_t aDesiredPageStart = (pCursor.mPhysicalOffset / kPageLength) * kPageLength;
    if (aLoadedArchiveIndex != pCursor.mArchiveIndex || aLoadedPageStart != aDesiredPageStart) {
      std::memset(pWorkspace.mSource, 0, kPageLength);
      std::memset(pWorkspace.mWorker, 0, kPageLength);
      std::memset(pWorkspace.mPage, 0, kPageLength);
      aLoadedPageLength = std::min(kPageLength, aArchive.mPayloadLength - aDesiredPageStart);
      if (!aReadStream->Read(kArchiveHeaderLength + aDesiredPageStart, pWorkspace.mSource, aLoadedPageLength)) {
        return false;
      }
      if (pUseEncryption) {
        std::string aCryptError;
        if (!pCrypt.UnsealData(pWorkspace.mSource,
                               pWorkspace.mWorker,
                               pWorkspace.mPage,
                               kPageLength,
                               &aCryptError,
                               CryptMode::kNormal)) {
          return false;
        }
      } else {
        std::memcpy(pWorkspace.mPage, pWorkspace.mSource, aLoadedPageLength);
      }
      if (aLoadedPageLength == 0) {
        return false;
      }
      MarkBadBlocksForPage(pWorkspace.mPage, aLoadedPageLength, pWorkspace.mBadBlocks);
      aLoadedArchiveIndex = pCursor.mArchiveIndex;
      aLoadedPageStart = aDesiredPageStart;
    }

    const std::size_t aPageOffset = pCursor.mPhysicalOffset - aLoadedPageStart;
    const std::size_t aBlockIndex = aPageOffset / kBlockLength;
    if (aBlockIndex >= kPageBlockCount || pWorkspace.mBadBlocks[aBlockIndex]) {
      return false;
    }

    const std::size_t aOffsetInBlock = pCursor.mPhysicalOffset % kBlockLength;
    const std::size_t aBytesToBlockEnd = kBlockLength - aOffsetInBlock;
    const std::size_t aBytesToArchiveEnd = aArchive.mPayloadLength - pCursor.mPhysicalOffset;
    const std::size_t aSpan = std::min({pLength - aWritten, aBytesToBlockEnd, aBytesToArchiveEnd});
    std::memcpy(pDestination + aWritten, pWorkspace.mPage + aPageOffset, aSpan);
    pCursor.mPhysicalOffset += aSpan;
    aWritten += aSpan;
  }

  return true;
}

bool LooksLikeRelativePath(const unsigned char* pPathBytes, std::size_t pPathLength) {
  if (pPathLength == 0 || pPathLength > peanutbutter::MAX_VALID_FILE_PATH_LENGTH) {
    return false;
  }
  if (pPathBytes[0] == '/' || pPathBytes[0] == '\\') {
    return false;
  }
  for (std::size_t aIndex = 0; aIndex < pPathLength; ++aIndex) {
    const unsigned char aByte = pPathBytes[aIndex];
    if (aByte == 0 || aByte < 32) {
      return false;
    }
  }
  return true;
}

bool ProbeRecordStart(FileSystem& pFileSystem,
                      const Crypt& pCrypt,
                      const std::vector<ArchiveHeaderRecord>& pArchives,
                      const FenceDomain& pFenceDomain,
                      bool pUseEncryption,
                      DecodeWorkspace& pWorkspace,
                      std::size_t pArchiveOffset,
                      std::size_t pPhysicalOffset,
                      bool* pIsEndMarker = nullptr) {
  FenceCursor aCursor{pArchiveOffset, pPhysicalOffset};
  unsigned char aLengthBytes[2] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aLengthBytes,
                             sizeof(aLengthBytes))) {
    return false;
  }

  const std::size_t aPathLength = static_cast<std::size_t>(ReadLeFromBytes(aLengthBytes, sizeof(aLengthBytes)));
  if (aPathLength == 0) {
    if (pIsEndMarker != nullptr) {
      *pIsEndMarker = true;
    }
    return true;
  }
  if (pIsEndMarker != nullptr) {
    *pIsEndMarker = false;
  }
  FenceProbe aPathProbe;
  aPathProbe.mRule = FenceRule::kPathLength;
  aPathProbe.mCursor = aCursor;
  aPathProbe.mValue = aPathLength;
  if (FenceCheck(pFenceDomain, aPathProbe)) {
    return false;
  }

  unsigned char aPathBytes[peanutbutter::MAX_VALID_FILE_PATH_LENGTH] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aPathBytes,
                             aPathLength) ||
      !LooksLikeRelativePath(aPathBytes, aPathLength)) {
    return false;
  }

  unsigned char aContentLengthBytes[6] = {};
  if (!TryReadVerifiedSpanAt(pFileSystem,
                             pCrypt,
                             pArchives,
                             pUseEncryption,
                             pWorkspace,
                             aCursor,
                             aContentLengthBytes,
                             sizeof(aContentLengthBytes))) {
    return false;
  }

  const std::uint64_t aContentLength = ReadLeFromBytes(aContentLengthBytes, sizeof(aContentLengthBytes));
  if (aContentLength == kDirectoryRecordContentMarker) {
    return true;
  }
  FenceProbe aContentProbe;
  aContentProbe.mRule = FenceRule::kFileContentLength;
  aContentProbe.mCursor = aCursor;
  aContentProbe.mValue = aContentLength;
  return !FenceCheck(pFenceDomain, aContentProbe);
}

bool ResolveRecoveryStartPosition(FileSystem& pFileSystem,
                                  const Crypt& pCrypt,
                                  const std::vector<ArchiveHeaderRecord>& pArchives,
                                  bool pUseEncryption,
                                  std::size_t& pStartArchiveOffset,
                                  std::size_t& pStartPhysicalOffset,
                                  std::string& pErrorMessage) {
  const FenceDomain aFenceDomain = BuildFenceDomain(pArchives);
  std::unique_ptr<DecodeWorkspace> aScanWorkspace = std::make_unique<DecodeWorkspace>();
  std::unique_ptr<DecodeWorkspace> aProbeWorkspace = std::make_unique<DecodeWorkspace>();
  std::string aLastFenceMessage;
  for (std::size_t aArchiveOffset = 0; aArchiveOffset < pArchives.size(); ++aArchiveOffset) {
    const ArchiveHeaderRecord& aArchive = pArchives[aArchiveOffset];
    if (aArchive.mMissingGap || aArchive.mPayloadLength < kBlockLength) {
      continue;
    }

    std::size_t aPageLength = 0;
    std::string aLoadError;
    if (!LoadArchivePage(pFileSystem,
                         pCrypt,
                         aArchive,
                         pUseEncryption,
                         0,
                         aScanWorkspace->mSource,
                         aScanWorkspace->mWorker,
                         aScanWorkspace->mPage,
                         aPageLength,
                         aLoadError)) {
      continue;
    }
    if (aPageLength < kBlockLength) {
      continue;
    }

    MarkBadBlocksForPage(aScanWorkspace->mPage, aPageLength, aScanWorkspace->mBadBlocks);
    if (aScanWorkspace->mBadBlocks[0]) {
      continue;
    }

    RecoveryHeader aHeader{};
    std::memcpy(&aHeader, aScanWorkspace->mPage, sizeof(aHeader));
    FenceProbe aRecoveryProbe;
    aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
    aRecoveryProbe.mArchiveIndex = aArchiveOffset;
    aRecoveryProbe.mBlockStartOffset = 0;
    aRecoveryProbe.mValue = aHeader.mDistanceToNextRecord;
    FenceViolation aRecoveryViolation;
    if (FenceCheck(aFenceDomain, aRecoveryProbe, &aRecoveryViolation)) {
      aLastFenceMessage = FenceDetails(aRecoveryProbe, aRecoveryViolation).mMessage;
      continue;
    }

    const std::uint64_t aCandidate64 =
        static_cast<std::uint64_t>(kRecoveryHeaderLength) + aHeader.mDistanceToNextRecord;
    const std::size_t aCandidate = static_cast<std::size_t>(aCandidate64);
    bool aIsEndMarker = false;
    if (!ProbeRecordStart(
            pFileSystem,
            pCrypt,
            pArchives,
            aFenceDomain,
            pUseEncryption,
            *aProbeWorkspace,
            aArchiveOffset,
            aCandidate,
            &aIsEndMarker)) {
      continue;
    }
    if (aIsEndMarker) {
      continue;
    }

    pStartArchiveOffset = aArchiveOffset;
    pStartPhysicalOffset = aCandidate;
    return true;
  }

  pErrorMessage = aLastFenceMessage.empty() ? "no recovery header pointed to a valid record boundary." : aLastFenceMessage;
  return false;
}

class ArchivePayloadReader {
 public:
  ArchivePayloadReader(FileSystem& pFileSystem,
                       const Crypt& pCrypt,
                       Logger& pLogger,
                       const std::vector<ArchiveHeaderRecord>& pArchives,
                       const FenceDomain& pFenceDomain,
                       std::size_t pStartPhysicalOffset,
                       bool pUseEncryption,
                       bool pRecoverMode)
      : mFileSystem(pFileSystem),
        mCrypt(pCrypt),
        mLogger(pLogger),
        mArchives(pArchives),
        mFenceDomain(pFenceDomain),
        mPhysicalOffset(pStartPhysicalOffset),
        mUseEncryption(pUseEncryption),
        mRecoverMode(pRecoverMode),
        mWorkspace(std::make_unique<DecodeWorkspace>()) {}

  bool Read(unsigned char* pDestination, std::size_t pLength, std::size_t& pBytesRead) {
    pBytesRead = 0;
    mRecoverStoppedAtEnd = false;
    while (pBytesRead < pLength) {
      if (!NormalizePosition()) {
        return false;
      }
      if (mArchiveIndex >= mArchives.size()) {
        if (mRecoverMode) {
          SetRecoverableFailure("UNP_EOF_003: recover reached the end of archive payload.", mArchives.size(), 0);
        } else {
          SetFatalFailure("UNP_EOF_003: unexpected end of archive payload.");
        }
        return false;
      }

      const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
      const std::size_t aBlockStart = BlockStartForPhysicalOffset(mPhysicalOffset);
      const std::size_t aOffsetInBlock = mPhysicalOffset % kBlockLength;
      const std::size_t aBytesToBlockEnd = kBlockLength - aOffsetInBlock;
      const std::size_t aBytesToArchiveEnd = aArchive.mPayloadLength - mPhysicalOffset;
      const std::size_t aSpan = std::min({pLength - pBytesRead, aBytesToBlockEnd, aBytesToArchiveEnd});

      if (aArchive.mMissingGap) {
        if (!mRecoverMode) {
          SetFatalFailure("attempted to read through a missing archive gap.");
        } else {
          SetRecoverableFailure("recover encountered a missing archive gap.", mArchiveIndex, aArchive.mPayloadLength);
        }
        return false;
      }

      if (!EnsurePageLoaded()) {
        return false;
      }

      const std::size_t aPageOffset = mPhysicalOffset - mPageStart;
      const std::size_t aBlockIndex = aPageOffset / kBlockLength;
      if (aBlockIndex >= kPageBlockCount || mWorkspace->mBadBlocks[aBlockIndex]) {
        if (!mRecoverMode) {
          SetFatalFailure("checksum mismatch detected while reading archive payload.");
        } else {
          SetRecoverableFailure("recover encountered a bad checksum while reading archive payload.",
                                mArchiveIndex,
                                aBlockStart + kBlockLength);
        }
        return false;
      }

      const std::size_t aBlockStartInPage = aBlockIndex * kBlockLength;
      if (mLastCheckedRecoveryArchive != mArchiveIndex || mLastCheckedRecoveryBlockStart != aBlockStart) {
        RecoveryHeader aRecoveryHeader{};
        std::memcpy(&aRecoveryHeader, mWorkspace->mPage + aBlockStartInPage, sizeof(aRecoveryHeader));
        FenceProbe aRecoveryProbe;
        aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
        aRecoveryProbe.mArchiveIndex = mArchiveIndex;
        aRecoveryProbe.mBlockStartOffset = aBlockStart;
        aRecoveryProbe.mValue = aRecoveryHeader.mDistanceToNextRecord;
        FenceViolation aRecoveryViolation;
        if (FenceCheck(mFenceDomain, aRecoveryProbe, &aRecoveryViolation)) {
          const FenceResult aFence = FenceDetails(aRecoveryProbe, aRecoveryViolation);
          if (!mRecoverMode) {
            SetFatalFailure(aFence.mMessage);
          } else {
            SetRecoverableFailure(aFence.mMessage, mArchiveIndex, aBlockStart + kBlockLength);
          }
          return false;
        }
        mLastCheckedRecoveryArchive = mArchiveIndex;
        mLastCheckedRecoveryBlockStart = aBlockStart;
      }

      std::memcpy(pDestination + pBytesRead, mWorkspace->mPage + aPageOffset, aSpan);
      mPhysicalOffset += aSpan;
      pBytesRead += aSpan;
    }

    mFailureMode = ReaderFailureMode::kNone;
    (void)NormalizePosition();
    return true;
  }

  std::size_t CompletedArchives() const {
    return mCompletedArchives;
  }

  FenceCursor Cursor() const {
    return {mArchiveIndex, mPhysicalOffset};
  }

  bool HasRecoverableFailure() const {
    return mFailureMode == ReaderFailureMode::kRecoverable;
  }

  bool RecoverStoppedAtEnd() const {
    return mRecoverStoppedAtEnd;
  }

  const std::string& ErrorMessage() const {
    return mErrorMessage;
  }

  void MarkRecoverableFenceFailure(const std::string& pMessage) {
    if (!mRecoverMode) {
      SetFatalFailure(pMessage);
      return;
    }
    if (mArchiveIndex >= mArchives.size()) {
      SetRecoverableFailure(pMessage, mArchives.size(), 0);
      return;
    }
    SetRecoverableFailure(pMessage, mArchiveIndex, NextBlockStartForPhysicalOffset(mPhysicalOffset));
  }

  bool WalkAheadToNextRecord() {
    if (!mRecoverMode) {
      SetFatalFailure("recover walk-ahead is not available during unpack.");
      return false;
    }

    FenceCursor aScanCursor{mWalkAheadArchiveIndex, mWalkAheadPhysicalOffset};
    while (aScanCursor.mArchiveIndex < mArchives.size()) {
      const ArchiveHeaderRecord& aArchive = mArchives[aScanCursor.mArchiveIndex];
      if (aScanCursor.mPhysicalOffset >= aArchive.mPayloadLength) {
        ++aScanCursor.mArchiveIndex;
        aScanCursor.mPhysicalOffset = 0;
        continue;
      }
      aScanCursor.mPhysicalOffset = BlockStartForPhysicalOffset(aScanCursor.mPhysicalOffset);
      break;
    }

    mLogger.LogStatus("Recover walk-ahead starting after " + mErrorMessage);

    std::unique_ptr<DecodeWorkspace> aScanWorkspace = std::make_unique<DecodeWorkspace>();
    std::unique_ptr<DecodeWorkspace> aProbeWorkspace = std::make_unique<DecodeWorkspace>();
    std::uint64_t aScannedBytes = 0;
    std::uint64_t aNextProgressLog = kRecoverLogStepBytes;

    const auto aMaybeLogProgress = [&]() {
      while (aScannedBytes >= aNextProgressLog) {
        mLogger.LogStatus("Recover walk-ahead scanned " + FormatBytes(aScannedBytes) +
                          " without a valid record start yet.");
        aNextProgressLog += kRecoverLogStepBytes;
      }
    };

    for (std::size_t aArchiveOffset = aScanCursor.mArchiveIndex; aArchiveOffset < mArchives.size(); ++aArchiveOffset) {
      const ArchiveHeaderRecord& aArchive = mArchives[aArchiveOffset];
      std::size_t aArchiveStart = (aArchiveOffset == aScanCursor.mArchiveIndex) ? aScanCursor.mPhysicalOffset : 0;
      if (aArchiveStart >= aArchive.mPayloadLength) {
        continue;
      }

      if (aArchive.mMissingGap || aArchive.mPayloadLength < kBlockLength) {
        aScannedBytes += static_cast<std::uint64_t>(aArchive.mPayloadLength - aArchiveStart);
        aMaybeLogProgress();
        continue;
      }

      std::unique_ptr<FileReadStream> aReadStream = mFileSystem.OpenReadStream(aArchive.mPath);
      if (aReadStream == nullptr || !aReadStream->IsReady() ||
          aReadStream->GetLength() < (kArchiveHeaderLength + aArchive.mPayloadLength)) {
        aScannedBytes += static_cast<std::uint64_t>(aArchive.mPayloadLength - aArchiveStart);
        aMaybeLogProgress();
        continue;
      }

      const std::size_t aFirstPageStart = (aArchiveStart / kPageLength) * kPageLength;
      for (std::size_t aPageStart = aFirstPageStart; aPageStart < aArchive.mPayloadLength; aPageStart += kPageLength) {
        const std::size_t aPageLength = std::min(kPageLength, aArchive.mPayloadLength - aPageStart);
        if (aPageLength < kPageLength) {
          const std::size_t aTailLength = kPageLength - aPageLength;
          std::memset(aScanWorkspace->mSource + aPageLength, 0, aTailLength);
          std::memset(aScanWorkspace->mWorker + aPageLength, 0, aTailLength);
          std::memset(aScanWorkspace->mPage + aPageLength, 0, aTailLength);
        }
        if (!aReadStream->Read(kArchiveHeaderLength + aPageStart, aScanWorkspace->mSource, aPageLength)) {
          aScannedBytes += static_cast<std::uint64_t>(aPageLength);
          aMaybeLogProgress();
          continue;
        }

        if (mUseEncryption) {
          std::string aCryptError;
          if (!mCrypt.UnsealData(aScanWorkspace->mSource,
                                 aScanWorkspace->mWorker,
                                 aScanWorkspace->mPage,
                                 kPageLength,
                                 &aCryptError,
                                 CryptMode::kNormal)) {
            aScannedBytes += static_cast<std::uint64_t>(aPageLength);
            aMaybeLogProgress();
            continue;
          }
        } else {
          std::memcpy(aScanWorkspace->mPage, aScanWorkspace->mSource, aPageLength);
        }

        if (aPageLength == 0) {
          aScannedBytes += static_cast<std::uint64_t>(aPageLength);
          aMaybeLogProgress();
          continue;
        }

        MarkBadBlocksForPage(aScanWorkspace->mPage, aPageLength, aScanWorkspace->mBadBlocks);
        const std::size_t aFirstBlockIndex =
            (aPageStart == aFirstPageStart) ? ((aArchiveStart - aPageStart) / kBlockLength) : 0;
        for (std::size_t aBlockIndex = aFirstBlockIndex; aBlockIndex < kPageBlockCount; ++aBlockIndex) {
          const std::size_t aBlockStartInPage = aBlockIndex * kBlockLength;
          const std::size_t aBlockStart = aPageStart + aBlockStartInPage;
          if (aBlockStart + kBlockLength > aArchive.mPayloadLength || aBlockStartInPage + kBlockLength > aPageLength) {
            break;
          }

          if (aScanWorkspace->mBadBlocks[aBlockIndex]) {
            aScannedBytes += kBlockLength;
            aMaybeLogProgress();
            continue;
          }

          RecoveryHeader aHeader{};
          std::memcpy(&aHeader, aScanWorkspace->mPage + aBlockStartInPage, sizeof(aHeader));
          FenceProbe aRecoveryProbe;
          aRecoveryProbe.mRule = FenceRule::kRecoveryDistance;
          aRecoveryProbe.mArchiveIndex = aArchiveOffset;
          aRecoveryProbe.mBlockStartOffset = aBlockStart;
          aRecoveryProbe.mValue = aHeader.mDistanceToNextRecord;
          if (!FenceCheck(mFenceDomain, aRecoveryProbe)) {
            const std::uint64_t aCandidate64 =
                static_cast<std::uint64_t>(aBlockStart) + static_cast<std::uint64_t>(kRecoveryHeaderLength) +
                aHeader.mDistanceToNextRecord;
            const std::size_t aCandidate = static_cast<std::size_t>(aCandidate64);
            bool aIsEndMarker = false;
            if (ProbeRecordStart(mFileSystem,
                                 mCrypt,
                                 mArchives,
                                 mFenceDomain,
                                 mUseEncryption,
                                 *aProbeWorkspace,
                                 aArchiveOffset,
                                 aCandidate,
                                 &aIsEndMarker) &&
                !aIsEndMarker) {
              AddRecoverSkippedBytes(aScannedBytes);
              SetPosition(aArchiveOffset, aCandidate);
              mLogger.LogStatus("Recover walk-ahead resynced at " + aArchive.mName + " +" +
                                FormatBytes(aCandidate) + " after scanning " +
                                FormatBytes(aScannedBytes) + ".");
              return true;
            }
          }

          aScannedBytes += kBlockLength;
          aMaybeLogProgress();
        }
      }
    }

    AddRecoverSkippedBytes(aScannedBytes);
    mLogger.LogStatus("Recover walk-ahead reached the end of the archive set after scanning " +
                      FormatBytes(aScannedBytes) + ".");
    ResetPageState();
    mArchiveIndex = mArchives.size();
    mPhysicalOffset = 0;
    mCompletedArchives = mArchives.size();
    mFailureMode = ReaderFailureMode::kNone;
    mRecoverStoppedAtEnd = true;
    return true;
  }

  void FlushRecoverLog() {
    if (mRecoverMode && mRecoverSkippedBytes > 0) {
      mLogger.LogStatus("Recover: skipped " + FormatBytes(mRecoverSkippedBytes) +
                        " while resynchronizing damaged payload.");
    }
  }

 private:
  bool NormalizePosition() {
    while (mArchiveIndex < mArchives.size()) {
      const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
      if (mPhysicalOffset >= aArchive.mPayloadLength) {
        ++mCompletedArchives;
        ++mArchiveIndex;
        mPhysicalOffset = 0;
        ResetPageState();
        continue;
      }

      const std::size_t aOffsetInBlock = mPhysicalOffset % kBlockLength;
      if (aOffsetInBlock < kRecoveryHeaderLength) {
        const std::size_t aSkip = kRecoveryHeaderLength - aOffsetInBlock;
        if (mPhysicalOffset + aSkip > aArchive.mPayloadLength) {
          ++mCompletedArchives;
          ++mArchiveIndex;
          mPhysicalOffset = 0;
          ResetPageState();
          continue;
        }
        mPhysicalOffset += aSkip;
        continue;
      }
      return true;
    }
    return true;
  }

  void ResetPageState() {
    mPageLoaded = false;
    mPageArchiveIndex = std::numeric_limits<std::size_t>::max();
    mPageStart = 0;
    mOpenedArchiveIndex = std::numeric_limits<std::size_t>::max();
    mReadStream.reset();
  }

  void SetFatalFailure(const std::string& pMessage) {
    mFailureMode = ReaderFailureMode::kFatal;
    mErrorMessage = pMessage;
    mRecoverStoppedAtEnd = false;
  }

  void SetRecoverableFailure(const std::string& pMessage,
                             std::size_t pArchiveIndex,
                             std::size_t pPhysicalOffset) {
    mFailureMode = ReaderFailureMode::kRecoverable;
    mErrorMessage = pMessage;
    mWalkAheadArchiveIndex = pArchiveIndex;
    mWalkAheadPhysicalOffset = pPhysicalOffset;
    mRecoverStoppedAtEnd = false;
  }

  void SetPosition(std::size_t pArchiveIndex, std::size_t pPhysicalOffset) {
    mArchiveIndex = pArchiveIndex;
    mPhysicalOffset = pPhysicalOffset;
    if (mCompletedArchives < pArchiveIndex) {
      mCompletedArchives = pArchiveIndex;
    }
    mFailureMode = ReaderFailureMode::kNone;
    mErrorMessage.clear();
    mRecoverStoppedAtEnd = false;
    ResetPageState();
  }

  bool OpenCurrentArchive() {
    if (mArchiveIndex >= mArchives.size()) {
      return false;
    }
    if (mOpenedArchiveIndex == mArchiveIndex && mReadStream != nullptr) {
      return true;
    }

    const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
    mReadStream = mFileSystem.OpenReadStream(aArchive.mPath);
    if (mReadStream == nullptr || !mReadStream->IsReady() ||
        mReadStream->GetLength() < (kArchiveHeaderLength + aArchive.mPayloadLength)) {
      mErrorMessage = "could not open archive " + aArchive.mName;
      return false;
    }
    mOpenedArchiveIndex = mArchiveIndex;
    return true;
  }

  bool EnsurePageLoaded() {
    if (mArchiveIndex >= mArchives.size()) {
      return false;
    }

    const ArchiveHeaderRecord& aArchive = mArchives[mArchiveIndex];
    const std::size_t aDesiredPageStart = (mPhysicalOffset / kPageLength) * kPageLength;
    if (mPageLoaded && mPageArchiveIndex == mArchiveIndex && mPageStart == aDesiredPageStart) {
      return true;
    }

    if (!OpenCurrentArchive()) {
      if (mRecoverMode) {
        SetRecoverableFailure(mErrorMessage.empty() ? "recover could not open an archive page."
                                                    : "recover " + mErrorMessage + ".",
                              mArchiveIndex,
                              aArchive.mPayloadLength);
      } else {
        SetFatalFailure(mErrorMessage.empty() ? "could not open archive page." : mErrorMessage);
      }
      return false;
    }

    std::memset(mWorkspace->mSource, 0, kPageLength);
    std::memset(mWorkspace->mWorker, 0, kPageLength);
    std::memset(mWorkspace->mPage, 0, kPageLength);
    std::memset(mWorkspace->mBadBlocks, 0, sizeof(mWorkspace->mBadBlocks));

    const std::size_t aPageLength = std::min(kPageLength, aArchive.mPayloadLength - aDesiredPageStart);
    if (!mReadStream->Read(kArchiveHeaderLength + aDesiredPageStart, mWorkspace->mSource, aPageLength)) {
      const std::string aMessage = "could not read archive " + aArchive.mName;
      if (mRecoverMode) {
        SetRecoverableFailure("recover encountered an unreadable archive page in " + aArchive.mName + ".",
                              mArchiveIndex,
                              aDesiredPageStart + kPageLength);
      } else {
        SetFatalFailure(aMessage);
      }
      return false;
    }

    if (mUseEncryption) {
      std::string aCryptError;
      if (!mCrypt.UnsealData(mWorkspace->mSource,
                             mWorkspace->mWorker,
                             mWorkspace->mPage,
                             kPageLength,
                             &aCryptError,
                             CryptMode::kNormal)) {
        const std::string aMessage = aCryptError.empty() ? "could not decrypt archive " + aArchive.mName
                                                         : "could not decrypt archive " + aArchive.mName + ": " + aCryptError;
        if (mRecoverMode) {
          SetRecoverableFailure("recover encountered an unreadable encrypted page in " + aArchive.mName + ".",
                                mArchiveIndex,
                                aDesiredPageStart + kPageLength);
        } else {
          SetFatalFailure(aMessage);
        }
        return false;
      }
    } else {
      std::memcpy(mWorkspace->mPage, mWorkspace->mSource, aPageLength);
    }

    MarkBadBlocksForPage(mWorkspace->mPage, aPageLength, mWorkspace->mBadBlocks);
    mPageArchiveIndex = mArchiveIndex;
    mPageStart = aDesiredPageStart;
    mPageLoaded = true;
    return true;
  }

  void AddRecoverSkippedBytes(std::uint64_t pBytes) {
    mRecoverSkippedBytes += pBytes;
  }

  FileSystem& mFileSystem;
  const Crypt& mCrypt;
  Logger& mLogger;
  const std::vector<ArchiveHeaderRecord>& mArchives;
  const FenceDomain& mFenceDomain;
  bool mUseEncryption = false;
  bool mRecoverMode = false;
  std::unique_ptr<DecodeWorkspace> mWorkspace;
  std::unique_ptr<FileReadStream> mReadStream;
  std::size_t mArchiveIndex = 0;
  std::size_t mPhysicalOffset = 0;
  std::size_t mCompletedArchives = 0;
  std::size_t mPageArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t mPageStart = 0;
  bool mPageLoaded = false;
  std::size_t mOpenedArchiveIndex = std::numeric_limits<std::size_t>::max();
  std::size_t mWalkAheadArchiveIndex = 0;
  std::size_t mWalkAheadPhysicalOffset = 0;
  std::size_t mLastCheckedRecoveryArchive = std::numeric_limits<std::size_t>::max();
  std::size_t mLastCheckedRecoveryBlockStart = std::numeric_limits<std::size_t>::max();
  ReaderFailureMode mFailureMode = ReaderFailureMode::kNone;
  bool mRecoverStoppedAtEnd = false;
  std::uint64_t mRecoverSkippedBytes = 0;
  std::string mErrorMessage;
};

bool DecodeArchiveSetUnbundle(FileSystem& pFileSystem,
                              const Crypt& pCrypt,
                              Logger& pLogger,
                              const std::vector<ArchiveHeaderRecord>& pArchives,
                              std::size_t pStartPhysicalOffset,
                              const std::string& pDestinationDirectory,
                              bool pWriteFiles,
                              bool pUseEncryption,
                              DecodeSummary& pSummary,
                              std::string& pErrorMessage) {
  const FenceDomain aFenceDomain = BuildFenceDomain(pArchives);
  ArchivePayloadReader aReader(
      pFileSystem, pCrypt, pLogger, pArchives, aFenceDomain, pStartPhysicalOffset, pUseEncryption, false);
  std::unique_ptr<DecodeWorkspace> aWorkspace = std::make_unique<DecodeWorkspace>();
  std::size_t aLastLoggedArchive = 0;

  const auto aLogArchiveProgress = [&]() {
    while (aLastLoggedArchive < aReader.CompletedArchives()) {
      ++aLastLoggedArchive;
      pLogger.LogStatus("Unbundled archive " + std::to_string(aLastLoggedArchive) + " / " +
                        std::to_string(pArchives.size()) + ", " + std::to_string(pSummary.mFilesProcessed) +
                        " files written, " + FormatBytes(pSummary.mProcessedBytes) + ".");
    }
  };

  bool aDone = false;
  while (!aDone) {
    std::size_t aBytesRead = 0;
    if (!aReader.Read(aWorkspace->mLengthBytes, 2, aBytesRead)) {
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FNL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    const std::size_t aPathLength = static_cast<std::size_t>(ReadLeFromBytes(aWorkspace->mLengthBytes, 2));
    if (aPathLength == 0) {
      LogicalEndTailClassification aTailClassification = LogicalEndTailClassification::kNone;
      if (!ClassifyLogicalEndTail(
              pFileSystem, pCrypt, pArchives, aReader.Cursor(), pUseEncryption, aTailClassification, pErrorMessage)) {
        return false;
      }
      if (aTailClassification == LogicalEndTailClassification::kDanglingArchives) {
        pErrorMessage = "UNP_EOF_001: dangling archives after logical end of stream.";
        return false;
      }
      if (aTailClassification == LogicalEndTailClassification::kDanglingBytes) {
        pErrorMessage = "UNP_EOF_002: dangling bytes after logical end of stream.";
        return false;
      }
      aDone = true;
      break;
    }

    FenceProbe aPathProbe;
    aPathProbe.mRule = FenceRule::kPathLength;
    aPathProbe.mCursor = aReader.Cursor();
    aPathProbe.mValue = aPathLength;
    FenceViolation aPathViolation;
    if (FenceCheck(aFenceDomain, aPathProbe, &aPathViolation)) {
      pErrorMessage = FenceDetails(aPathProbe, aPathViolation).mMessage;
      return false;
    }

    if (!aReader.Read(aWorkspace->mPathBytes, aPathLength, aBytesRead)) {
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FNL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    const std::string aRelativePath(reinterpret_cast<const char*>(aWorkspace->mPathBytes), aPathLength);
    if (!LooksLikeRelativePath(aWorkspace->mPathBytes, aPathLength)) {
      pErrorMessage = "UNP_FNL_FENCE: decoded record path failed validation.";
      return false;
    }

    if (!aReader.Read(aWorkspace->mLengthBytes, 6, aBytesRead)) {
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    std::uint64_t aContentLength = ReadLeFromBytes(aWorkspace->mLengthBytes, 6);
    if (aContentLength == kDirectoryRecordContentMarker) {
      if (pWriteFiles && !pFileSystem.EnsureDirectory(pFileSystem.JoinPath(pDestinationDirectory, aRelativePath))) {
        pErrorMessage = "could not create empty directory " + aRelativePath;
        return false;
      }
      ++pSummary.mEmptyDirectoriesProcessed;
      continue;
    }

    FenceProbe aContentProbe;
    aContentProbe.mRule = FenceRule::kFileContentLength;
    aContentProbe.mCursor = aReader.Cursor();
    aContentProbe.mValue = aContentLength;
    FenceViolation aContentViolation;
    if (FenceCheck(aFenceDomain, aContentProbe, &aContentViolation)) {
      pErrorMessage = FenceDetails(aContentProbe, aContentViolation).mMessage;
      return false;
    }

    std::unique_ptr<FileWriteStream> aWriteStream;
    if (pWriteFiles) {
      const std::string aOutputPath = pFileSystem.JoinPath(pDestinationDirectory, aRelativePath);
      const std::string aParentPath = pFileSystem.ParentPath(aOutputPath);
      if (!aParentPath.empty() && aParentPath != aOutputPath && !pFileSystem.EnsureDirectory(aParentPath)) {
        pErrorMessage = "could not prepare output directory for " + aRelativePath;
        return false;
      }
      aWriteStream = pFileSystem.OpenWriteStream(aOutputPath);
      if (aWriteStream == nullptr || !aWriteStream->IsReady()) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
    }

    std::uint64_t aRecoveredFileBytes = 0;
    while (aContentLength > 0) {
      const std::size_t aChunkLength = static_cast<std::size_t>(
          std::min<std::uint64_t>(aContentLength, static_cast<std::uint64_t>(kFixedIoChunkLength)));
      aBytesRead = 0;
      if (!aReader.Read(aWorkspace->mChunk, aChunkLength, aBytesRead)) {
        if (aBytesRead > 0) {
          if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aBytesRead)) {
            pErrorMessage = "could not write " + aRelativePath;
            return false;
          }
          aRecoveredFileBytes += aBytesRead;
          aContentLength -= aBytesRead;
        }
        aLogArchiveProgress();
        pErrorMessage = aReader.ErrorMessage();
        if (pErrorMessage.rfind("UNP_", 0) != 0) {
          pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
        }
        return false;
      }

      aLogArchiveProgress();
      if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aChunkLength)) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
      aContentLength -= aChunkLength;
      aRecoveredFileBytes += aChunkLength;
    }

    if (aWriteStream != nullptr && !aWriteStream->Close()) {
      pErrorMessage = "could not finalize " + aRelativePath;
      return false;
    }
    pSummary.mProcessedBytes += aRecoveredFileBytes;
    ++pSummary.mFilesProcessed;
  }

  aLogArchiveProgress();
  return true;
}

bool DecodeArchiveSetRecover(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const std::vector<ArchiveHeaderRecord>& pArchives,
                             std::size_t pStartPhysicalOffset,
                             const std::string& pDestinationDirectory,
                             bool pWriteFiles,
                             bool pUseEncryption,
                             DecodeSummary& pSummary,
                             std::string& pErrorMessage) {
  const FenceDomain aFenceDomain = BuildFenceDomain(pArchives);
  ArchivePayloadReader aReader(
      pFileSystem, pCrypt, pLogger, pArchives, aFenceDomain, pStartPhysicalOffset, pUseEncryption, true);
  std::unique_ptr<DecodeWorkspace> aWorkspace = std::make_unique<DecodeWorkspace>();
  std::size_t aLastLoggedArchive = 0;

  const auto aLogArchiveProgress = [&]() {
    while (aLastLoggedArchive < aReader.CompletedArchives()) {
      ++aLastLoggedArchive;
      pLogger.LogStatus("Recovered archive " + std::to_string(aLastLoggedArchive) + " / " +
                        std::to_string(pArchives.size()) + ", " + std::to_string(pSummary.mFilesProcessed) +
                        " files written, " + FormatBytes(pSummary.mProcessedBytes) + ".");
    }
  };

  const auto aHandleRecoverFailure = [&](const std::string& pReason,
                                         std::unique_ptr<FileWriteStream>* pWriteStream,
                                         const std::string& pRelativePath,
                                         std::uint64_t pPartialBytes) -> bool {
    if (!pReason.empty()) {
      aReader.MarkRecoverableFenceFailure(pReason);
    }

    if (pWriteStream != nullptr && pWriteStream->get() != nullptr) {
      const std::size_t aBytesWritten = (*pWriteStream)->GetBytesWritten();
      if (!(*pWriteStream)->Close()) {
        pErrorMessage = "could not finalize partial file " + pRelativePath;
        return false;
      }
      pWriteStream->reset();
      pLogger.LogStatus("Recover: abandoned partial file " + pRelativePath + " after " +
                        FormatBytes(std::max<std::uint64_t>(pPartialBytes, aBytesWritten)) + ".");
    } else if (!pRelativePath.empty()) {
      pLogger.LogStatus("Recover: abandoned record " + pRelativePath + " before completion.");
    }

    if (!aReader.WalkAheadToNextRecord()) {
      pErrorMessage = aReader.ErrorMessage();
      return false;
    }

    aLogArchiveProgress();
    return true;
  };

  bool aDone = false;
  while (!aDone) {
    std::size_t aBytesRead = 0;
    if (!aReader.Read(aWorkspace->mLengthBytes, 2, aBytesRead)) {
      if (aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, std::string(), 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      return false;
    }
    aLogArchiveProgress();

    const std::size_t aPathLength = static_cast<std::size_t>(ReadLeFromBytes(aWorkspace->mLengthBytes, 2));
    if (aPathLength == 0) {
      LogicalEndTailClassification aTailClassification = LogicalEndTailClassification::kNone;
      if (!ClassifyLogicalEndTail(
              pFileSystem, pCrypt, pArchives, aReader.Cursor(), pUseEncryption, aTailClassification, pErrorMessage)) {
        return false;
      }

      if (aTailClassification == LogicalEndTailClassification::kNone) {
        aDone = true;
        break;
      }

      std::string aRecoverReason =
          "UNP_EOF_003: unexpected end of archive (zero-length path marker before payload exhaustion).";
      if (aTailClassification == LogicalEndTailClassification::kDanglingArchives) {
        aRecoverReason =
            "UNP_EOF_003: unexpected end of archive (zero-length path marker before trailing archives).";
      }
      if (!aHandleRecoverFailure(aRecoverReason, nullptr, std::string(), 0)) {
        return false;
      }
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    FenceProbe aPathProbe;
    aPathProbe.mRule = FenceRule::kPathLength;
    aPathProbe.mCursor = aReader.Cursor();
    aPathProbe.mValue = aPathLength;
    FenceViolation aPathViolation;
    if (FenceCheck(aFenceDomain, aPathProbe, &aPathViolation)) {
      if (!aHandleRecoverFailure(FenceDetails(aPathProbe, aPathViolation).mMessage, nullptr, std::string(), 0)) {
        return false;
      }
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    if (!aReader.Read(aWorkspace->mPathBytes, aPathLength, aBytesRead)) {
      if (aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, std::string(), 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FNL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    const std::string aRelativePath(reinterpret_cast<const char*>(aWorkspace->mPathBytes), aPathLength);
    if (!LooksLikeRelativePath(aWorkspace->mPathBytes, aPathLength)) {
      if (!aHandleRecoverFailure("recover encountered an invalid record path fence.", nullptr, std::string(), 0)) {
        return false;
      }
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    if (!aReader.Read(aWorkspace->mLengthBytes, 6, aBytesRead)) {
      if (aReader.HasRecoverableFailure()) {
        if (!aHandleRecoverFailure(std::string(), nullptr, aRelativePath, 0)) {
          return false;
        }
        if (aReader.RecoverStoppedAtEnd()) {
          break;
        }
        continue;
      }
      pErrorMessage = aReader.ErrorMessage();
      if (pErrorMessage.rfind("UNP_", 0) != 0) {
        pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
      }
      return false;
    }
    aLogArchiveProgress();

    std::uint64_t aContentLength = ReadLeFromBytes(aWorkspace->mLengthBytes, 6);
    if (aContentLength == kDirectoryRecordContentMarker) {
      if (pWriteFiles && !pFileSystem.EnsureDirectory(pFileSystem.JoinPath(pDestinationDirectory, aRelativePath))) {
        pErrorMessage = "could not create empty directory " + aRelativePath;
        return false;
      }
      ++pSummary.mEmptyDirectoriesProcessed;
      continue;
    }

    FenceProbe aContentProbe;
    aContentProbe.mRule = FenceRule::kFileContentLength;
    aContentProbe.mCursor = aReader.Cursor();
    aContentProbe.mValue = aContentLength;
    FenceViolation aContentViolation;
    if (FenceCheck(aFenceDomain, aContentProbe, &aContentViolation)) {
      if (!aHandleRecoverFailure(FenceDetails(aContentProbe, aContentViolation).mMessage, nullptr, aRelativePath, 0)) {
        return false;
      }
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    std::unique_ptr<FileWriteStream> aWriteStream;
    if (pWriteFiles) {
      const std::string aOutputPath = pFileSystem.JoinPath(pDestinationDirectory, aRelativePath);
      const std::string aParentPath = pFileSystem.ParentPath(aOutputPath);
      if (!aParentPath.empty() && aParentPath != aOutputPath && !pFileSystem.EnsureDirectory(aParentPath)) {
        pErrorMessage = "could not prepare output directory for " + aRelativePath;
        return false;
      }
      aWriteStream = pFileSystem.OpenWriteStream(aOutputPath);
      if (aWriteStream == nullptr || !aWriteStream->IsReady()) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
    }

    std::uint64_t aRecoveredFileBytes = 0;
    bool aRestarted = false;
    while (aContentLength > 0) {
      const std::size_t aChunkLength = static_cast<std::size_t>(
          std::min<std::uint64_t>(aContentLength, static_cast<std::uint64_t>(kFixedIoChunkLength)));
      aBytesRead = 0;
      if (!aReader.Read(aWorkspace->mChunk, aChunkLength, aBytesRead)) {
        if (aBytesRead > 0) {
          if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aBytesRead)) {
            pErrorMessage = "could not write " + aRelativePath;
            return false;
          }
          aRecoveredFileBytes += aBytesRead;
          aContentLength -= aBytesRead;
        }
        aLogArchiveProgress();
        if (aReader.HasRecoverableFailure()) {
          if (!aHandleRecoverFailure(std::string(), &aWriteStream, aRelativePath, aRecoveredFileBytes)) {
            return false;
          }
          aRestarted = true;
          break;
        }
        pErrorMessage = aReader.ErrorMessage();
        if (pErrorMessage.rfind("UNP_", 0) != 0) {
          pErrorMessage = "UNP_FDL_FENCE: " + pErrorMessage;
        }
        return false;
      }

      aLogArchiveProgress();
      if (aWriteStream != nullptr && !aWriteStream->Write(aWorkspace->mChunk, aChunkLength)) {
        pErrorMessage = "could not write " + aRelativePath;
        return false;
      }
      aContentLength -= aChunkLength;
      aRecoveredFileBytes += aChunkLength;
    }

    if (aRestarted) {
      if (aReader.RecoverStoppedAtEnd()) {
        break;
      }
      continue;
    }

    if (aWriteStream != nullptr && !aWriteStream->Close()) {
      pErrorMessage = "could not finalize " + aRelativePath;
      return false;
    }
    pSummary.mProcessedBytes += aRecoveredFileBytes;
    ++pSummary.mFilesProcessed;
  }

  aLogArchiveProgress();
  aReader.FlushRecoverLog();
  return true;
}

}  // namespace

// Role: Shared decode discovery and decode-core helpers.
bool DiscoverArchiveSetForDecode(FileSystem& pFileSystem,
                                 Logger& pLogger,
                                 const RuntimeSettings& pSettings,
                                 const std::string& pJobName,
                                 const std::string& pArchivePathOrDirectory,
                                 const std::string& pSelectedArchiveFilePath,
                                 std::vector<ArchiveHeaderRecord>& pArchives,
                                 std::size_t& pSelectedArchiveOffset,
                                 bool& pSelectedArchiveOffsetValid,
                                 std::string& pErrorMessage) {
  ArchiveDiscoveryResult aDiscovery;
  if (!DiscoverArchiveSet(pFileSystem,
                          pLogger,
                          pSettings,
                          pJobName,
                          pArchivePathOrDirectory,
                          pSelectedArchiveFilePath,
                          aDiscovery,
                          pErrorMessage)) {
    return false;
  }

  pArchives = std::move(aDiscovery.mArchives);
  pSelectedArchiveOffset = aDiscovery.mSelectedArchiveOffset;
  pSelectedArchiveOffsetValid = aDiscovery.mSelectedArchiveOffsetValid;
  return true;
}

PreflightResult RoleDecodeCheckPathAndDestination(FileSystem& pFileSystem,
                                              const std::string& pOperationName,
                                              const std::string& pArchivePathOrDirectory,
                                              const std::string& pDestinationDirectory) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, std::string());
  if (!aInputSelection.has_value()) {
    return MakeInvalid(pOperationName + " Failed",
                       RoleTextToLowerAscii(pOperationName) + " failed: archive path does not exist.");
  }
  if (pFileSystem.DirectoryHasEntries(pDestinationDirectory)) {
    return MakeNeedsDestination(pOperationName + " Destination",
                                pOperationName + " destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

bool RoleDecodeDiscoverArchiveWindow(FileSystem& pFileSystem,
                                 Logger& pLogger,
                                 const RuntimeSettings& pSettings,
                                 const std::string& pJobName,
                                 const std::string& pArchivePathOrDirectory,
                                 std::vector<ArchiveHeaderRecord>& pDecodeArchives,
                                 std::string& pErrorMessage) {
  std::vector<ArchiveHeaderRecord> aDiscoveredArchives;
  std::size_t aSelectedArchiveOffset = 0;
  bool aSelectedArchiveOffsetValid = false;
  if (!DiscoverArchiveSetForDecode(pFileSystem,
                                   pLogger,
                                   pSettings,
                                   pJobName,
                                   pArchivePathOrDirectory,
                                   std::string(),
                                   aDiscoveredArchives,
                                   aSelectedArchiveOffset,
                                   aSelectedArchiveOffsetValid,
                                   pErrorMessage)) {
    return false;
  }

  const std::size_t aStartArchiveOffset = aSelectedArchiveOffsetValid ? aSelectedArchiveOffset : 0;
  if (aStartArchiveOffset >= aDiscoveredArchives.size()) {
    pErrorMessage = pJobName + " failed: selected start is outside the discovered archive set.";
    return false;
  }

  pDecodeArchives.assign(aDiscoveredArchives.begin() + aStartArchiveOffset,
                         aDiscoveredArchives.end());
  if (pDecodeArchives.empty()) {
    pErrorMessage = pJobName + " failed: no archives were available from the selected start.";
    return false;
  }

  pLogger.LogStatus("Selected archive set start = " + std::to_string(aStartArchiveOffset) + ", count = " +
                    std::to_string(pDecodeArchives.size()) + ".");
  return true;
}

bool ResolveRecoveryStartPositionForDecode(FileSystem& pFileSystem,
                                           const Crypt& pCrypt,
                                           const std::vector<ArchiveHeaderRecord>& pArchives,
                                           bool pUseEncryption,
                                           std::size_t& pStartArchiveOffset,
                                           std::size_t& pStartPhysicalOffset,
                                           std::string& pErrorMessage) {
  return ResolveRecoveryStartPosition(pFileSystem,
                                      pCrypt,
                                      pArchives,
                                      pUseEncryption,
                                      pStartArchiveOffset,
                                      pStartPhysicalOffset,
                                      pErrorMessage);
}

bool DecodeArchiveSetForUnbundle(FileSystem& pFileSystem,
                                 const Crypt& pCrypt,
                                 Logger& pLogger,
                                 const std::vector<ArchiveHeaderRecord>& pArchives,
                                 std::size_t pStartPhysicalOffset,
                                 const std::string& pDestinationDirectory,
                                 bool pWriteFiles,
                                 bool pUseEncryption,
                                 std::uint64_t& pProcessedBytes,
                                 std::size_t& pFilesProcessed,
                                 std::size_t& pEmptyDirectoriesProcessed,
                                 std::string& pErrorMessage) {
  DecodeSummary aSummary;
  if (!DecodeArchiveSetUnbundle(pFileSystem,
                                pCrypt,
                                pLogger,
                                pArchives,
                                pStartPhysicalOffset,
                                pDestinationDirectory,
                                pWriteFiles,
                                pUseEncryption,
                                aSummary,
                                pErrorMessage)) {
    return false;
  }

  pProcessedBytes = aSummary.mProcessedBytes;
  pFilesProcessed = aSummary.mFilesProcessed;
  pEmptyDirectoriesProcessed = aSummary.mEmptyDirectoriesProcessed;
  return true;
}

bool DecodeArchiveSetForRecover(FileSystem& pFileSystem,
                                const Crypt& pCrypt,
                                Logger& pLogger,
                                const std::vector<ArchiveHeaderRecord>& pArchives,
                                std::size_t pStartPhysicalOffset,
                                const std::string& pDestinationDirectory,
                                bool pWriteFiles,
                                bool pUseEncryption,
                                std::uint64_t& pProcessedBytes,
                                std::size_t& pFilesProcessed,
                                std::size_t& pEmptyDirectoriesProcessed,
                                std::string& pErrorMessage) {
  DecodeSummary aSummary;
  if (!DecodeArchiveSetRecover(pFileSystem,
                               pCrypt,
                               pLogger,
                               pArchives,
                               pStartPhysicalOffset,
                               pDestinationDirectory,
                               pWriteFiles,
                               pUseEncryption,
                               aSummary,
                               pErrorMessage)) {
    return false;
  }

  pProcessedBytes = aSummary.mProcessedBytes;
  pFilesProcessed = aSummary.mFilesProcessed;
  pEmptyDirectoriesProcessed = aSummary.mEmptyDirectoriesProcessed;
  return true;
}

}  // namespace peanutbutter::detail

namespace peanutbutter::detail {
namespace {

PreflightResult RoleUnbundleCheck(FileSystem& pFileSystem, const UnbundleRequest& pRequest);
OperationResult RoleUnbundleRunDecode(FileSystem& pFileSystem,
                                         const Crypt& pCrypt,
                                         Logger& pLogger,
                                         const RuntimeSettings& pSettings,
                                         const std::string& pArchivePathOrDirectory,
                                         const std::string& pDestinationPathOrDirectory,
                                         bool pUseEncryption,
                                         DestinationAction pAction);
OperationResult RoleUnbundleRun(FileSystem& pFileSystem,
                                   const Crypt& pCrypt,
                                   Logger& pLogger,
                                   const RuntimeSettings& pSettings,
                                   const UnbundleRequest& pRequest,
                                   DestinationAction pAction);

}  // namespace

// Role: Unbundle orchestration entry points.
PreflightResult CheckUnbundleJob(FileSystem& pFileSystem, const UnbundleRequest& pRequest) {
  return RoleUnbundleCheck(pFileSystem, pRequest);
}

OperationResult RunUnbundleDecodeJob(FileSystem& pFileSystem,
                                     const Crypt& pCrypt,
                                     Logger& pLogger,
                                     const RuntimeSettings& pSettings,
                                     const std::string& pArchivePathOrDirectory,
                                     const std::string& pDestinationPathOrDirectory,
                                     bool pUseEncryption,
                                     DestinationAction pAction) {
  return RoleUnbundleRunDecode(
      pFileSystem, pCrypt, pLogger, pSettings, pArchivePathOrDirectory, pDestinationPathOrDirectory, pUseEncryption, pAction);
}

OperationResult RunUnbundleJob(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               Logger& pLogger,
                               const RuntimeSettings& pSettings,
                               const UnbundleRequest& pRequest,
                               DestinationAction pAction) {
  return RoleUnbundleRun(pFileSystem, pCrypt, pLogger, pSettings, pRequest, pAction);
}

namespace {

PreflightResult RoleUnbundleCheck(FileSystem& pFileSystem, const UnbundleRequest& pRequest) {
  return RoleDecodeCheckPathAndDestination(
      pFileSystem, "Unbundle", pRequest.mArchiveDirectory, ResolveDirectoryTargetPath(pFileSystem, pRequest.mDestinationDirectory));
}

OperationResult RoleUnbundleRunDecode(FileSystem& pFileSystem,
                                         const Crypt& pCrypt,
                                         Logger& pLogger,
                                         const RuntimeSettings& pSettings,
                                         const std::string& pArchivePathOrDirectory,
                                         const std::string& pDestinationPathOrDirectory,
                                         bool pUseEncryption,
                                         DestinationAction pAction) {
  const std::string aDestinationDirectory = ResolveDirectoryTargetPath(pFileSystem, pDestinationPathOrDirectory);

  // 1) Validate source/destination paths and early-cancel if destination is occupied and user declined.
  const PreflightResult aPreflight =
      RoleDecodeCheckPathAndDestination(pFileSystem, "Unbundle", pArchivePathOrDirectory, aDestinationDirectory);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Unbundle Canceled", "Unbundle canceled."};
  }

  pLogger.LogStatus("Unbundle job starting...");

  // 2) Discover and normalize archive window exactly once.
  std::string aErrorMessage;
  std::vector<ArchiveHeaderRecord> aDecodeArchives;
  if (!RoleDecodeDiscoverArchiveWindow(
          pFileSystem, pLogger, pSettings, "Unbundle", pArchivePathOrDirectory, aDecodeArchives, aErrorMessage)) {
    return MakeFailure(pLogger, "Unbundle Failed", aErrorMessage);
  }

  // 3) Prepare destination once source archives are known-good.
  if (!ApplyDestinationAction(pFileSystem, aDestinationDirectory, pAction)) {
    return MakeFailure(pLogger, "Unbundle Failed", "Unbundle failed: could not prepare destination directory.");
  }

  // 4) Decode/write records.
  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aEmptyDirectoriesProcessed = 0;
  if (!DecodeArchiveSetForUnbundle(pFileSystem,
                                   pCrypt,
                                   pLogger,
                                   aDecodeArchives,
                                   0,
                                   aDestinationDirectory,
                                   true,
                                   pUseEncryption,
                                   aProcessedBytes,
                                   aFilesProcessed,
                                   aEmptyDirectoriesProcessed,
                                   aErrorMessage)) {
    return MakeFailure(pLogger, "Unbundle Failed", "Unbundle failed: " + aErrorMessage);
  }

  if (aEmptyDirectoriesProcessed > 0) {
    pLogger.LogStatus("Successfully unpacked " + std::to_string(aEmptyDirectoriesProcessed) + " empty directories.");
  }

  pLogger.LogStatus("Unbundled archive " + std::to_string(aDecodeArchives.size()) + " / " +
                    std::to_string(aDecodeArchives.size()) + ", " +
                    std::to_string(aFilesProcessed) + " files written, " +
                    FormatBytes(aProcessedBytes) + " / " + FormatBytes(aProcessedBytes) + ".");
  pLogger.LogStatus("Unbundle job complete.");
  return {true, "Unbundle Complete", "Unbundle completed successfully."};
}

OperationResult RoleUnbundleRun(FileSystem& pFileSystem,
                                   const Crypt& pCrypt,
                                   Logger& pLogger,
                                   const RuntimeSettings& pSettings,
                                   const UnbundleRequest& pRequest,
                                   DestinationAction pAction) {
  return RoleUnbundleRunDecode(pFileSystem,
                                  pCrypt,
                                  pLogger,
                                  pSettings,
                                  pRequest.mArchiveDirectory,
                                  pRequest.mDestinationDirectory,
                                  pRequest.mUseEncryption,
                                  pAction);
}

}  // namespace
}  // namespace peanutbutter::detail

namespace peanutbutter::detail {
namespace {

std::string ResolveRecoverArchivePath(const RecoverRequest& pRequest) {
  if (!pRequest.mRecoveryStartFilePath.empty()) {
    return pRequest.mRecoveryStartFilePath;
  }
  return pRequest.mArchiveDirectory;
}

void LogRecoverStartProbe(Logger& pLogger,
                          bool pProbeMatched,
                          std::size_t pRecoveredArchiveOffset,
                          std::size_t pResolvedPhysicalOffset,
                          const std::string& pProbeErrorMessage) {
  if (pProbeMatched) {
    pLogger.LogStatus("Recover start probe located a candidate record boundary at archive offset " +
                      std::to_string(pRecoveredArchiveOffset) + ", byte " +
                      FormatBytes(pResolvedPhysicalOffset) +
                      "; recovery walk will still process the full selected archive range.");
    return;
  }

  if (!pProbeErrorMessage.empty()) {
    pLogger.LogStatus("Recover start probe did not locate a valid boundary (" + pProbeErrorMessage +
                      "); recovery walk will continue from the selected archive start.");
  }
}

PreflightResult RoleRecoverCheck(FileSystem& pFileSystem, const RecoverRequest& pRequest);
OperationResult RoleRecoverRunDecode(FileSystem& pFileSystem,
                                        const Crypt& pCrypt,
                                        Logger& pLogger,
                                        const RuntimeSettings& pSettings,
                                        const std::string& pArchivePathOrDirectory,
                                        const std::string& pDestinationPathOrDirectory,
                                        bool pUseEncryption,
                                        DestinationAction pAction);
OperationResult RoleRecoverRun(FileSystem& pFileSystem,
                                  const Crypt& pCrypt,
                                  Logger& pLogger,
                                  const RuntimeSettings& pSettings,
                                  const RecoverRequest& pRequest,
                                  DestinationAction pAction);

}  // namespace

// Role: Recover orchestration entry points.
PreflightResult CheckRecoverJob(FileSystem& pFileSystem, const RecoverRequest& pRequest) {
  return RoleRecoverCheck(pFileSystem, pRequest);
}

OperationResult RunRecoverDecodeJob(FileSystem& pFileSystem,
                                    const Crypt& pCrypt,
                                    Logger& pLogger,
                                    const RuntimeSettings& pSettings,
                                    const std::string& pArchivePathOrDirectory,
                                    const std::string& pDestinationPathOrDirectory,
                                    bool pUseEncryption,
                                    DestinationAction pAction) {
  return RoleRecoverRunDecode(
      pFileSystem, pCrypt, pLogger, pSettings, pArchivePathOrDirectory, pDestinationPathOrDirectory, pUseEncryption, pAction);
}

OperationResult RunRecoverJob(FileSystem& pFileSystem,
                              const Crypt& pCrypt,
                              Logger& pLogger,
                              const RuntimeSettings& pSettings,
                              const RecoverRequest& pRequest,
                              DestinationAction pAction) {
  return RoleRecoverRun(pFileSystem, pCrypt, pLogger, pSettings, pRequest, pAction);
}

namespace {

PreflightResult RoleRecoverCheck(FileSystem& pFileSystem, const RecoverRequest& pRequest) {
  if (!pRequest.mRecoveryStartFilePath.empty() &&
      !pFileSystem.IsFile(pRequest.mRecoveryStartFilePath) &&
      !pFileSystem.IsDirectory(pRequest.mRecoveryStartFilePath)) {
    return MakeInvalid("Recover Failed", "Recover failed: recovery start path does not exist.");
  }
  const std::string aArchivePathOrDirectory = ResolveRecoverArchivePath(pRequest);
  return RoleDecodeCheckPathAndDestination(
      pFileSystem, "Recover", aArchivePathOrDirectory, ResolveDirectoryTargetPath(pFileSystem, pRequest.mDestinationDirectory));
}

OperationResult RoleRecoverRunDecode(FileSystem& pFileSystem,
                                        const Crypt& pCrypt,
                                        Logger& pLogger,
                                        const RuntimeSettings& pSettings,
                                        const std::string& pArchivePathOrDirectory,
                                        const std::string& pDestinationPathOrDirectory,
                                        bool pUseEncryption,
                                        DestinationAction pAction) {
  const std::string aDestinationDirectory = ResolveDirectoryTargetPath(pFileSystem, pDestinationPathOrDirectory);

  // 1) Validate paths and destination policy.
  const PreflightResult aPreflight =
      RoleDecodeCheckPathAndDestination(pFileSystem, "Recover", pArchivePathOrDirectory, aDestinationDirectory);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Recover Canceled", "Recover canceled."};
  }

  pLogger.LogStatus("Recover job starting...");

  // 2) Discover and normalize archive window exactly once.
  std::string aErrorMessage;
  std::vector<ArchiveHeaderRecord> aDecodeArchives;
  if (!RoleDecodeDiscoverArchiveWindow(
          pFileSystem, pLogger, pSettings, "Recover", pArchivePathOrDirectory, aDecodeArchives, aErrorMessage)) {
    return MakeFailure(pLogger, "Recover Failed", aErrorMessage);
  }

  // 3) Prepare destination after source archives are known-good.
  if (!ApplyDestinationAction(pFileSystem, aDestinationDirectory, pAction)) {
    return MakeFailure(pLogger, "Recover Failed", "Recover failed: could not prepare destination directory.");
  }

  // 4) Probe for diagnostics, then recover from the selected start range.
  std::size_t aRecoveredArchiveOffset = 0;
  std::size_t aResolvedPhysicalOffset = 0;
  const bool aProbeMatched = ResolveRecoveryStartPositionForDecode(pFileSystem,
                                                                    pCrypt,
                                                                    aDecodeArchives,
                                                                    pUseEncryption,
                                                                    aRecoveredArchiveOffset,
                                                                    aResolvedPhysicalOffset,
                                                                    aErrorMessage);
  LogRecoverStartProbe(pLogger, aProbeMatched, aRecoveredArchiveOffset, aResolvedPhysicalOffset, aErrorMessage);

  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aEmptyDirectoriesProcessed = 0;
  if (!DecodeArchiveSetForRecover(pFileSystem,
                                  pCrypt,
                                  pLogger,
                                  aDecodeArchives,
                                  0,
                                  aDestinationDirectory,
                                  true,
                                  pUseEncryption,
                                  aProcessedBytes,
                                  aFilesProcessed,
                                  aEmptyDirectoriesProcessed,
                                  aErrorMessage)) {
    return MakeFailure(pLogger, "Recover Failed", "Recover failed: " + aErrorMessage);
  }

  if (aEmptyDirectoriesProcessed > 0) {
    pLogger.LogStatus("Successfully unpacked " + std::to_string(aEmptyDirectoriesProcessed) + " empty directories.");
  }

  pLogger.LogStatus("Recovered archive " + std::to_string(aDecodeArchives.size()) + " / " +
                    std::to_string(aDecodeArchives.size()) + ", " +
                    std::to_string(aFilesProcessed) + " files written, " +
                    FormatBytes(aProcessedBytes) + " / " + FormatBytes(aProcessedBytes) + ".");
  pLogger.LogStatus("Recover job complete.");
  return {true, "Recover Complete", "Recover completed successfully."};
}

OperationResult RoleRecoverRun(FileSystem& pFileSystem,
                                  const Crypt& pCrypt,
                                  Logger& pLogger,
                                  const RuntimeSettings& pSettings,
                                  const RecoverRequest& pRequest,
                                  DestinationAction pAction) {
  return RoleRecoverRunDecode(pFileSystem,
                                 pCrypt,
                                 pLogger,
                                 pSettings,
                                 ResolveRecoverArchivePath(pRequest),
                                 pRequest.mDestinationDirectory,
                                 pRequest.mUseEncryption,
                                 pAction);
}

}  // namespace
}  // namespace peanutbutter::detail



namespace peanutbutter {
namespace {

struct ComparedPath {
  std::string mPath;
  bool mIsHidden = false;
};

struct ValidationReport {
  std::vector<std::string> mOnlyInSourceVisible;
  std::vector<std::string> mOnlyInDestinationVisible;
  std::vector<std::string> mDataMismatchVisible;
  std::vector<std::string> mEmptyDirectoriesOnlyInSourceVisible;
  std::vector<std::string> mEmptyDirectoriesOnlyInDestinationVisible;
  std::vector<std::string> mOnlyInSourceHidden;
  std::vector<std::string> mOnlyInDestinationHidden;
  std::vector<std::string> mDataMismatchHidden;
  std::vector<std::string> mEmptyDirectoriesOnlyInSourceHidden;
  std::vector<std::string> mEmptyDirectoriesOnlyInDestinationHidden;
};

struct CompareProgress {
  std::uint64_t mSourceBytesProcessed = 0;
  std::uint64_t mDestinationBytesProcessed = 0;
  std::size_t mFilesScanned = 0;
};

constexpr std::size_t kValidationListLimit = 200;
constexpr std::uint64_t kValidationProgressByteStep = 500ull * 1024ull * 1024ull;

bool IsHiddenComponent(const std::string& pRelativePath) {
  std::size_t aStart = 0;
  while (aStart < pRelativePath.size()) {
    std::size_t aEnd = pRelativePath.find_first_of("/\\", aStart);
    if (aEnd == std::string::npos) {
      aEnd = pRelativePath.size();
    }
    if (aEnd > aStart) {
      const std::string_view aPart(pRelativePath.data() + aStart, aEnd - aStart);
      if (aPart != "." && aPart != ".." && !aPart.empty() && aPart.front() == '.') {
        return true;
      }
    }
    aStart = aEnd + 1;
  }
  return false;
}

void AppendGroupedLine(std::ostringstream& pReport, const std::string& pPrefix, const std::string& pPath) {
  pReport << pPrefix << pPath << "\n";
}

void WriteLimitedGroupedSection(std::ostringstream& pReport,
                                const std::string& pLabel,
                                const std::string& pPrefix,
                                const std::vector<std::string>& pPaths) {
  pReport << pLabel << " = " << pPaths.size() << "\n";
  const std::size_t aLimit = std::min(kValidationListLimit, pPaths.size());
  for (std::size_t aIndex = 0; aIndex < aLimit; ++aIndex) {
    AppendGroupedLine(pReport, pPrefix, pPaths[aIndex]);
  }
  if (pPaths.size() > aLimit) {
    pReport << "... truncated, " << (pPaths.size() - aLimit) << " more entries not shown\n";
  }
}

void ReportPathSample(Logger& pLogger,
                      const std::string& pHeader,
                      const std::vector<std::string>& pPaths) {
  if (pPaths.empty()) {
    return;
  }

  const std::size_t aLimit = std::min(kValidationListLimit, pPaths.size());
  pLogger.LogStatus(pHeader + " (" + std::to_string(aLimit) + " of " + std::to_string(pPaths.size()) + ")");
  for (std::size_t aIndex = 0; aIndex < aLimit; ++aIndex) {
    pLogger.LogStatus("  " + pPaths[aIndex]);
  }
  if (pPaths.size() > aLimit) {
    pLogger.LogStatus("  ... " + std::to_string(pPaths.size() - aLimit) + " more");
  }
}

bool FilesMatchByteForByte(const FileSystem& pFileSystem,
                           const DirectoryEntry& pLeftEntry,
                           const DirectoryEntry& pRightEntry,
                           std::uint64_t* pSourceBytesProcessed,
                           std::uint64_t* pDestinationBytesProcessed) {
  std::unique_ptr<FileReadStream> aLeftStream = pFileSystem.OpenReadStream(pLeftEntry.mPath);
  std::unique_ptr<FileReadStream> aRightStream = pFileSystem.OpenReadStream(pRightEntry.mPath);
  if (aLeftStream == nullptr || aRightStream == nullptr || !aLeftStream->IsReady() || !aRightStream->IsReady()) {
    return false;
  }
  if (aLeftStream->GetLength() != aRightStream->GetLength()) {
    return false;
  }

  unsigned char aLeftBuffer[detail::kFixedIoChunkLength] = {};
  unsigned char aRightBuffer[detail::kFixedIoChunkLength] = {};
  const std::size_t aTotalLength = aLeftStream->GetLength();
  for (std::size_t aOffset = 0; aOffset < aTotalLength; aOffset += detail::kFixedIoChunkLength) {
    const std::size_t aChunkLength = std::min(detail::kFixedIoChunkLength, aTotalLength - aOffset);
    if (!aLeftStream->Read(aOffset, aLeftBuffer, aChunkLength) ||
        !aRightStream->Read(aOffset, aRightBuffer, aChunkLength)) {
      return false;
    }
    if (pSourceBytesProcessed != nullptr) {
      *pSourceBytesProcessed += static_cast<std::uint64_t>(aChunkLength);
    }
    if (pDestinationBytesProcessed != nullptr) {
      *pDestinationBytesProcessed += static_cast<std::uint64_t>(aChunkLength);
    }
    if (std::memcmp(aLeftBuffer, aRightBuffer, aChunkLength) != 0) {
      return false;
    }
  }

  return true;
}

void LogValidationProgress(Logger& pLogger,
                           const CompareProgress& pProgress,
                           std::uint64_t& pLastLoggedBytes) {
  const std::uint64_t aTotalBytes = pProgress.mSourceBytesProcessed + pProgress.mDestinationBytesProcessed;
  if (aTotalBytes < pLastLoggedBytes + kValidationProgressByteStep) {
    return;
  }
  pLastLoggedBytes = aTotalBytes;
  const std::string aByteSummary = detail::FormatBytes(pProgress.mSourceBytesProcessed) +
                                   " source, " + detail::FormatBytes(pProgress.mDestinationBytesProcessed) +
                                   " destination";
  pLogger.LogStatus("Scanned " + aByteSummary + ", " + std::to_string(pProgress.mFilesScanned) +
                    " files checked so far.");
}

std::string BuildValidationReportText(const ValidationReport& pReport) {
  std::ostringstream aReport;
  WriteLimitedGroupedSection(aReport, "Visible-only source extras", "SRC ", pReport.mOnlyInSourceVisible);
  WriteLimitedGroupedSection(aReport, "Visible-only destination extras", "DST ", pReport.mOnlyInDestinationVisible);
  WriteLimitedGroupedSection(aReport, "Visible byte mismatches", "MISMATCH ", pReport.mDataMismatchVisible);
  WriteLimitedGroupedSection(aReport,
                             "Visible empty-directory source extras",
                             "SRC_DIR ",
                             pReport.mEmptyDirectoriesOnlyInSourceVisible);
  WriteLimitedGroupedSection(aReport,
                             "Visible empty-directory destination extras",
                             "DST_DIR ",
                             pReport.mEmptyDirectoriesOnlyInDestinationVisible);
  WriteLimitedGroupedSection(aReport, "Hidden-only source extras", "SRC_HIDDEN ", pReport.mOnlyInSourceHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden-only destination extras",
                             "DST_HIDDEN ",
                             pReport.mOnlyInDestinationHidden);
  WriteLimitedGroupedSection(aReport, "Hidden byte mismatches", "MISMATCH_HIDDEN ", pReport.mDataMismatchHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden empty-directory source extras",
                             "SRC_DIR_HIDDEN ",
                             pReport.mEmptyDirectoriesOnlyInSourceHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden empty-directory destination extras",
                             "DST_DIR_HIDDEN ",
                             pReport.mEmptyDirectoriesOnlyInDestinationHidden);
  return aReport.str();
}

}  // namespace

namespace detail {

// Role: Validate (sanity/tree-compare) entry points.
PreflightResult CheckValidateJob(const FileSystem& pFileSystem, const ValidateRequest& pRequest) {
  if (!pFileSystem.IsDirectory(pRequest.mLeftDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: source directory does not exist. Resolved source directory = " +
                           pRequest.mLeftDirectory);
  }
  if (!pFileSystem.IsDirectory(pRequest.mRightDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: destination directory does not exist. Resolved destination directory = " +
                           pRequest.mRightDirectory);
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult RunValidateJob(FileSystem& pFileSystem, Logger& pLogger, const ValidateRequest& pRequest) {
  const PreflightResult aPreflight = CheckValidateJob(pFileSystem, pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }

  pLogger.LogStatus("Sanity job starting...");
  const std::vector<DirectoryEntry> aLeftEntries = pFileSystem.ListFilesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightEntries = pFileSystem.ListFilesRecursive(pRequest.mRightDirectory);
  const std::vector<DirectoryEntry> aLeftDirectories = pFileSystem.ListDirectoriesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightDirectories = pFileSystem.ListDirectoriesRecursive(pRequest.mRightDirectory);

  std::map<std::string, ComparedPath> aLeftFiles;
  std::map<std::string, ComparedPath> aRightFiles;
  std::map<std::string, ComparedPath> aLeftEmptyDirectories;
  std::map<std::string, ComparedPath> aRightEmptyDirectories;
  for (const DirectoryEntry& aEntry : aLeftEntries) {
    if (!aEntry.mIsDirectory && !aEntry.mRelativePath.empty()) {
      aLeftFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aRightEntries) {
    if (!aEntry.mIsDirectory && !aEntry.mRelativePath.empty()) {
      aRightFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aLeftDirectories) {
    if (!aEntry.mRelativePath.empty() && !pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      aLeftEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aRightDirectories) {
    if (!aEntry.mRelativePath.empty() && !pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      aRightEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }

  ValidationReport aReport;
  CompareProgress aProgress;
  std::uint64_t aLastLoggedBytes = 0;

  for (const auto& aPair : aLeftFiles) {
    const auto aRightIt = aRightFiles.find(aPair.first);
    if (aRightIt == aRightFiles.end()) {
      (aPair.second.mIsHidden ? aReport.mOnlyInSourceHidden : aReport.mOnlyInSourceVisible).push_back(aPair.first);
      continue;
    }
    ++aProgress.mFilesScanned;
    if (!FilesMatchByteForByte(pFileSystem,
                               {aPair.second.mPath, aPair.first, false},
                               {aRightIt->second.mPath, aPair.first, false},
                               &aProgress.mSourceBytesProcessed,
                               &aProgress.mDestinationBytesProcessed)) {
      (aPair.second.mIsHidden ? aReport.mDataMismatchHidden : aReport.mDataMismatchVisible).push_back(aPair.first);
    }
    LogValidationProgress(pLogger, aProgress, aLastLoggedBytes);
  }

  for (const auto& aPair : aRightFiles) {
    if (aLeftFiles.find(aPair.first) == aLeftFiles.end()) {
      (aPair.second.mIsHidden ? aReport.mOnlyInDestinationHidden : aReport.mOnlyInDestinationVisible)
          .push_back(aPair.first);
    }
  }

  for (const auto& aPair : aLeftEmptyDirectories) {
    if (aRightEmptyDirectories.find(aPair.first) == aRightEmptyDirectories.end()) {
      (aPair.second.mIsHidden ? aReport.mEmptyDirectoriesOnlyInSourceHidden
                              : aReport.mEmptyDirectoriesOnlyInSourceVisible)
          .push_back(aPair.first);
    }
  }
  for (const auto& aPair : aRightEmptyDirectories) {
    if (aLeftEmptyDirectories.find(aPair.first) == aLeftEmptyDirectories.end()) {
      (aPair.second.mIsHidden ? aReport.mEmptyDirectoriesOnlyInDestinationHidden
                              : aReport.mEmptyDirectoriesOnlyInDestinationVisible)
          .push_back(aPair.first);
    }
  }

  const std::string aOutputPath =
      pFileSystem.JoinPath(pFileSystem.CurrentWorkingDirectory(), "tree_validation_report_generated.txt");
  if (pFileSystem.WriteTextFile(aOutputPath, BuildValidationReportText(aReport))) {
    pLogger.LogStatus("Generated " + aOutputPath);
  } else {
    pLogger.LogStatus("[WARN] Could not write tree_validation_report_generated.txt to " + aOutputPath + ".");
  }

  pLogger.LogStatus("Hidden-only source extras: " + std::to_string(aReport.mOnlyInSourceHidden.size()));
  pLogger.LogStatus("Hidden-only destination extras: " + std::to_string(aReport.mOnlyInDestinationHidden.size()));
  pLogger.LogStatus("Hidden byte mismatches: " + std::to_string(aReport.mDataMismatchHidden.size()));
  pLogger.LogStatus("Hidden empty-directory source extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInSourceHidden.size()));
  pLogger.LogStatus("Hidden empty-directory destination extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationHidden.size()));
  pLogger.LogStatus("Visible-only source extras: " + std::to_string(aReport.mOnlyInSourceVisible.size()));
  pLogger.LogStatus("Visible-only destination extras: " + std::to_string(aReport.mOnlyInDestinationVisible.size()));
  pLogger.LogStatus("Visible byte mismatches: " + std::to_string(aReport.mDataMismatchVisible.size()));
  pLogger.LogStatus("Visible empty-directory source extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInSourceVisible.size()));
  pLogger.LogStatus("Visible empty-directory destination extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationVisible.size()));

  ReportPathSample(pLogger, "Visible-only source sample", aReport.mOnlyInSourceVisible);
  ReportPathSample(pLogger, "Visible-only destination sample", aReport.mOnlyInDestinationVisible);
  ReportPathSample(pLogger, "Visible mismatch sample", aReport.mDataMismatchVisible);

  pLogger.LogStatus("Scanned " + std::to_string(aProgress.mFilesScanned) + " files " +
                    std::to_string(aLeftDirectories.size() + aRightDirectories.size()) + " directories.");

  const bool aSucceeded = aReport.mOnlyInSourceVisible.empty() && aReport.mOnlyInDestinationVisible.empty() &&
                          aReport.mDataMismatchVisible.empty() &&
                          aReport.mEmptyDirectoriesOnlyInSourceVisible.empty() &&
                          aReport.mEmptyDirectoriesOnlyInDestinationVisible.empty();
  pLogger.LogStatus(aSucceeded ? "[OK] source and destination trees are byte-for-byte equal."
                               : "[DIFF] source and destination trees diverged.");
  pLogger.LogStatus("Sanity job complete.");
  return {true, "Sanity Complete", "Sanity completed successfully."};
}

}  // namespace detail
}  // namespace peanutbutter
