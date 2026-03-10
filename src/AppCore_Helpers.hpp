#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_HELPERS_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_HELPERS_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "AppCore.hpp"

namespace peanutbutter::detail {

inline constexpr std::size_t kArchiveHeaderLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
inline constexpr std::uint32_t kMagicHeaderBytes = peanutbutter::MAGIC_HEADER_BYTES;
inline constexpr std::uint32_t kMajorVersion = peanutbutter::MAJOR_VERSION;
inline constexpr std::uint32_t kMinorVersion = peanutbutter::MINOR_VERSION;
inline constexpr std::size_t kRecoveryHeaderLength = peanutbutter::SB_RECOVERY_HEADER_LENGTH;
inline constexpr std::size_t kPayloadBytesPerBlock = peanutbutter::SB_PAYLOAD_SIZE;
inline constexpr std::size_t kBlockLength = peanutbutter::SB_L1_LENGTH;
inline constexpr std::size_t kPageLength = peanutbutter::SB_L3_LENGTH;
inline constexpr std::size_t kPageBlockCount = peanutbutter::SB_L3_LENGTH / peanutbutter::SB_L1_LENGTH;
inline constexpr std::size_t kFixedIoChunkLength = 64 * 1024;
inline constexpr std::size_t kScanLogThrottleFileCount = 10000;
inline constexpr std::uint64_t kRecoverLogStepBytes = 100ull * 1024ull * 1024ull;
inline constexpr std::uint64_t kDirectoryRecordContentMarker = (1ULL << 48) - 1ULL;

struct ArchiveHeader {
  std::uint32_t mMagic = kMagicHeaderBytes;
  std::uint16_t mVersionMajor = static_cast<std::uint16_t>(kMajorVersion & 0xFFFFU);
  std::uint16_t mVersionMinor = static_cast<std::uint16_t>(kMinorVersion & 0xFFFFU);
  std::uint64_t mIdentifier = 0;
  std::uint32_t mArchiveIndex = 0;
  std::uint32_t mArchiveCount = 0;
  std::uint32_t mPayloadLength = 0;
  std::uint8_t mRecordCountMod256 = 0;
  std::uint8_t mFolderCountMod256 = 0;
  std::uint64_t mReserved = 0;
};

struct RecoveryHeader {
  std::uint64_t mChecksum = 0;
  std::uint64_t mDistanceToNextRecord = 0;
};

static_assert(sizeof(ArchiveHeader) == peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH,
              "ArchiveHeader size must match SB_PLAIN_TEXT_HEADER_LENGTH.");
static_assert(sizeof(RecoveryHeader) == peanutbutter::SB_RECOVERY_HEADER_LENGTH,
              "RecoveryHeader size must match SB_RECOVERY_HEADER_LENGTH.");

struct ArchiveHeaderRecord {
  std::string mPath;
  std::string mName;
  ArchiveHeader mHeader;
  std::size_t mPayloadLength = 0;
  bool mMissingGap = false;
};

struct SourceFileEntry {
  std::string mSourcePath;
  std::string mRelativePath;
  std::uint64_t mContentLength = 0;
};

struct ArchiveInputSelection {
  std::string mSearchDirectory;
  std::string mSelectedFilePath;
};

PreflightResult MakeInvalid(const std::string& pTitle, const std::string& pMessage);
PreflightResult MakeNeedsDestination(const std::string& pTitle, const std::string& pMessage);
OperationResult MakeFailure(Logger& pLogger, const std::string& pTitle, const std::string& pMessage);

std::string FormatBytes(std::uint64_t pBytes);
std::optional<ArchiveInputSelection> ResolveArchiveInputSelection(const FileSystem& pFileSystem,
                                                                  const std::string& pArchivePathOrDirectory);
std::optional<ArchiveInputSelection> ResolveArchiveInputSelection(const FileSystem& pFileSystem,
                                                                  const std::string& pArchivePathOrDirectory,
                                                                  const std::string& pSelectedFilePath);
std::vector<DirectoryEntry> CollectArchiveFilesByHeaderScan(const FileSystem& pFileSystem,
                                                            const ArchiveInputSelection& pSelection);
std::vector<SourceFileEntry> CollectSourceEntries(const FileSystem& pFileSystem,
                                                  const std::string& pSourceDirectory);
std::vector<std::string> CollectEmptyDirectoryEntries(const FileSystem& pFileSystem,
                                                      const std::string& pSourceDirectory);
std::size_t LogicalCapacityForPhysicalLength(std::size_t pPhysicalLength);
std::size_t EffectiveArchivePhysicalPayloadLength(const RuntimeSettings& pSettings);
std::size_t EffectiveArchiveLogicalPayloadLength(const RuntimeSettings& pSettings);
std::size_t PhysicalOffsetForLogicalOffset(std::size_t pLogicalOffset);
std::size_t PhysicalLengthForLogicalLength(std::size_t pLogicalLength);
bool TryBuildBundleSettings(const RuntimeSettings& pBaseSettings,
                            const BundleRequest& pRequest,
                            RuntimeSettings& pBundleSettings,
                            std::string* pErrorMessage);
void GenerateChecksum(const unsigned char* pBlockData, unsigned char* pDestination);
std::uint64_t ReadLeFromBytes(const unsigned char* pBytes, std::size_t pWidth);
void WriteLeToBytes(unsigned char* pBytes, std::uint64_t pValue, std::size_t pWidth);
bool TryReadArchiveHeader(const FileSystem& pFileSystem,
                          const std::string& pPath,
                          ArchiveHeader& pHeader,
                          std::size_t& pFileLength);
void WriteArchiveHeaderBytes(const ArchiveHeader& pHeader, unsigned char* pBytes);
std::uint64_t GenerateArchiveIdentifier();
bool ApplyDestinationAction(FileSystem& pFileSystem,
                            const std::string& pDestinationDirectory,
                            DestinationAction pAction);
std::string MakeArchiveName(const std::string& pSourceStem,
                            const std::string& pPrefix,
                            const std::string& pSuffix,
                            std::size_t pSequenceOneBased,
                            std::size_t pArchiveCount);
void LogMissingArchiveRanges(Logger& pLogger,
                             const std::vector<std::pair<std::uint32_t, std::uint32_t>>& pMissingRanges);

PreflightResult CheckBundleJob(FileSystem& pFileSystem,
                               const RuntimeSettings& pSettings,
                               const BundleRequest& pRequest);
OperationResult RunBundleJob(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const RuntimeSettings& pSettings,
                             const BundleRequest& pRequest,
                             DestinationAction pAction);

PreflightResult CheckDecodeJob(FileSystem& pFileSystem,
                               const std::string& pJobName,
                               const std::string& pArchivePathOrDirectory,
                               const std::string& pSelectedArchiveFilePath,
                               const std::string& pDestinationDirectory);
OperationResult RunDecodeJob(FileSystem& pFileSystem,
                             const Crypt& pCrypt,
                             Logger& pLogger,
                             const RuntimeSettings& pSettings,
                             const std::string& pJobName,
                             const std::string& pArchivePathOrDirectory,
                             const std::string& pSelectedArchiveFilePath,
                             const std::string& pDestinationDirectory,
                             bool pUseEncryption,
                             bool pRecoverMode,
                             DestinationAction pAction);

PreflightResult CheckUnbundleJob(FileSystem& pFileSystem, const UnbundleRequest& pRequest);
OperationResult RunUnbundleJob(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               Logger& pLogger,
                               const RuntimeSettings& pSettings,
                               const UnbundleRequest& pRequest,
                               DestinationAction pAction);

PreflightResult CheckRecoverJob(FileSystem& pFileSystem, const RecoverRequest& pRequest);
OperationResult RunRecoverJob(FileSystem& pFileSystem,
                              const Crypt& pCrypt,
                              Logger& pLogger,
                              const RuntimeSettings& pSettings,
                              const RecoverRequest& pRequest,
                              DestinationAction pAction);

}  // namespace peanutbutter::detail

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_HELPERS_HPP_
