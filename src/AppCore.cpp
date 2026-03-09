#include "AppCore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string_view>
#include <utility>

#include "AppCore_MemoryOwners.hpp"
#include "AppCore_Stackers.hpp"
#include "Memory/HeapBuffer.hpp"

namespace peanutbutter {

namespace {

std::string FormatBytes(std::uint64_t pBytes) {
  std::ostringstream aStream;
  constexpr std::uint64_t kKiB = 1024;
  constexpr std::uint64_t kMiB = 1024 * 1024;
  if (pBytes >= kMiB) {
    aStream << (pBytes / kMiB) << " MiB";
  } else if (pBytes >= kKiB) {
    aStream << (pBytes / kKiB) << " KiB";
  } else {
    aStream << pBytes << " B";
  }
  return aStream.str();
}

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
constexpr std::size_t kL3Length = peanutbutter::SB_L3_LENGTH;

bool IsHiddenComponent(const std::string& pRelativePath) {
  if (pRelativePath.empty()) {
    return false;
  }

  for (const auto& aPart : std::filesystem::path(pRelativePath)) {
    const std::string aPartText = aPart.string();
    if (!aPartText.empty() && aPartText != "." && aPartText != ".." && aPartText.front() == '.') {
      return true;
    }
  }

  return false;
}

void AppendGroupedLine(std::ofstream& pOutFile, const std::string& pPrefix, const std::string& pPath) {
  pOutFile << pPrefix << pPath << "\n";
}

void WriteLimitedGroupedSection(std::ofstream& pOutFile,
                               const std::string& pLabel,
                               const std::string& pPrefix,
                               const std::vector<std::string>& pPaths) {
  pOutFile << pLabel << " = " << pPaths.size() << "\n";
  const std::size_t aLimit = std::min(kValidationListLimit, pPaths.size());
  for (std::size_t aIndex = 0; aIndex < aLimit; ++aIndex) {
    AppendGroupedLine(pOutFile, pPrefix, pPaths[aIndex]);
  }
  if (pPaths.size() > aLimit) {
    pOutFile << "... truncated, " << (pPaths.size() - aLimit) << " more entries not shown\n";
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
  constexpr std::size_t kBufferLength = 64 * 1024;

  const std::unique_ptr<FileReadStream> aLeftStream = pFileSystem.OpenReadStream(pLeftEntry.mPath);
  const std::unique_ptr<FileReadStream> aRightStream = pFileSystem.OpenReadStream(pRightEntry.mPath);
  if (!aLeftStream || !aRightStream || !aLeftStream->IsReady() || !aRightStream->IsReady()) {
    return false;
  }
  if (aLeftStream->GetLength() != aRightStream->GetLength()) {
    return false;
  }

  HeapBuffer aLeftBuffer(kBufferLength);
  HeapBuffer aRightBuffer(kBufferLength);
  const std::size_t aTotalLength = aLeftStream->GetLength();
  for (std::size_t aOffset = 0; aOffset < aTotalLength; aOffset += kBufferLength) {
    const std::size_t aChunkLength = std::min(kBufferLength, aTotalLength - aOffset);
    if (!aLeftStream->Read(aOffset, aLeftBuffer.Data(), aChunkLength) ||
        !aRightStream->Read(aOffset, aRightBuffer.Data(), aChunkLength)) {
      return false;
    }
    const auto aLeftRead = static_cast<std::uint64_t>(aChunkLength);
    if (pSourceBytesProcessed != nullptr) {
      *pSourceBytesProcessed += aLeftRead;
    }
    if (pDestinationBytesProcessed != nullptr) {
      *pDestinationBytesProcessed += static_cast<std::uint64_t>(aChunkLength);
    }
    if (std::memcmp(aLeftBuffer.Data(), aRightBuffer.Data(), aChunkLength) != 0) {
      return false;
    }
  }

  return true;
}

const unsigned char* ReadArchivePayloadPageForValidation(FileReadStream& pStream,
                                                        const Crypt& pCrypt,
                                                        bool pUseEncryption,
                                                        std::size_t pPageStart,
                                                        std::size_t pPageLength,
                                                        HeapBuffer& pPageBuffer,
                                                        CryptWorkspaceL3& pWorkspace,
                                                        const unsigned char*& pPayloadBytes,
                                                        std::string* pCryptError) {
  const std::size_t aPageOffset = kArchiveHeaderLength + pPageStart;
  unsigned char* aPageSource = pUseEncryption ? pWorkspace.Source() : pPageBuffer.Data();
  if (!pStream.Read(aPageOffset, aPageSource, pPageLength)) {
    return nullptr;
  }
  pPayloadBytes = aPageSource;
  if (!pUseEncryption) {
    return pPayloadBytes;
  }

  std::fill_n(pWorkspace.Worker(), kL3Length, 0);
  std::fill_n(pWorkspace.Destination(), kL3Length, 0);
  if (!pCrypt.UnsealData(aPageSource, pWorkspace.Worker(), pWorkspace.Destination(), pPageLength, pCryptError, CryptMode::kNormal)) {
    return nullptr;
  }
  pPayloadBytes = pWorkspace.Destination();
  return pPayloadBytes;
}

std::string DescribeUnpackIntegerFailure(UnpackIntegerFailure pCode) {
  switch (pCode) {
    case UnpackIntegerFailure::kNone:
      return "no unpack failure";
    case UnpackIntegerFailure::kFileNameLengthGreaterThanMaxValidFilePathLength:
      return "file name length exceeded MAX_VALID_FILE_PATH_LENGTH";
    case UnpackIntegerFailure::kFileNameLengthGreaterThanRemainingBytesInUnpackJob:
      return "file name length exceeded remaining bytes in unpack job";
    case UnpackIntegerFailure::kFileNameLengthIsZero:
      return "file name length is zero";
    case UnpackIntegerFailure::kFileNameLengthLandsInsideRecoveryHeader:
      return "file name length landed inside a recovery header";
    case UnpackIntegerFailure::kFileNameLengthLandsInsideArchiveHeader:
      return "file name length landed inside an archive header";
    case UnpackIntegerFailure::kFileDataLengthGreaterThanRemainingBytesInUnpackJob:
      return "file data length exceeded remaining bytes in unpack job";
    case UnpackIntegerFailure::kFileDataLengthLandsInsideRecoveryHeader:
      return "file data length landed inside a recovery header";
    case UnpackIntegerFailure::kFileDataLengthLandsInsideArchiveHeader:
      return "file data length landed inside an archive header";
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceGreaterThanRemainingBytesInUnpackJob:
      return "non-first recovery next-file distance exceeded remaining bytes in unpack job";
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceIsZero:
      return "non-first recovery next-file distance is zero";
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceLandsInsideRecoveryHeader:
      return "non-first recovery next-file distance landed inside a recovery header";
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceLandsInsideArchiveHeader:
      return "non-first recovery next-file distance landed inside an archive header";
    case UnpackIntegerFailure::kRecoverySpecialFlowDistanceLandsOutsideSelectedArchive:
      return "recovery special-flow distance landed outside the selected archive";
    case UnpackIntegerFailure::kManifestFolderLengthIsZero:
      return "manifest folder length is zero";
    case UnpackIntegerFailure::kManifestFolderLengthGreaterThanRemainingBytesInUnpackJob:
      return "manifest folder length exceeded remaining bytes in unpack job";
    case UnpackIntegerFailure::kManifestFolderLengthGreaterThanMaxValidFilePathLength:
      return "manifest folder length exceeded MAX_VALID_FILE_PATH_LENGTH";
    case UnpackIntegerFailure::kDanglingArchives:
    case UnpackIntegerFailure::kDanglingBytes:
      return "Unexpected end of file found.";
    case UnpackIntegerFailure::kUnknown:
    default:
      return "unknown unpack failure";
  }
}

std::string FormatUnpackFailure(const UnpackFailureInfo& pFailure) {
  std::string aCode = "UNK_SYS_001";
  switch (pFailure.mCode) {
    case UnpackIntegerFailure::kNone:
      aCode = "UNP_SYS_000";
      break;
    case UnpackIntegerFailure::kFileNameLengthGreaterThanMaxValidFilePathLength:
    case UnpackIntegerFailure::kFileNameLengthGreaterThanRemainingBytesInUnpackJob:
    case UnpackIntegerFailure::kFileNameLengthIsZero:
    case UnpackIntegerFailure::kFileNameLengthLandsInsideRecoveryHeader:
    case UnpackIntegerFailure::kFileNameLengthLandsInsideArchiveHeader:
    case UnpackIntegerFailure::kManifestFolderLengthIsZero:
    case UnpackIntegerFailure::kManifestFolderLengthGreaterThanRemainingBytesInUnpackJob:
    case UnpackIntegerFailure::kManifestFolderLengthGreaterThanMaxValidFilePathLength:
      aCode = "UNP_FNL_FENCE";
      break;
    case UnpackIntegerFailure::kFileDataLengthGreaterThanRemainingBytesInUnpackJob:
    case UnpackIntegerFailure::kFileDataLengthLandsInsideRecoveryHeader:
    case UnpackIntegerFailure::kFileDataLengthLandsInsideArchiveHeader:
      aCode = "UNP_FDL_FENCE";
      break;
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceGreaterThanRemainingBytesInUnpackJob:
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceIsZero:
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceLandsInsideRecoveryHeader:
    case UnpackIntegerFailure::kNonFirstRecoveryNextFileDistanceLandsInsideArchiveHeader:
    case UnpackIntegerFailure::kRecoverySpecialFlowDistanceLandsOutsideSelectedArchive:
      aCode = "UNP_RHD_FENCE";
      break;
    case UnpackIntegerFailure::kDanglingArchives:
      aCode = "UNP_EOF_001";
      break;
    case UnpackIntegerFailure::kDanglingBytes:
      aCode = "UNP_EOF_002";
      break;
    case UnpackIntegerFailure::kUnknown:
    default:
      aCode = "UNK_SYS_001";
      break;
  }
  if (!pFailure.mMessage.empty()) {
    return aCode + ": " + pFailure.mMessage;
  }
  return aCode + ": " + DescribeUnpackIntegerFailure(pFailure.mCode);
}

constexpr std::size_t kRecoveryHeaderLength = peanutbutter::SB_RECOVERY_HEADER_LENGTH;
constexpr std::size_t kPayloadBytesPerL1 = peanutbutter::SB_PAYLOAD_SIZE;
constexpr std::size_t kL1Length = peanutbutter::SB_L1_LENGTH;
constexpr unsigned char kSpecialFirstRecoveryHeader[kRecoveryHeaderLength] = {
    0x76, 0x47, 0xb2, 0x1d, 0xef, 0x8e};

void WriteLe(unsigned char* pBytes, std::uint64_t pValue, std::size_t pWidth) {
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    pBytes[aIndex] = static_cast<unsigned char>((pValue >> (8 * aIndex)) & 0xFFU);
  }
}

std::uint64_t ReadLe(const unsigned char* pBytes, std::size_t pOffset, std::size_t pWidth) {
  std::uint64_t aValue = 0;
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    aValue |= static_cast<std::uint64_t>(pBytes[pOffset + aIndex]) << (8 * aIndex);
  }
  return aValue;
}

std::uint64_t ReadLeFromBytes(const unsigned char* pBytes, std::size_t pWidth) {
  std::uint64_t aValue = 0;
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    aValue |= static_cast<std::uint64_t>(pBytes[aIndex]) << (8 * aIndex);
  }
  return aValue;
}

void WriteLeToBuffer(unsigned char* pBytes, std::uint64_t pValue, std::size_t pWidth) {
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    pBytes[aIndex] = static_cast<unsigned char>((pValue >> (8 * aIndex)) & 0xFFU);
  }
}

std::uint64_t SerializedRecordLength(const SourceFileEntry& pEntry) {
  return 2ULL + static_cast<std::uint64_t>(pEntry.mRelativePath.size()) + 6ULL + pEntry.mContentLength;
}

std::size_t LogicalCapacityForPhysicalLength(std::size_t pPhysicalLength) {
  const std::size_t aFullBlocks = pPhysicalLength / kL1Length;
  const std::size_t aRemainder = pPhysicalLength % kL1Length;
  const std::size_t aPartialPayloadLength =
      aRemainder > kRecoveryHeaderLength ? aRemainder - kRecoveryHeaderLength : 0;
  return (aFullBlocks * kPayloadBytesPerL1) + aPartialPayloadLength;
}

std::size_t PhysicalOffsetForLogicalOffset(std::size_t pLogicalOffset) {
  const std::size_t aBlockIndex = pLogicalOffset / kPayloadBytesPerL1;
  const std::size_t aOffsetInBlock = pLogicalOffset % kPayloadBytesPerL1;
  return (aBlockIndex * kL1Length) + kRecoveryHeaderLength + aOffsetInBlock;
}

void WriteHeaderBuffer(const ArchiveHeader& pHeader, unsigned char* pBytes) {
  WriteLeToBuffer(pBytes + 0, kMagicHeaderBytes, 4);
  pBytes[4] = static_cast<unsigned char>(pHeader.mRecoveryEnabled ? 1 : 0);
  pBytes[5] = 0;
  WriteLeToBuffer(pBytes + 6, kMajorVersion, 4);
  WriteLeToBuffer(pBytes + 10, kMinorVersion, 4);
  WriteLeToBuffer(pBytes + 14, pHeader.mSequence, 6);
  std::copy(pHeader.mArchiveIdentifier.begin(), pHeader.mArchiveIdentifier.end(), pBytes + 20);
  WriteLeToBuffer(pBytes + 28, 0, 4);
  WriteLeToBuffer(pBytes + 32, kMagicFooterBytes, 4);
}

std::array<unsigned char, 8> GenerateArchiveIdentifier() {
#ifdef PEANUT_BUTTER_ULTIMA_TEST_BUILD
  return {0x96, 0x5a, 0x99, 0xe2, 0xf6, 0x23, 0xd4, 0x1f};
#else
  const std::uint64_t aNow =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t aRandom = (static_cast<std::uint64_t>(std::random_device{}()) << 32) ^
                                static_cast<std::uint64_t>(std::random_device{}());
  const std::uint64_t aValue = aNow ^ aRandom;

  std::array<unsigned char, 8> aIdentifier{};
  for (std::size_t aIndex = 0; aIndex < aIdentifier.size(); ++aIndex) {
    aIdentifier[aIndex] = static_cast<unsigned char>((aValue >> (8 * aIndex)) & 0xFFU);
  }
  return aIdentifier;
#endif
}

std::size_t EffectiveArchivePayloadLength(const RuntimeSettings& pSettings) {
  if (pSettings.mArchiveFileLength <= kArchiveHeaderLength) {
    return 0;
  }

  const std::size_t aRawPayloadLength = pSettings.mArchiveFileLength - kArchiveHeaderLength;
  const std::size_t aAlignedPhysicalPayloadLength = (aRawPayloadLength / kL3Length) * kL3Length;
  return LogicalCapacityForPhysicalLength(aAlignedPhysicalPayloadLength);
}

std::size_t EffectiveArchivePhysicalPayloadLength(const RuntimeSettings& pSettings) {
  if (pSettings.mArchiveFileLength <= kArchiveHeaderLength) {
    return 0;
  }

  const std::size_t aRawPayloadLength = pSettings.mArchiveFileLength - kArchiveHeaderLength;
  return (aRawPayloadLength / kL3Length) * kL3Length;
}

bool ShouldLogArchiveProgress(std::size_t pArchiveIndexOneBased,
                              std::size_t pArchiveCount,
                              std::uint64_t pProcessedBytes,
                              std::uint64_t pArchivePayloadLength,
                              std::size_t pThrottleBlockSize,
                              std::size_t pIgnoreLast,
                              std::uint64_t& pLastBucket) {
  const bool aWithinTrailingWindow =
      pArchiveCount > 0 && pArchiveIndexOneBased + pIgnoreLast > pArchiveCount;
  const std::uint64_t aBucket =
      (pArchivePayloadLength == 0 || pThrottleBlockSize == 0)
          ? 0
          : (pProcessedBytes / pArchivePayloadLength) / static_cast<std::uint64_t>(pThrottleBlockSize);
  if (!aWithinTrailingWindow && aBucket <= pLastBucket) {
    return false;
  }
  if (!aWithinTrailingWindow) {
    pLastBucket = aBucket;
  }
  return true;
}

std::string MakeArchiveName(const std::string& pSourceStem,
                            const std::string& pPrefix,
                            const std::string& pSuffix,
                            std::size_t pSequenceOneBased,
                            std::size_t pArchiveCount,
                            bool pRecoveryEnabled) {
  const std::size_t aWidth = std::max<std::size_t>(3, std::to_string(pArchiveCount).size());
  std::ostringstream aStream;
  aStream << pPrefix << pSourceStem << "_";
  aStream.width(static_cast<std::streamsize>(aWidth));
  aStream.fill('0');
  aStream << pSequenceOneBased;
  aStream << (pRecoveryEnabled ? 'R' : 'X');
  if (!pSuffix.empty() && pSuffix.front() != '.') {
    aStream << '.';
  }
  aStream << pSuffix;
  return aStream.str();
}

bool ApplyDestinationAction(FileSystem& pFileSystem,
                            const std::string& pDestinationDirectory,
                            DestinationAction pAction) {
  if (pAction == DestinationAction::Cancel) {
    return false;
  }
  if (pAction == DestinationAction::Clear) {
    return pFileSystem.ClearDirectory(pDestinationDirectory);
  }
  return pFileSystem.EnsureDirectory(pDestinationDirectory);
}

struct ArchiveDecodeCandidate {
  std::size_t mStartIndex = 0;
  std::size_t mArchiveCount = 0;
};

std::vector<ArchiveDecodeCandidate> BuildArchiveDecodeCandidates(const std::vector<ArchiveHeaderRecord>& pArchives,
                                                                Logger* pLogger) {
  std::vector<ArchiveDecodeCandidate> aCandidates;
  if (pArchives.empty()) {
    return aCandidates;
  }

  if (pLogger != nullptr) {
    pLogger->LogStatus("Discovered " + std::to_string(pArchives.size()) + " readable archives, scanning for valid archive sets.");
  }

  std::size_t aLowestSequenceIndex = 0;
  for (std::size_t aArchiveIndex = 1; aArchiveIndex < pArchives.size(); ++aArchiveIndex) {
    if (pArchives[aArchiveIndex].mHeader.mSequence < pArchives[aLowestSequenceIndex].mHeader.mSequence) {
      aLowestSequenceIndex = aArchiveIndex;
    }

    if (pLogger != nullptr && ((aArchiveIndex % 1000 == 999) || (aArchiveIndex + 1 == pArchives.size()))) {
      pLogger->LogStatus("Scanned " + std::to_string(aArchiveIndex + 1) + " of " + std::to_string(pArchives.size()) +
                         " archives...");
    }
  }

  const auto& aSelectedIdentifier = pArchives[aLowestSequenceIndex].mHeader.mArchiveIdentifier;
  std::size_t aStartIndex = aLowestSequenceIndex;
  while (aStartIndex > 0 && pArchives[aStartIndex - 1].mHeader.mArchiveIdentifier == aSelectedIdentifier) {
    --aStartIndex;
  }
  std::size_t aEndIndex = aLowestSequenceIndex + 1;
  while (aEndIndex < pArchives.size() && pArchives[aEndIndex].mHeader.mArchiveIdentifier == aSelectedIdentifier) {
    ++aEndIndex;
  }

  const std::size_t aNormalizedStartIndex = aLowestSequenceIndex;
  const std::size_t aNormalizedArchiveCount = aEndIndex - aNormalizedStartIndex;
  if (aNormalizedArchiveCount > 0) {
    aCandidates.push_back({aNormalizedStartIndex, aNormalizedArchiveCount});
  }

  return aCandidates;
}

bool TryParseArchiveSet(FileSystem& pFileSystem,
                        const Crypt& pCrypt,
                        const std::vector<ArchiveHeaderRecord>& pArchives,
                        std::size_t pStartIndex,
                        std::size_t pArchiveCount,
                        std::size_t pStartPhysicalOffset,
                        bool pUseEncryption,
                        const std::string& pDestinationDirectory,
                        bool pWriteFiles,
                        Logger* pLogger,
                        std::uint64_t& pProcessedBytes,
                        std::size_t& pFilesProcessed,
                        UnpackFailureInfo& pFailure,
                        std::size_t pLogThrottleBlockSize,
                        std::size_t pLogThrottleIgnoreLast) {

  PageReader aReader(pFileSystem,
                     pCrypt,
                     pArchives,
                     pStartIndex,
                     pArchiveCount,
                     pStartPhysicalOffset,
                     pUseEncryption);
  RecordParser aParser(pFileSystem, pDestinationDirectory, pWriteFiles);
  std::uint64_t aLastBucket = 0;
  const std::uint64_t aThrottlePayloadLength =
      (pStartIndex < pArchives.size() && pArchives[pStartIndex].mPayloadLength > 0)
          ? static_cast<std::uint64_t>(pArchives[pStartIndex].mPayloadLength)
          : static_cast<std::uint64_t>(peanutbutter::SB_PAYLOAD_SIZE);
  const auto aProgressLogger = [&](std::uint64_t pDecodedBytes, std::size_t pDecodedFiles) {
    if (!pWriteFiles || pLogger == nullptr) {
      return;
    }
    const std::size_t aArchiveIndexOneBased = std::min<std::size_t>(
        pArchiveCount,
        static_cast<std::size_t>(pDecodedBytes / std::max<std::uint64_t>(1, aThrottlePayloadLength)) + 1);
    if (!ShouldLogArchiveProgress(aArchiveIndexOneBased,
                                  pArchiveCount,
                                  pDecodedBytes,
                                  aThrottlePayloadLength,
                                  pLogThrottleBlockSize,
                                  pLogThrottleIgnoreLast,
                                  aLastBucket)) {
      return;
    }
    pLogger->LogStatus("Unbundled archive " + std::to_string(aArchiveIndexOneBased) + " / " +
                       std::to_string(pArchiveCount) + ", " + std::to_string(pDecodedFiles) +
                       " files written, " + FormatBytes(pDecodedBytes) + ".");
  };
  if (!aParser.Parse(aReader, aProgressLogger)) {
    pFailure = aParser.Failure();
    return false;
  }

  pProcessedBytes = aParser.ProcessedBytes();
  pFilesProcessed = aParser.FilesProcessed();
  if (pWriteFiles && pLogger != nullptr && aParser.EmptyDirectoriesProcessed() > 0) {
    pLogger->LogStatus("Successfully unpacked " + std::to_string(aParser.EmptyDirectoriesProcessed()) +
                       " empty directories.");
  }
  return true;
}

bool SelectDecodableArchiveSet(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               const std::vector<ArchiveHeaderRecord>& pArchives,
                               bool pUseEncryption,
                               Logger* pLogger,
                               std::vector<ArchiveDecodeCandidate>& pCandidates,
                               std::size_t& pSelectedStartIndex,
                               std::size_t& pSelectedArchiveCount,
                               std::uint64_t& pTotalLogicalBytes,
                               std::size_t& pFilesProcessed,
                               UnpackFailureInfo& pFailure) {
  (void)pFileSystem;
  (void)pCrypt;
  (void)pUseEncryption;

  pSelectedStartIndex = 0;
  pSelectedArchiveCount = 0;
  pTotalLogicalBytes = 0;
  pFilesProcessed = 0;
  pCandidates.clear();
  pCandidates = BuildArchiveDecodeCandidates(pArchives, pLogger);
  if (pCandidates.empty()) {
    pFailure = {UnpackIntegerFailure::kUnknown, "no decode candidates were found in headers."};
    return false;
  }

  const ArchiveDecodeCandidate& aBestCandidate = pCandidates.front();
  pSelectedStartIndex = aBestCandidate.mStartIndex;
  pSelectedArchiveCount = aBestCandidate.mArchiveCount;

  if (pLogger != nullptr) {
    pLogger->LogStatus("Selected archive set start = " + std::to_string(pSelectedStartIndex) +
                       ", count = " + std::to_string(pSelectedArchiveCount) + ".");
  }

  pTotalLogicalBytes = 0;
  pFilesProcessed = 0;
  pFailure.mCode = UnpackIntegerFailure::kNone;
  pFailure.mMessage.clear();

  return true;
}

bool SelectDecodableArchiveSet(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               const std::vector<ArchiveHeaderRecord>& pArchives,
                               bool pUseEncryption,
                               Logger* pLogger,
                               std::size_t& pSelectedStartIndex,
                               std::size_t& pSelectedArchiveCount,
                               std::uint64_t& pTotalLogicalBytes,
                               std::size_t& pFilesProcessed,
                               UnpackFailureInfo& pFailure) {
  std::vector<ArchiveDecodeCandidate> aCandidates;
  return SelectDecodableArchiveSet(pFileSystem,
                                   pCrypt,
                                   pArchives,
                                   pUseEncryption,
                                   pLogger,
                                   aCandidates,
                                   pSelectedStartIndex,
                                   pSelectedArchiveCount,
                                   pTotalLogicalBytes,
                                   pFilesProcessed,
                                   pFailure);
}

std::vector<std::size_t> FindRecoveryStartPhysicalOffsetCandidates(const FileSystem& pFileSystem,
                                                                   const Crypt& pCrypt,
                                                                   const ArchiveHeaderRecord& pArchive,
                                                                   bool pUseEncryption,
                                                                   bool pSkipSpecialFirstHeader,
                                                                   UnpackFailureInfo& pFailure) {
  (void)pCrypt;
  (void)pUseEncryption;
  std::vector<std::size_t> aCandidates;
  std::unique_ptr<FileReadStream> aStream = pFileSystem.OpenReadStream(pArchive.mPath);
  if (aStream == nullptr || !aStream->IsReady() || aStream->GetLength() < kArchiveHeaderLength) {
    pFailure = {UnpackIntegerFailure::kUnknown, "Discovery: could not open selected recovery archive."};
    return aCandidates;
  }

  const std::size_t aPayloadLength = aStream->GetLength() - kArchiveHeaderLength;
  if (aPayloadLength <= kRecoveryHeaderLength) {
    pFailure = {UnpackIntegerFailure::kUnknown, "Discovery: selected recovery archive has no usable payload."};
    return aCandidates;
  }

  // Avoid pre-parse decryption work for recovery-start discovery. We enter the
  // working flow immediately and let TryParseArchiveSet surface fast failures.
  // This prevents "derive offset" from doubling crypt cost on very large packs.
  (void)pSkipSpecialFirstHeader;
  aCandidates.push_back(kRecoveryHeaderLength);
  return aCandidates;
}

bool WriteArchivePage(FileWriteStream& pStream,
                      const Crypt& pCrypt,
                      const WriterPage& pWriterPage,
                      CryptWorkspaceL3& pWorkspace,
                      std::string* pErrorMessage,
                      bool pUseEncryption) {
  if (!pUseEncryption) {
    return pStream.Write(pWriterPage.mBuffer, pWriterPage.mPhysicalLength);
  }

  std::fill_n(pWorkspace.Worker(), kL3Length, 0);
  std::fill_n(pWorkspace.Destination(), kL3Length, 0);
  if (!pCrypt.SealData(pWriterPage.mBuffer,
                       pWorkspace.Worker(),
                       pWorkspace.Destination(),
                       kL3Length,
                       pErrorMessage,
                       CryptMode::kNormal)) {
    return false;
  }
  return pStream.Write(pWorkspace.Destination(), pWriterPage.mPhysicalLength);
}

PreflightResult MakeInvalid(const std::string& pTitle, const std::string& pMessage) {
  return {PreflightSignal::RedLight, pTitle, pMessage};
}

PreflightResult MakeNeedsDestination(const std::string& pTitle, const std::string& pMessage) {
  return {PreflightSignal::YellowLight, pTitle, pMessage};
}

OperationResult MakeFailure(Logger& pLogger, const std::string& pTitle, const std::string& pMessage) {
  pLogger.LogError(pMessage);
  return {false, pTitle, pMessage};
}

}  // namespace

FunctionLogger::FunctionLogger(std::function<void(const std::string&, bool)> pSink) : mSink(std::move(pSink)) {}

void FunctionLogger::LogStatus(const std::string& pMessage) {
  if (mSink) {
    mSink(pMessage, false);
  }
}

void FunctionLogger::LogError(const std::string& pMessage) {
  if (mSink) {
    mSink(pMessage, true);
  }
}

void CapturingLogger::LogStatus(const std::string& pMessage) {
  mStatusMessages.push_back(pMessage);
}

void CapturingLogger::LogError(const std::string& pMessage) {
  mErrorMessages.push_back(pMessage);
}

const std::vector<std::string>& CapturingLogger::StatusMessages() const {
  return mStatusMessages;
}

const std::vector<std::string>& CapturingLogger::ErrorMessages() const {
  return mErrorMessages;
}

ApplicationCore::ApplicationCore(FileSystem& pFileSystem, Crypt& pCrypt, Logger& pLogger, RuntimeSettings pSettings)
    : mFileSystem(pFileSystem),
      mCrypt(pCrypt),
      mLogger(pLogger),
      mSettings(std::move(pSettings)) {}

void ApplicationCore::SetSettings(RuntimeSettings pSettings) {
  mSettings = std::move(pSettings);
}

PreflightResult ApplicationCore::CheckBundle(const BundleRequest& pRequest) const {
  if (!mFileSystem.IsDirectory(pRequest.mSourceDirectory)) {
    return MakeInvalid("Bundle Failed", "Bundle failed: source directory does not exist.");
  }
  if (mFileSystem.ListFilesRecursive(pRequest.mSourceDirectory).empty() &&
      mFileSystem.ListDirectoriesRecursive(pRequest.mSourceDirectory).empty()) {
    return MakeInvalid("Bundle Failed", "Bundle failed: source directory is empty.");
  }
  if (EffectiveArchivePayloadLength(mSettings) == 0) {
    return MakeInvalid("Bundle Failed", "Bundle failed: archive file length is too small.");
  }
  if (mFileSystem.DirectoryHasEntries(pRequest.mDestinationDirectory)) {
    return MakeNeedsDestination("Bundle Destination", "Bundle destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

PreflightResult ApplicationCore::CheckUnbundle(const UnbundleRequest& pRequest) const {
  if (!mFileSystem.IsDirectory(pRequest.mArchiveDirectory)) {
    return MakeInvalid("Unbundle Failed", "Unbundle failed: archive directory does not exist.");
  }
  if (!HasAnyReadableArchive(mFileSystem, pRequest.mArchiveDirectory)) {
    return MakeInvalid("Unbundle Failed", "Unbundle failed: no readable archives were found.");
  }
  if (mFileSystem.DirectoryHasEntries(pRequest.mDestinationDirectory)) {
    return MakeNeedsDestination("Unbundle Destination", "Unbundle destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

PreflightResult ApplicationCore::CheckRecover(const RecoverRequest& pRequest) const {
  if (!mFileSystem.IsDirectory(pRequest.mArchiveDirectory)) {
    return MakeInvalid("Recover Failed", "Recover failed: archive directory does not exist.");
  }
  std::optional<std::size_t> aDiscoveryStartIndex;
  const std::vector<ArchiveHeaderRecord> aArchives = ScanArchiveDirectory(mFileSystem,
                                                                          pRequest.mArchiveDirectory,
                                                                          &pRequest.mRecoveryStartFilePath,
                                                                          &aDiscoveryStartIndex);
  if (aArchives.empty()) {
    return MakeInvalid("Recover Failed", "Recover failed: no readable archives were found.");
  }
  if (!aDiscoveryStartIndex.has_value()) {
    return MakeInvalid("Recover Failed", "Recover failed: recovery start archive was not found.");
  }
  if (!aArchives[*aDiscoveryStartIndex].mHeader.mRecoveryEnabled) {
    return MakeInvalid("Recover Failed", "Recover failed: the selected archive is not marked recoverable.");
  }
  if (mFileSystem.DirectoryHasEntries(pRequest.mDestinationDirectory)) {
    return MakeNeedsDestination("Recover Destination", "Recover destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

PreflightResult ApplicationCore::CheckValidate(const ValidateRequest& pRequest) const {
  if (!mFileSystem.IsDirectory(pRequest.mLeftDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: source directory does not exist. Resolved source directory = " +
                           pRequest.mLeftDirectory);
  }
  if (!mFileSystem.IsDirectory(pRequest.mRightDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: destination directory does not exist. Resolved destination directory = " +
                           pRequest.mRightDirectory);
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult ApplicationCore::RunBundle(const BundleRequest& pRequest, DestinationAction pAction) {
  const PreflightResult aPreflight = CheckBundle(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(mLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Bundle Canceled", "Bundle canceled."};
  }
  if (!ApplyDestinationAction(mFileSystem, pRequest.mDestinationDirectory, pAction)) {
    return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not prepare destination directory.");
  }

  mLogger.LogStatus("Bundle job starting...");
  const std::vector<SourceFileEntry> aFiles = CollectSourceEntries(mFileSystem, pRequest.mSourceDirectory);
  const std::vector<std::string> aEmptyDirectories = CollectEmptyDirectoryEntries(mFileSystem, pRequest.mSourceDirectory);
  if (aFiles.empty() && aEmptyDirectories.empty()) {
    return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not read source files.");
  }
  mLogger.LogStatus("Found " + std::to_string(aFiles.size()) + " files and " +
                    std::to_string(aEmptyDirectories.size()) + " empty folders to bundle.");

  const std::size_t aPayloadLimit = EffectiveArchivePayloadLength(mSettings);
  const std::size_t aPhysicalPayloadLimit = EffectiveArchivePhysicalPayloadLength(mSettings);
  const std::size_t aLogicalCapacity = aPayloadLimit;
  if (aLogicalCapacity == 0) {
    return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: archive file length is too small.");
  }

  std::vector<std::size_t> aFileStartLogicalOffsets;
  std::vector<std::size_t> aFileEndLogicalOffsets;
  aFileStartLogicalOffsets.reserve(aFiles.size());
  aFileEndLogicalOffsets.reserve(aFiles.size());
  std::size_t aTotalLogicalLength = 0;
  std::uint64_t aTotalLogicalBytes = 0;
  for (const SourceFileEntry& aFile : aFiles) {
    aFileStartLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalLength += static_cast<std::size_t>(SerializedRecordLength(aFile));
    aFileEndLogicalOffsets.push_back(aTotalLogicalLength);
    aTotalLogicalBytes += aFile.mContentLength;
  }
  aTotalLogicalLength += 2;  // file record terminator
  for (const std::string& aDirectory : aEmptyDirectories) {
    if (aDirectory.empty()) {
      continue;
    }
    aTotalLogicalLength += 2 + aDirectory.size();
  }
  aTotalLogicalLength += 2;  // manifest terminator

  const std::vector<PlannedArchiveLayout> aLayouts =
      BuildArchiveLayouts(aTotalLogicalLength, aLogicalCapacity, aPhysicalPayloadLimit);
  if (aLayouts.empty()) {
    return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: no archive payloads were generated.");
  }
  const std::vector<std::vector<unsigned long long>> aRecoveryHeaders =
      GenerateAllRecoveryHeaders(aLayouts, aFileStartLogicalOffsets);

  const std::array<unsigned char, 8> aArchiveIdentifier = GenerateArchiveIdentifier();
  const std::string aSourceStem = mFileSystem.StemName(pRequest.mSourceDirectory);

  std::uint64_t aProcessedBytes = 0;
  std::uint64_t aLastBucket = 0;
  std::size_t aFilesProcessed = 0;
  LogicalRecordStreamer aStreamer(mFileSystem, aFiles, aEmptyDirectories);
  HeapBuffer aLogicalChunk(kL3Length);
  WriterPageOwner aWriterPageOwner;
  CryptWorkspaceL3 aCryptWorkspace;

  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aLayouts.size(); ++aArchiveIndex) {
    const PlannedArchiveLayout& aLayout = aLayouts[aArchiveIndex];
    ArchiveHeader aHeader;
    aHeader.mRecoveryEnabled = false;
    for (std::size_t aFileIndex = 0; aFileIndex < aFileStartLogicalOffsets.size(); ++aFileIndex) {
      if (aFileStartLogicalOffsets[aFileIndex] >= aLayout.mLogicalStart &&
          aFileStartLogicalOffsets[aFileIndex] < aLayout.mLogicalEnd) {
        aHeader.mRecoveryEnabled = true;
        break;
      }
    }
    aHeader.mSequence = aArchiveIndex;
    aHeader.mArchiveIdentifier = aArchiveIdentifier;

    const std::string aName = MakeArchiveName(aSourceStem,
                                              pRequest.mArchivePrefix,
                                              pRequest.mArchiveSuffix,
                                              aArchiveIndex + 1,
                                              aLayouts.size(),
                                              aHeader.mRecoveryEnabled);

    std::unique_ptr<FileWriteStream> aArchiveStream =
        mFileSystem.OpenWriteStream(mFileSystem.JoinPath(pRequest.mDestinationDirectory, aName));
    if (aArchiveStream == nullptr || !aArchiveStream->IsReady()) {
      return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not write archive " + aName);
    }
    unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
    WriteHeaderBuffer(aHeader, aHeaderBytes);
    if (!aArchiveStream->Write(aHeaderBytes, kArchiveHeaderLength)) {
      return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not write archive " + aName);
    }

    const std::size_t aBlockCount =
        (aLayout.mUsedPayloadLength + kL3Length - 1) / kL3Length;
    const std::size_t aArchiveLogicalLength = aLayout.mLogicalEnd - aLayout.mLogicalStart;
    std::size_t aArchiveLogicalWritten = 0;
    for (std::size_t aBlockIndex = 0; aBlockIndex < aBlockCount; ++aBlockIndex) {
      const std::size_t aBlockPhysicalLength =
          std::min<std::size_t>(kL3Length, aLayout.mUsedPayloadLength - (aBlockIndex * kL3Length));
      WriterPage& aWriterPage = aWriterPageOwner.Writer();
      aWriterPage.Reset(aRecoveryHeaders[aArchiveIndex],
                        aBlockIndex * (kL3Length / kL1Length),
                        aArchiveIndex == 0 && aBlockIndex == 0,
                        aBlockPhysicalLength);

      const std::size_t aBlockLogicalTarget =
          std::min(aWriterPage.PayloadCapacity(), aArchiveLogicalLength - aArchiveLogicalWritten);
      std::size_t aBlockLogicalWritten = 0;
      while (aBlockLogicalWritten < aBlockLogicalTarget) {
        const std::size_t aChunkTarget =
            std::min<std::size_t>(kL3Length, aBlockLogicalTarget - aBlockLogicalWritten);
        std::size_t aChunkBytesRead = 0;
        if (!aStreamer.Read(aLogicalChunk.Data(), aChunkTarget, aChunkBytesRead)) {
          return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not stream source bytes.");
        }
        if (aChunkBytesRead == 0) {
          return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: source stream ended unexpectedly.");
        }

        const std::size_t aBytesWritten = aWriterPage.WritePayloadBytes(aLogicalChunk.Data(), aChunkBytesRead);
        if (aBytesWritten != aChunkBytesRead) {
          return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: page packing overflowed unexpectedly.");
        }
        aBlockLogicalWritten += aBytesWritten;
      }

      std::string aCryptError;
      if (!WriteArchivePage(*aArchiveStream,
                            mCrypt,
                            aWriterPage,
                            aCryptWorkspace,
                            &aCryptError,
                            pRequest.mUseEncryption)) {
        return MakeFailure(mLogger,
                           "Bundle Failed",
                           aCryptError.empty() ? "Bundle failed: could not write archive " + aName
                                               : "Bundle failed: " + aCryptError);
      }
      aArchiveLogicalWritten += aBlockLogicalWritten;
    }

    if (!aArchiveStream->Close()) {
      return MakeFailure(mLogger, "Bundle Failed", "Bundle failed: could not finalize archive " + aName);
    }

    for (std::size_t aFileIndex = 0; aFileIndex < aFiles.size(); ++aFileIndex) {
      if (aFileEndLogicalOffsets[aFileIndex] <= aLayout.mLogicalEnd &&
          aFileEndLogicalOffsets[aFileIndex] > aLayout.mLogicalStart) {
        ++aFilesProcessed;
        aProcessedBytes += aFiles[aFileIndex].mContentLength;
      }
    }

    if (ShouldLogArchiveProgress(aArchiveIndex + 1,
                                 aLayouts.size(),
                                 aProcessedBytes,
                                 aPayloadLimit,
                                 mSettings.mLogThrottleBlockSize,
                                 mSettings.mLogThrottleIgnoreLast,
                                 aLastBucket)) {
      mLogger.LogStatus("Bundled archive " + std::to_string(aArchiveIndex + 1) + " / " +
                        std::to_string(aLayouts.size()) + ", " + std::to_string(aFilesProcessed) +
                        " files written, " + FormatBytes(aProcessedBytes) + " / " +
                        FormatBytes(aTotalLogicalBytes) + ".");
    }
  }

  mLogger.LogStatus("Bundle job complete.");
  return {true, "Bundle Complete", "Bundle completed successfully."};
}

OperationResult ApplicationCore::RunUnbundle(const UnbundleRequest& pRequest, DestinationAction pAction) {
  const PreflightResult aPreflight = CheckUnbundle(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(mLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Unbundle Canceled", "Unbundle canceled."};
  }
  if (!ApplyDestinationAction(mFileSystem, pRequest.mDestinationDirectory, pAction)) {
    return MakeFailure(mLogger, "Unbundle Failed", "Unbundle failed: could not prepare destination directory.");
  }

  mLogger.LogStatus("Unbundle job starting...");
  const std::vector<ArchiveHeaderRecord> aArchives = ScanArchiveDirectory(mFileSystem, pRequest.mArchiveDirectory);
  if (aArchives.empty()) {
    return MakeFailure(mLogger, "Unbundle Failed", "Unbundle failed: no readable archives were found.");
  }

  std::size_t aSelectedStartIndex = 0;
  std::size_t aSelectedArchiveCount = 0;
  std::vector<ArchiveDecodeCandidate> aDecodeCandidates;
  std::uint64_t aTotalLogicalBytes = 0;
  std::size_t aFilesProcessed = 0;
  UnpackFailureInfo aDecodeFailure;
  if (!SelectDecodableArchiveSet(mFileSystem,
                                 mCrypt,
                                 aArchives,
                                 pRequest.mUseEncryption,
                                 &mLogger,
                                 aDecodeCandidates,
                                 aSelectedStartIndex,
                                 aSelectedArchiveCount,
                                 aTotalLogicalBytes,
                                 aFilesProcessed,
                                 aDecodeFailure)) {
    return MakeFailure(mLogger, "Unbundle Failed", "Unbundle failed: " + FormatUnpackFailure(aDecodeFailure));
  }

  std::uint64_t aProcessedBytes = 0;
  UnpackFailureInfo aWriteFailure;
  if (!TryParseArchiveSet(mFileSystem,
                          mCrypt,
                          aArchives,
                          aSelectedStartIndex,
                          aSelectedArchiveCount,
                          kRecoveryHeaderLength,
                          pRequest.mUseEncryption,
                          pRequest.mDestinationDirectory,
                          true,
                          &mLogger,
                          aProcessedBytes,
                          aFilesProcessed,
                          aWriteFailure,
                          mSettings.mLogThrottleBlockSize,
                          mSettings.mLogThrottleIgnoreLast)) {
    return MakeFailure(mLogger, "Unbundle Failed", "Unbundle failed: " + FormatUnpackFailure(aWriteFailure));
  }

  std::uint64_t aLastBucket = 0;
  if (ShouldLogArchiveProgress(aSelectedArchiveCount,
                               aSelectedArchiveCount,
                               aProcessedBytes,
                               EffectiveArchivePayloadLength(mSettings),
                               mSettings.mLogThrottleBlockSize,
                               mSettings.mLogThrottleIgnoreLast,
                               aLastBucket)) {
    mLogger.LogStatus("Unbundled archive " + std::to_string(aSelectedArchiveCount) + " / " +
                      std::to_string(aSelectedArchiveCount) + ", " + std::to_string(aFilesProcessed) +
                      " files written, " + FormatBytes(aProcessedBytes) + " / " +
                      FormatBytes(aTotalLogicalBytes) + ".");
  }

  mLogger.LogStatus("Unbundle job complete.");
  return {true, "Unbundle Complete", "Unbundle completed successfully."};
}

OperationResult ApplicationCore::RunRecover(const RecoverRequest& pRequest, DestinationAction pAction) {
  const PreflightResult aPreflight = CheckRecover(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(mLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Recover Canceled", "Recover canceled."};
  }
  if (!ApplyDestinationAction(mFileSystem, pRequest.mDestinationDirectory, pAction)) {
    return MakeFailure(mLogger, "Recover Failed", "Recover failed: could not prepare destination directory.");
  }

  mLogger.LogStatus("Recover job starting...");
  std::optional<std::size_t> aStartIndex;
  const std::vector<ArchiveHeaderRecord> aArchives = ScanArchiveDirectory(mFileSystem,
                                                                          pRequest.mArchiveDirectory,
                                                                          &pRequest.mRecoveryStartFilePath,
                                                                          &aStartIndex);
  if (!aStartIndex.has_value()) {
    return MakeFailure(mLogger, "Recover Failed", "Recover failed: recovery start archive was not found.");
  }

  std::size_t aMatchingArchiveCount = 0;
  const auto aExpectedArchiveIdentifier = aArchives[*aStartIndex].mHeader.mArchiveIdentifier;
  for (std::size_t aArchiveIndex = *aStartIndex; aArchiveIndex < aArchives.size(); ++aArchiveIndex) {
    if (aArchives[aArchiveIndex].mHeader.mArchiveIdentifier != aExpectedArchiveIdentifier) {
      break;
    }
    ++aMatchingArchiveCount;
  }

  bool aRecovered = false;
  std::uint64_t aTotalLogicalBytes = 0;
  std::size_t aFilesProcessed = 0;
  UnpackFailureInfo aRecoveryFailure;
  const std::vector<std::size_t> aStartCandidates =
      FindRecoveryStartPhysicalOffsetCandidates(mFileSystem,
                                                mCrypt,
                                                aArchives[*aStartIndex],
                                                pRequest.mUseEncryption,
                                                *aStartIndex == 0,
                                                aRecoveryFailure);
  if (aStartCandidates.empty() && aRecoveryFailure.mCode != UnpackIntegerFailure::kNone) {
    return MakeFailure(mLogger, "Recover Failed", "Recover failed: " + FormatUnpackFailure(aRecoveryFailure));
  }
  for (const std::size_t aStartPhysicalOffset : aStartCandidates) {
    UnpackFailureInfo aLocalFailure;
    std::uint64_t aProcessedBytes = 0;
    std::size_t aWriteFilesProcessed = 0;
    if (TryParseArchiveSet(mFileSystem,
                           mCrypt,
                           aArchives,
                           *aStartIndex,
                           aMatchingArchiveCount,
                           aStartPhysicalOffset,
                           pRequest.mUseEncryption,
                           pRequest.mDestinationDirectory,
                           true,
                           &mLogger,
                           aProcessedBytes,
                           aWriteFilesProcessed,
                           aLocalFailure,
                           mSettings.mLogThrottleBlockSize,
                           mSettings.mLogThrottleIgnoreLast) &&
        aWriteFilesProcessed > 0) {
      aTotalLogicalBytes = aProcessedBytes;
      aFilesProcessed = aWriteFilesProcessed;
      aRecovered = true;
      break;
    }
    if (aRecoveryFailure.mCode == UnpackIntegerFailure::kNone && aLocalFailure.mCode != UnpackIntegerFailure::kNone) {
      aRecoveryFailure = aLocalFailure;
    }
  }
  if (!aRecovered) {
    if (aRecoveryFailure.mCode == UnpackIntegerFailure::kNone) {
      aRecoveryFailure = {UnpackIntegerFailure::kUnknown, "archive payloads could not be decoded."};
    }
    return MakeFailure(mLogger, "Recover Failed", "Recover failed: " + FormatUnpackFailure(aRecoveryFailure));
  }
  const std::uint64_t aProcessedBytes = aTotalLogicalBytes;

  std::uint64_t aLastBucket = 0;
  if (ShouldLogArchiveProgress(aMatchingArchiveCount,
                               aMatchingArchiveCount,
                               aProcessedBytes,
                               EffectiveArchivePayloadLength(mSettings),
                               mSettings.mLogThrottleBlockSize,
                               mSettings.mLogThrottleIgnoreLast,
                               aLastBucket)) {
    mLogger.LogStatus("Recovered archive " + std::to_string(aMatchingArchiveCount) + " / " +
                      std::to_string(aMatchingArchiveCount) + ", " + std::to_string(aFilesProcessed) +
                      " files written, " + FormatBytes(aProcessedBytes) + " / " +
                      FormatBytes(aTotalLogicalBytes) + ".");
  }

  mLogger.LogStatus("Recover job complete.");
  return {true, "Recover Complete", "Recover completed successfully."};
}

OperationResult ApplicationCore::RunValidate(const ValidateRequest& pRequest) {
  const PreflightResult aPreflight = CheckValidate(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(mLogger, aPreflight.mTitle, aPreflight.mMessage);
  }

  mLogger.LogStatus("Sanity job starting...");
  const std::vector<DirectoryEntry> aLeftEntries = mFileSystem.ListFilesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightEntries = mFileSystem.ListFilesRecursive(pRequest.mRightDirectory);
  const std::vector<DirectoryEntry> aLeftDirectories = mFileSystem.ListDirectoriesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightDirectories = mFileSystem.ListDirectoriesRecursive(pRequest.mRightDirectory);

  std::map<std::string, ComparedPath> aLeftFiles;
  std::map<std::string, ComparedPath> aRightFiles;
  std::map<std::string, ComparedPath> aLeftEmptyDirectories;
  std::map<std::string, ComparedPath> aRightEmptyDirectories;
  for (const DirectoryEntry& aEntry : aLeftEntries) {
    if (aEntry.mIsDirectory || aEntry.mRelativePath.empty()) {
      continue;
    }
    aLeftFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
  }
  for (const DirectoryEntry& aEntry : aRightEntries) {
    if (aEntry.mIsDirectory || aEntry.mRelativePath.empty()) {
      continue;
    }
    aRightFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
  }
  for (const DirectoryEntry& aEntry : aLeftDirectories) {
    if (aEntry.mRelativePath.empty() || mFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      continue;
    }
    aLeftEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
  }
  for (const DirectoryEntry& aEntry : aRightDirectories) {
    if (aEntry.mRelativePath.empty() || mFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      continue;
    }
    aRightEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
  }

  ValidationReport aReport;
  CompareProgress aProgress;
  std::uint64_t aNextProgressThreshold = kValidationProgressByteStep;
  std::size_t aLastReportedFiles = 0;
  const std::size_t aTotalFilesToScan = aLeftFiles.size();

  auto ReportCompareProgressIfNeeded = [&]() {
    const std::uint64_t aTotalBytesProcessed =
        aProgress.mSourceBytesProcessed + aProgress.mDestinationBytesProcessed;
    if (aTotalBytesProcessed < aNextProgressThreshold || aProgress.mFilesScanned <= aLastReportedFiles) {
      return;
    }

    const bool aStillPerfectMatch =
        aReport.mOnlyInSourceVisible.empty() && aReport.mOnlyInDestinationVisible.empty() &&
        aReport.mDataMismatchVisible.empty() && aReport.mOnlyInSourceHidden.empty() &&
        aReport.mOnlyInDestinationHidden.empty() && aReport.mDataMismatchHidden.empty();

    std::string aByteSummary;
    if (aStillPerfectMatch) {
      aByteSummary = FormatBytes(aTotalBytesProcessed);
    } else {
      aByteSummary = FormatBytes(aProgress.mSourceBytesProcessed) + " and " +
                     FormatBytes(aProgress.mDestinationBytesProcessed);
    }

    mLogger.LogStatus("Scanned " + aByteSummary + ", " +
                      std::to_string(aProgress.mFilesScanned) + " / " + std::to_string(aTotalFilesToScan) +
                      " files");
    aLastReportedFiles = aProgress.mFilesScanned;
    while (aTotalBytesProcessed >= aNextProgressThreshold) {
      aNextProgressThreshold += kValidationProgressByteStep;
    }
  };

  for (const auto& [aRelativePath, aLeftEntry] : aLeftFiles) {
    const auto aRightPaths = aRightFiles.find(aRelativePath);
    if (aRightPaths == aRightFiles.end()) {
      ++aProgress.mFilesScanned;
      if (aLeftEntry.mIsHidden) {
        aReport.mOnlyInSourceHidden.push_back(aRelativePath);
      } else {
        aReport.mOnlyInSourceVisible.push_back(aRelativePath);
      }
      ReportCompareProgressIfNeeded();
      continue;
    }

    DirectoryEntry aSourceEntry{aLeftEntry.mPath, aRelativePath, false};
    DirectoryEntry aDestinationEntry{aRightPaths->second.mPath, aRelativePath, false};
    if (!FilesMatchByteForByte(mFileSystem, aSourceEntry, aDestinationEntry, &aProgress.mSourceBytesProcessed,
                              &aProgress.mDestinationBytesProcessed)) {
      if (aLeftEntry.mIsHidden) {
        aReport.mDataMismatchHidden.push_back(aRelativePath);
      } else {
        aReport.mDataMismatchVisible.push_back(aRelativePath);
      }
    }
    ++aProgress.mFilesScanned;
    ReportCompareProgressIfNeeded();
  }

  for (const auto& [aRelativePath, aRightEntry] : aRightFiles) {
    if (aLeftFiles.find(aRelativePath) == aLeftFiles.end()) {
      if (aRightEntry.mIsHidden) {
        aReport.mOnlyInDestinationHidden.push_back(aRelativePath);
      } else {
        aReport.mOnlyInDestinationVisible.push_back(aRelativePath);
      }
    }
  }
  for (const auto& [aRelativePath, aLeftDirectory] : aLeftEmptyDirectories) {
    if (aRightEmptyDirectories.find(aRelativePath) == aRightEmptyDirectories.end()) {
      if (aLeftDirectory.mIsHidden) {
        aReport.mEmptyDirectoriesOnlyInSourceHidden.push_back(aRelativePath);
      } else {
        aReport.mEmptyDirectoriesOnlyInSourceVisible.push_back(aRelativePath);
      }
    }
  }
  for (const auto& [aRelativePath, aRightDirectory] : aRightEmptyDirectories) {
    if (aLeftEmptyDirectories.find(aRelativePath) == aLeftEmptyDirectories.end()) {
      if (aRightDirectory.mIsHidden) {
        aReport.mEmptyDirectoriesOnlyInDestinationHidden.push_back(aRelativePath);
      } else {
        aReport.mEmptyDirectoriesOnlyInDestinationVisible.push_back(aRelativePath);
      }
    }
  }

  const bool aHasVisibleDifferences = !aReport.mOnlyInSourceVisible.empty() ||
                                     !aReport.mOnlyInDestinationVisible.empty() ||
                                     !aReport.mDataMismatchVisible.empty() ||
                                     !aReport.mEmptyDirectoriesOnlyInSourceVisible.empty() ||
                                     !aReport.mEmptyDirectoriesOnlyInDestinationVisible.empty();
  const bool aHasHiddenDifferences = !aReport.mOnlyInSourceHidden.empty() ||
                                    !aReport.mOnlyInDestinationHidden.empty() ||
                                    !aReport.mDataMismatchHidden.empty() ||
                                    !aReport.mEmptyDirectoriesOnlyInSourceHidden.empty() ||
                                    !aReport.mEmptyDirectoriesOnlyInDestinationHidden.empty();

  const std::string aFinalStatus = aHasVisibleDifferences
                                      ? "[ERROR] one or more non-hidden files differ."
                                      : (aHasHiddenDifferences ? "[WARN] only hidden files or folders differ."
                                                              : "[OK] source and destination trees are byte-for-byte equal.");

  const std::string aOutputPath =
      (std::filesystem::current_path() / "tree_validation_report_generated.txt").generic_string();
  std::ofstream aReportStream(aOutputPath, std::ios::out | std::ios::trunc);
  bool aReportWritten = false;
  if (aReportStream.is_open()) {
    aReportStream << "tree_validation_report_generated.txt\n\n";
    aReportStream << "Source = " << pRequest.mLeftDirectory << "\n";
    aReportStream << "Destination = " << pRequest.mRightDirectory << "\n\n";
    aReportStream << "PASS 1: hidden files or folders\n";
    WriteLimitedGroupedSection(aReportStream, "only in source", "A_ONLY ", aReport.mOnlyInSourceHidden);
    WriteLimitedGroupedSection(aReportStream, "only in destination", "B_ONLY ", aReport.mOnlyInDestinationHidden);
    WriteLimitedGroupedSection(aReportStream, "content mismatches", "DIFF   ", aReport.mDataMismatchHidden);
    WriteLimitedGroupedSection(aReportStream, "empty directories only in source", "A_DIR  ", aReport.mEmptyDirectoriesOnlyInSourceHidden);
    WriteLimitedGroupedSection(aReportStream, "empty directories only in destination", "B_DIR  ", aReport.mEmptyDirectoriesOnlyInDestinationHidden);
    aReportStream << "\n";
    aReportStream << "PASS 2: non-hidden files or folders\n";
    WriteLimitedGroupedSection(aReportStream, "only in source", "A_ONLY ", aReport.mOnlyInSourceVisible);
    WriteLimitedGroupedSection(aReportStream, "only in destination", "B_ONLY ", aReport.mOnlyInDestinationVisible);
    WriteLimitedGroupedSection(aReportStream, "content mismatches", "DIFF   ", aReport.mDataMismatchVisible);
    WriteLimitedGroupedSection(aReportStream, "empty directories only in source", "A_DIR  ", aReport.mEmptyDirectoriesOnlyInSourceVisible);
    WriteLimitedGroupedSection(aReportStream, "empty directories only in destination", "B_DIR  ", aReport.mEmptyDirectoriesOnlyInDestinationVisible);
    aReportStream << "\n";
    aReportStream << aFinalStatus << "\n";
    aReportStream.close();
    aReportWritten = aReportStream.good();
  }

  if (aReportWritten) {
    mLogger.LogStatus("Generated " + aOutputPath);
  } else {
    mLogger.LogStatus("[WARN] Could not write tree_validation_report_generated.txt to " + aOutputPath +
                      ". Continuing without a report file.");
  }
  if (!aReport.mOnlyInSourceHidden.empty()) {
    mLogger.LogStatus("Hidden-only source extras: " + std::to_string(aReport.mOnlyInSourceHidden.size()));
  }
  if (!aReport.mOnlyInDestinationHidden.empty()) {
    mLogger.LogStatus("Hidden-only destination extras: " + std::to_string(aReport.mOnlyInDestinationHidden.size()));
  }
  if (!aReport.mDataMismatchHidden.empty()) {
    mLogger.LogStatus("Hidden byte mismatches: " + std::to_string(aReport.mDataMismatchHidden.size()));
  }
  if (!aReport.mEmptyDirectoriesOnlyInSourceHidden.empty()) {
    mLogger.LogStatus("Hidden empty-directory source extras: " +
                      std::to_string(aReport.mEmptyDirectoriesOnlyInSourceHidden.size()));
  }
  if (!aReport.mEmptyDirectoriesOnlyInDestinationHidden.empty()) {
    mLogger.LogStatus("Hidden empty-directory destination extras: " +
                      std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationHidden.size()));
  }
  mLogger.LogStatus("Visible-only source extras: " + std::to_string(aReport.mOnlyInSourceVisible.size()));
  mLogger.LogStatus("Visible-only destination extras: " + std::to_string(aReport.mOnlyInDestinationVisible.size()));
  mLogger.LogStatus("Visible byte mismatches: " + std::to_string(aReport.mDataMismatchVisible.size()));
  mLogger.LogStatus("Visible empty-directory source extras: " + std::to_string(aReport.mEmptyDirectoriesOnlyInSourceVisible.size()));
  mLogger.LogStatus("Visible empty-directory destination extras: " + std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationVisible.size()));
  
  ReportPathSample(mLogger, "Hidden files in A, not in B", aReport.mOnlyInSourceHidden);
  ReportPathSample(mLogger, "Hidden files in B, not in A", aReport.mOnlyInDestinationHidden);
  ReportPathSample(mLogger, "Hidden file mismatches in A and B", aReport.mDataMismatchHidden);
  ReportPathSample(mLogger, "Hidden empty directories in A, not in B", aReport.mEmptyDirectoriesOnlyInSourceHidden);
  ReportPathSample(mLogger, "Hidden empty directories in B, not in A", aReport.mEmptyDirectoriesOnlyInDestinationHidden);
  ReportPathSample(mLogger, "Visible files in A, not in B", aReport.mOnlyInSourceVisible);
  ReportPathSample(mLogger, "Visible files in B, not in A", aReport.mOnlyInDestinationVisible);
  ReportPathSample(mLogger, "Visible file mismatches in A and B", aReport.mDataMismatchVisible);
  ReportPathSample(mLogger, "Visible empty directories in A, not in B", aReport.mEmptyDirectoriesOnlyInSourceVisible);
  ReportPathSample(mLogger, "Visible empty directories in B, not in A", aReport.mEmptyDirectoriesOnlyInDestinationVisible);
  mLogger.LogStatus("Scanned " + std::to_string(aProgress.mFilesScanned) + " files " +
                    std::to_string(aLeftDirectories.size() + aRightDirectories.size()) + " directories.");
                    
  mLogger.LogStatus(aFinalStatus);

  mLogger.LogStatus("Sanity job complete.");
  return {true, "Sanity Complete", "Directory trees checked."};
}

}  // namespace peanutbutter
