#ifndef PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
#define PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_

#include <cstddef>
#include <cstdint>

#define BLOCK_GRANULARITY 192u
#define ARCHIVE_HEADER_LENGTH 48u
#define RECOVERY_HEADER_LENGTH 48u
#define BLOCK_SIZE_L1 250176u
#define BLOCK_SIZE_L2 500352u
#define BLOCK_SIZE_L3 1000704u

#define MAX_VALID_FILE_PATH_LENGTH 2048u
#define MAX_ARCHIVE_COUNT 65535u
#define MAX_BLOCKS_PER_ARCHIVE 2048u

#if defined(PEANUT_BUTTER_ULTIMA_TEST_BUILD)
#define TEST_SEED 777u /*T-SEED*/
#define TEST_LOOPS 1u /*T-LOOPS*/
#define TEST_BLOCK_COUNT 2u /*T-BLOCKS*/
#endif


namespace peanutbutter {

inline constexpr std::uint32_t kMagicHeaderBytes = 0xDECAFBADu;
inline constexpr std::uint32_t kMagicFooterBytes = 0xF01DAB1Eu;
inline constexpr std::uint32_t kMajorVersion = 1u;
inline constexpr std::uint32_t kMinorVersion = 7u;
inline constexpr std::uint32_t kArchiverVersion = 1u;
inline constexpr std::uint32_t kPasswordExpanderVersion = 1u;
inline constexpr std::uint32_t kCipherStackVersion = 1u;

// Logging controls.
inline constexpr std::uint32_t kElapsedTimeLogIntervalSeconds = 300u;          // 5 minutes
inline constexpr std::uint32_t kProgressCountLogIntervalDefault = 10000u;      // grouped count logs
inline constexpr std::uint64_t kProgressByteLogIntervalDefault = 250u * 1024u * 1024u;  // 100MB
inline constexpr std::uint64_t kProgressByteLogIntervalValidation = 1024u * 1024u * 1024u;  // 1GB
inline constexpr std::size_t kValidationLogCapFiles = 40u;
inline constexpr std::size_t kValidationLogCapFolders = 40u;
inline constexpr std::size_t kDebugLogLineCharacterLimit = 512u;  // kept chars + "..."
inline constexpr std::size_t kDebugLogLineLimit = 5000u;           // ring-buffer lines in UI

inline constexpr std::size_t kBlockGranularity = static_cast<std::size_t>(BLOCK_GRANULARITY);
inline constexpr std::size_t kBlockSizeL1 = static_cast<std::size_t>(BLOCK_SIZE_L1);
inline constexpr std::size_t kBlockSizeL2 = static_cast<std::size_t>(BLOCK_SIZE_L2);
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

enum class EncryptionStrength : std::uint8_t {
  kHigh = 1u,
  kMedium = 2u,
  kLow = 3u,
};

enum class ExpansionStrength : std::uint8_t {
  kHigh = 1u,
  kMedium = 2u,
  kLow = 3u,
};

enum class DirtyType : std::uint8_t {
  kInvalid = 0u,
  kFinishedWithCancel = 1u,
  kFinishedWithError = 2u,
  kFinishedWithCancelAndError = 3u,
  kFinished = 4u,
};

struct ArchiveHeader {
  std::uint32_t mMagic = kMagicHeaderBytes;
  std::uint16_t mVersionMajor = static_cast<std::uint16_t>(kMajorVersion & 0xFFFFu);
  std::uint16_t mVersionMinor = static_cast<std::uint16_t>(kMinorVersion & 0xFFFFu);
  std::uint8_t mArchiverVersion = static_cast<std::uint8_t>(kArchiverVersion & 0xFFu);
  std::uint8_t mPasswordExpanderVersion =
      static_cast<std::uint8_t>(kPasswordExpanderVersion & 0xFFu);
  std::uint8_t mCipherStackVersion = static_cast<std::uint8_t>(kCipherStackVersion & 0xFFu);
  EncryptionStrength mEncryptionStrength = EncryptionStrength::kHigh;
  ExpansionStrength mExpansionStrength = ExpansionStrength::kHigh;
  std::uint8_t mRecordCountMod256 = 0;
  std::uint8_t mFolderCountMod256 = 0;
  DirtyType mDirtyType = DirtyType::kInvalid;
  std::uint32_t mArchiveIndex = 0;
  std::uint32_t mArchiveCount = 0;
  std::uint32_t mPayloadLength = 0;
  std::uint32_t mReserved32 = 0;
  std::uint64_t mReservedB = 0;
  std::uint64_t mArchiveFamilyId = 0;
};

struct Checksum {
  std::uint64_t mWord1 = 0;
  std::uint64_t mWord2 = 0;
  std::uint64_t mWord3 = 0;
  std::uint64_t mWord4 = 0;
  std::uint64_t mWord5 = 0;
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

class BlockBuffer final {
 public:
  BlockBuffer()
      : mBuffer(new unsigned char[BLOCK_SIZE_L3] {}) {}

  ~BlockBuffer() {
    delete[] mBuffer;
    mBuffer = nullptr;
  }

  BlockBuffer(const BlockBuffer&) = delete;
  BlockBuffer& operator=(const BlockBuffer&) = delete;

  BlockBuffer(BlockBuffer&& pOther) noexcept
      : mBuffer(pOther.mBuffer) {
    pOther.mBuffer = nullptr;
  }

  BlockBuffer& operator=(BlockBuffer&& pOther) noexcept {
    if (this == &pOther) {
      return *this;
    }
    delete[] mBuffer;
    mBuffer = pOther.mBuffer;
    pOther.mBuffer = nullptr;
    return *this;
  }

  unsigned char* Data() {
    return mBuffer;
  }

  const unsigned char* Data() const {
    return mBuffer;
  }

  unsigned char* mBuffer = nullptr;
};

static_assert(sizeof(ArchiveHeader) == ARCHIVE_HEADER_LENGTH,
              "ArchiveHeader size must match ARCHIVE_HEADER_LENGTH.");
static_assert(sizeof(RecoveryHeader) == RECOVERY_HEADER_LENGTH,
              "RecoveryHeader size must match RECOVERY_HEADER_LENGTH.");
static_assert((kBlockSizeL1 % kBlockGranularity) == 0u, "BLOCK_SIZE_L1 must align to BLOCK_GRANULARITY.");
static_assert((kBlockSizeL2 % kBlockGranularity) == 0u, "BLOCK_SIZE_L2 must align to BLOCK_GRANULARITY.");
static_assert((kBlockSizeL3 % kBlockGranularity) == 0u, "BLOCK_SIZE_L3 must align to BLOCK_GRANULARITY.");
static_assert(kBlockSizeL2 == (kBlockSizeL1 * 2u), "BLOCK_SIZE_L2 must equal 2 * BLOCK_SIZE_L1.");
static_assert(kBlockSizeL3 == (kBlockSizeL2 * 2u), "BLOCK_SIZE_L3 must equal 2 * BLOCK_SIZE_L2.");
static_assert(kPayloadBytesPerL3 > 0, "BLOCK_SIZE_L3 must exceed RECOVERY_HEADER_LENGTH.");
static_assert(IsValidArchiveBlockCount(1u), "Archive block count must be >= 1.");

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
