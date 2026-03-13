#ifndef PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
#define PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_

#include <cstddef>
#include <cstdint>

#define ARCHIVE_HEADER_LENGTH 40u
#define RECOVERY_HEADER_LENGTH 40u

#define MAX_VALID_FILE_PATH_LENGTH 2048u
#define MAX_ARCHIVE_COUNT 65535u
#define MAX_BLOCKS_PER_ARCHIVE 2048u

// Canonical fixed-size block and header lengths.
#if defined(PEANUT_BUTTER_ULTIMA_TEST_BUILD)
#define BLOCK_SIZE_L3 1u + RECOVERY_HEADER_LENGTH /*T-L3*/
#define TEST_SEED 777u /*T-SEED*/
#define TEST_LOOPS 1u /*T-LOOPS*/
#define TEST_BLOCK_COUNT 2u /*T-BLOCKS*/

#else
#define BLOCK_SIZE_L3 144000u + RECOVERY_HEADER_LENGTH

// assert((BLOCK_SIZE_L3 % 48) == 0)

#endif


namespace peanutbutter {

inline constexpr std::uint32_t kMagicHeaderBytes = 0xDECAFBADu;
inline constexpr std::uint32_t kMagicFooterBytes = 0xF01DAB1Eu;
inline constexpr std::uint32_t kMajorVersion = 1u;
inline constexpr std::uint32_t kMinorVersion = 2u;

// Logging controls.
inline constexpr std::uint32_t kElapsedTimeLogIntervalSeconds = 300u;          // 5 minutes
inline constexpr std::uint32_t kProgressCountLogIntervalDefault = 10000u;      // grouped count logs
inline constexpr std::uint64_t kProgressByteLogIntervalDefault = 100u * 1024u * 1024u;  // 100MB
inline constexpr std::uint64_t kProgressByteLogIntervalValidation = 1024u * 1024u * 1024u;  // 1GB
inline constexpr std::size_t kValidationLogCapFiles = 40u;
inline constexpr std::size_t kValidationLogCapFolders = 40u;
inline constexpr std::size_t kDebugLogLineCharacterLimit = 512u;  // kept chars + "..."
inline constexpr std::size_t kDebugLogLineLimit = 5000u;           // ring-buffer lines in UI

inline constexpr std::size_t kBlockSizeL3 = static_cast<std::size_t>(BLOCK_SIZE_L3);
inline constexpr std::size_t kArchiveHeaderLength = static_cast<std::size_t>(ARCHIVE_HEADER_LENGTH);
inline constexpr std::size_t kRecoveryHeaderLength = static_cast<std::size_t>(RECOVERY_HEADER_LENGTH);
inline constexpr std::size_t kMaxPathLength = static_cast<std::size_t>(MAX_VALID_FILE_PATH_LENGTH);
inline constexpr std::uint32_t kMaxArchiveCount = static_cast<std::uint32_t>(MAX_ARCHIVE_COUNT);
inline constexpr std::uint32_t kMaxBlocksPerArchive = static_cast<std::uint32_t>(MAX_BLOCKS_PER_ARCHIVE);
inline constexpr std::size_t kPayloadBytesPerL3 =
    kBlockSizeL3 > kRecoveryHeaderLength ? kBlockSizeL3 - kRecoveryHeaderLength : 0;
inline constexpr std::uint32_t kMinimumArchiveBlockCount = 1u;

inline constexpr bool IsValidArchiveBlockCount(std::uint32_t pBlockCount) {
  return pBlockCount >= kMinimumArchiveBlockCount && pBlockCount <= kMaxBlocksPerArchive;
}

inline constexpr std::uint64_t ComputeArchiveSizeForBlockCount(std::uint32_t pBlockCount) {
  return static_cast<std::uint64_t>(kArchiveHeaderLength) +
         (static_cast<std::uint64_t>(kBlockSizeL3) * static_cast<std::uint64_t>(pBlockCount));
}

inline constexpr bool IsValidArchiveSize(std::uint64_t pArchiveSizeBytes) {
  if (pArchiveSizeBytes < ComputeArchiveSizeForBlockCount(kMinimumArchiveBlockCount)) {
    return false;
  }
  const std::uint64_t aPayloadBytes = pArchiveSizeBytes - static_cast<std::uint64_t>(kArchiveHeaderLength);
  return (aPayloadBytes % static_cast<std::uint64_t>(kBlockSizeL3)) == 0u;
}

struct ArchiveHeader {
  std::uint32_t mMagic = kMagicHeaderBytes;
  std::uint16_t mVersionMajor = static_cast<std::uint16_t>(kMajorVersion & 0xFFFFu);
  std::uint16_t mVersionMinor = static_cast<std::uint16_t>(kMinorVersion & 0xFFFFu);
  std::uint32_t mArchiveIndex = 0;
  std::uint32_t mArchiveCount = 0;
  std::uint32_t mPayloadLength = 0;
  std::uint8_t mRecordCountMod256 = 0;
  std::uint8_t mFolderCountMod256 = 0;
  std::uint16_t mReserved16 = 0;
  std::uint64_t mReservedA = 0;
  std::uint64_t mReservedB = 0;
};

struct Checksum {
  std::uint64_t mWord1 = 0;
  std::uint64_t mWord2 = 0;
  std::uint64_t mWord3 = 0;
  std::uint64_t mWord4 = 0;
};

struct SkipRecord {
  std::uint16_t mArchiveDistance = 0;
  std::uint16_t mBlockDistance = 0;
  std::uint32_t mByteDistance = 0;
};

struct RecoveryHeader {
  Checksum mChecksum{};
  SkipRecord mSkip{};
};

struct L3BlockBuffer {
  unsigned char mBuffer[BLOCK_SIZE_L3] = {};

  unsigned char* Data() {
    return mBuffer;
  }

  const unsigned char* Data() const {
    return mBuffer;
  }
};

static_assert(sizeof(ArchiveHeader) == ARCHIVE_HEADER_LENGTH,
              "ArchiveHeader size must match ARCHIVE_HEADER_LENGTH.");
static_assert(sizeof(RecoveryHeader) == RECOVERY_HEADER_LENGTH,
              "RecoveryHeader size must match RECOVERY_HEADER_LENGTH.");
static_assert(kPayloadBytesPerL3 > 0, "BLOCK_SIZE_L3 must exceed RECOVERY_HEADER_LENGTH.");
static_assert(IsValidArchiveBlockCount(1u), "Archive block count must be >= 1.");

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
