#ifndef PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
#define PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_

#include <cstddef>
#include <cstdint>

#define PASSWORD_EXPANDED_SIZE 7680
#define PASSWORD_BALLOONED_SIZE 15360
#define BLOCK_GRANULARITY 192u
#define ARCHIVE_HEADER_LENGTH 48u
#define RECOVERY_HEADER_LENGTH 48u
#define BLOCK_SIZE_L1 261120u
#define BLOCK_SIZE_L2 522240u
#define BLOCK_SIZE_L3 1044480u

#define MAX_VALID_FILE_PATH_LENGTH 2048u
#define MAX_ARCHIVE_COUNT 65535u
#define MAX_BLOCKS_PER_ARCHIVE 2048u

#if defined(PEANUT_BUTTER_ULTIMA_TEST_BUILD)
#define TEST_SEED 777u /*T-SEED*/
#define TEST_LOOPS 1u /*T-LOOPS*/
#define TEST_BLOCK_COUNT 2u /*T-BLOCKS*/
#endif

extern unsigned char gTableL1_A[BLOCK_SIZE_L1];
extern unsigned char gTableL1_B[BLOCK_SIZE_L1];
extern unsigned char gTableL1_C[BLOCK_SIZE_L1];
extern unsigned char gTableL1_D[BLOCK_SIZE_L1];
extern unsigned char gTableL1_E[BLOCK_SIZE_L1];
extern unsigned char gTableL1_F[BLOCK_SIZE_L1];
extern unsigned char gTableL1_G[BLOCK_SIZE_L1];
extern unsigned char gTableL1_H[BLOCK_SIZE_L1];

extern unsigned char gTableL2_A[BLOCK_SIZE_L2];
extern unsigned char gTableL2_B[BLOCK_SIZE_L2];
extern unsigned char gTableL2_C[BLOCK_SIZE_L2];
extern unsigned char gTableL2_D[BLOCK_SIZE_L2];
extern unsigned char gTableL2_E[BLOCK_SIZE_L2];
extern unsigned char gTableL2_F[BLOCK_SIZE_L2];

extern unsigned char gTableL3_A[BLOCK_SIZE_L3];
extern unsigned char gTableL3_B[BLOCK_SIZE_L3];
extern unsigned char gTableL3_C[BLOCK_SIZE_L3];
extern unsigned char gTableL3_D[BLOCK_SIZE_L3];

namespace peanutbutter {

inline constexpr std::uint32_t kMagicHeaderBytes = 0xDECAFBADu;
inline constexpr std::uint32_t kMagicFooterBytes = 0xF01DAB1Eu;
inline constexpr std::uint32_t kMajorVersion = 1u;
inline constexpr std::uint32_t kMinorVersion = 6u;

inline constexpr std::uint32_t kElapsedTimeLogIntervalSeconds = 300u;
inline constexpr std::uint32_t kProgressCountLogIntervalDefault = 10000u;
inline constexpr std::uint64_t kProgressByteLogIntervalDefault = 250u * 1024u * 1024u;
inline constexpr std::uint64_t kProgressByteLogIntervalValidation = 1024u * 1024u * 1024u;
inline constexpr std::size_t kValidationLogCapFiles = 40u;
inline constexpr std::size_t kValidationLogCapFolders = 40u;
inline constexpr std::size_t kDebugLogLineCharacterLimit = 512u;
inline constexpr std::size_t kDebugLogLineLimit = 5000u;

inline constexpr std::size_t kBlockGranularity = static_cast<std::size_t>(BLOCK_GRANULARITY);
inline constexpr std::size_t kBlockSizeL1 = static_cast<std::size_t>(BLOCK_SIZE_L1);
inline constexpr std::size_t kBlockSizeL2 = static_cast<std::size_t>(BLOCK_SIZE_L2);
inline constexpr std::size_t kBlockSizeL3 = static_cast<std::size_t>(BLOCK_SIZE_L3);
inline constexpr std::size_t kPasswordExpandedSize = static_cast<std::size_t>(PASSWORD_EXPANDED_SIZE);
inline constexpr std::size_t kPasswordBalloonedSize = static_cast<std::size_t>(PASSWORD_BALLOONED_SIZE);
inline constexpr std::size_t kArchiveHeaderLength = static_cast<std::size_t>(ARCHIVE_HEADER_LENGTH);
inline constexpr std::size_t kRecoveryHeaderLength = static_cast<std::size_t>(RECOVERY_HEADER_LENGTH);
inline constexpr std::size_t kMaxPathLength = static_cast<std::size_t>(MAX_VALID_FILE_PATH_LENGTH);
inline constexpr std::uint32_t kMaxArchiveCount = static_cast<std::uint32_t>(MAX_ARCHIVE_COUNT);
inline constexpr std::uint32_t kMaxBlocksPerArchive = static_cast<std::uint32_t>(MAX_BLOCKS_PER_ARCHIVE);
inline constexpr std::size_t kL1TableCount = 8u;
inline constexpr std::size_t kL2TableCount = 6u;
inline constexpr std::size_t kL3TableCount = 4u;
inline constexpr std::size_t kExpandedBuffersPerL1 = kBlockSizeL1 / kPasswordExpandedSize;
inline constexpr std::size_t kBalloonedBuffersPerL1 = kBlockSizeL1 / kPasswordBalloonedSize;
inline constexpr std::size_t kL1BlocksPerL2 = kBlockSizeL2 / kBlockSizeL1;
inline constexpr std::size_t kL2BlocksPerL3 = kBlockSizeL3 / kBlockSizeL2;

inline constexpr std::size_t SB_CIPHER_LENGTH_GRANULARITY = kBlockGranularity;
inline constexpr std::size_t EB_MAX_LENGTH = 48u;
inline constexpr std::size_t EB_BLOCK_SIZE_08 = 8u;
inline constexpr std::size_t EB_BLOCK_SIZE_12 = 12u;
inline constexpr std::size_t EB_BLOCK_SIZE_16 = 16u;
inline constexpr std::size_t EB_BLOCK_SIZE_24 = 24u;
inline constexpr std::size_t EB_BLOCK_SIZE_32 = 32u;
inline constexpr std::size_t EB_BLOCK_SIZE_48 = 48u;
inline constexpr std::size_t SB_PLAIN_TEXT_HEADER_LENGTH = 36u;
inline constexpr std::size_t SB_RECOVERY_HEADER_LENGTH = 6u;
inline constexpr std::size_t SB_PAYLOAD_SIZE = kBlockSizeL1 - SB_RECOVERY_HEADER_LENGTH;
inline constexpr std::size_t SB_L1_LENGTH = kBlockSizeL1;
inline constexpr std::size_t SB_L2_LENGTH = kBlockSizeL2;
inline constexpr std::size_t SB_L3_LENGTH = kBlockSizeL3;
inline constexpr std::size_t kPayloadBytesPerL3 =
    kBlockSizeL3 > kRecoveryHeaderLength ? kBlockSizeL3 - kRecoveryHeaderLength : 0;
inline constexpr std::uint32_t kMinimumArchiveBlockCount = 1u;

inline constexpr bool IsValidArchiveBlockCount(std::uint32_t pBlockCount) {
  return pBlockCount >= kMinimumArchiveBlockCount && pBlockCount <= kMaxBlocksPerArchive;
}

bool BuildL1FromExpandedChunks(unsigned char* pDestination, const unsigned char* pChunks, std::size_t pChunkCount);
bool BuildL1FromBalloonedChunks(unsigned char* pDestination, const unsigned char* pChunks, std::size_t pChunkCount);
bool BuildL2FromL1Tables(unsigned char* pDestination, const unsigned char* pTableA, const unsigned char* pTableB);
bool BuildL3FromL2Tables(unsigned char* pDestination, const unsigned char* pTableA, const unsigned char* pTableB);

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
static_assert(PASSWORD_BALLOONED_SIZE == (PASSWORD_EXPANDED_SIZE * 2),
              "PASSWORD_BALLOONED_SIZE must equal 2 * PASSWORD_EXPANDED_SIZE.");
static_assert((kBlockSizeL1 % kBlockGranularity) == 0u, "BLOCK_SIZE_L1 must align to BLOCK_GRANULARITY.");
static_assert((kBlockSizeL2 % kBlockGranularity) == 0u, "BLOCK_SIZE_L2 must align to BLOCK_GRANULARITY.");
static_assert((kBlockSizeL3 % kBlockGranularity) == 0u, "BLOCK_SIZE_L3 must align to BLOCK_GRANULARITY.");
static_assert((kBlockSizeL1 % kPasswordExpandedSize) == 0u,
              "BLOCK_SIZE_L1 must be divisible by PASSWORD_EXPANDED_SIZE.");
static_assert((kBlockSizeL1 % kPasswordBalloonedSize) == 0u,
              "BLOCK_SIZE_L1 must be divisible by PASSWORD_BALLOONED_SIZE.");
static_assert(kExpandedBuffersPerL1 == 34u, "BLOCK_SIZE_L1 must hold exactly 34 expanded buffers.");
static_assert(kBalloonedBuffersPerL1 == 17u, "BLOCK_SIZE_L1 must hold exactly 17 ballooned buffers.");
static_assert(kL1BlocksPerL2 == 2u, "BLOCK_SIZE_L2 must hold exactly 2 L1 tables.");
static_assert(kL2BlocksPerL3 == 2u, "BLOCK_SIZE_L3 must hold exactly 2 L2 tables.");
static_assert(kBlockSizeL2 == (kBlockSizeL1 * 2u), "BLOCK_SIZE_L2 must equal 2 * BLOCK_SIZE_L1.");
static_assert(kBlockSizeL3 == (kBlockSizeL2 * 2u), "BLOCK_SIZE_L3 must equal 2 * BLOCK_SIZE_L2.");
static_assert(kPayloadBytesPerL3 > 0, "BLOCK_SIZE_L3 must exceed RECOVERY_HEADER_LENGTH.");
static_assert(IsValidArchiveBlockCount(1u), "Archive block count must be >= 1.");
static_assert(SB_L1_LENGTH % EB_MAX_LENGTH == 0u, "SB_L1_LENGTH must be a multiple of EB_MAX_LENGTH.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_08 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_08.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_12 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_12.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_16 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_16.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_24 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_24.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_32 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_32.");
static_assert(SB_L1_LENGTH % EB_BLOCK_SIZE_48 == 0u, "SB_L1_LENGTH must be divisible by EB_BLOCK_SIZE_48.");
static_assert(SB_L1_LENGTH % SB_CIPHER_LENGTH_GRANULARITY == 0u,
              "SB_L1_LENGTH must be divisible by SB_CIPHER_LENGTH_GRANULARITY.");

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_PEANUT_BUTTER_HPP_
