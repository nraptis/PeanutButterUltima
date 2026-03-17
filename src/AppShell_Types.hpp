#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_TYPES_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_TYPES_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppShell_Common.hpp"
#include "Encryption/Crypt.hpp"

namespace peanutbutter {

class Logger {
 public:
  virtual ~Logger() = default;
  virtual void LogStatus(const std::string& pMessage) = 0;
  virtual void LogError(const std::string& pMessage) = 0;
  virtual void LogProgress(const ProgressInfo&) {}
};

class NullLogger final : public Logger {
 public:
  void LogStatus(const std::string&) override {}
  void LogError(const std::string&) override {}
};

class CapturingLogger final : public Logger {
 public:
  void LogStatus(const std::string& pMessage) override;
  void LogError(const std::string& pMessage) override;
  void LogProgress(const ProgressInfo& pProgress) override;

  const std::vector<std::string>& StatusMessages() const;
  const std::vector<std::string>& ErrorMessages() const;
  const std::vector<ProgressInfo>& ProgressEvents() const;

 private:
  std::vector<std::string> mStatusMessages;
  std::vector<std::string> mErrorMessages;
  std::vector<ProgressInfo> mProgressEvents;
};

enum class ErrorCode : std::uint32_t {
  kNone = 0u,
  kCanceled = 1u,
  kInvalidRequest = 2u,
  kFileSystem = 3u,
  kCrypt = 4u,
  kArchiveHeader = 5u,
  kGap001 = 6u,
  kBlockChecksum = 7u,
  kRecordParse = 8u,
  kRecoverExhausted = 9u,
  kInternal = 255u,
};

const char* ErrorCodeToString(ErrorCode pCode);
std::uint32_t ErrorCodeToUInt(ErrorCode pCode);

struct OperationResult {
  bool mSucceeded = false;
  bool mCanceled = false;
  ErrorCode mErrorCode = ErrorCode::kNone;
  std::string mFailureMessage;
};

struct BundleRequest {
  std::string mDestinationDirectory;
  std::string mSourceStem = "archive_data";
  std::string mArchivePrefix = "";
  std::string mArchiveSuffix = ".PBTR";
  std::string mPasswordOne;
  std::string mPasswordTwo;
  std::uint32_t mArchiveBlockCount = 1;
  bool mUseEncryption = false;
  EncryptionStrength mEncryptionStrength = EncryptionStrength::kHigh;
  CryptGenerator mCryptGenerator;
};

struct UnbundleRequest {
  std::string mDestinationDirectory;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
  bool mRecoverMode = false;
  CryptGenerator mCryptGenerator;
};

struct ValidateRequest {
  std::string mLeftDirectory;
  std::string mRightDirectory;
};

struct SourceEntry {
  std::string mSourcePath;
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::uint64_t mFileLength = 0;
};

struct BundleArchivePlan {
  std::size_t mArchiveOrdinal = 0;
  std::uint32_t mArchiveIndex = 0;
  std::uint32_t mBlockCount = 0;
  std::uint32_t mPayloadBytes = 0;
  std::uint8_t mRecordCountMod256 = 0;
  std::uint8_t mFolderCountMod256 = 0;
  std::string mArchivePath;
};

struct ArchiveFileBox {
  std::uint64_t mPayloadStart = 0;
  std::uint64_t mPayloadLength = 0;
  std::uint32_t mSequenceJumber = 0;
  bool mEmpty = true;
};

struct BundleDiscovery {
  std::vector<SourceEntry> mResolvedEntries;
  std::vector<BundleArchivePlan> mArchives;
  std::vector<std::uint64_t> mRecordStartLogicalOffsets;
  std::uint64_t mArchiveFamilyId = 0;
  std::uint64_t mTotalLogicalBytes = 0;
  std::uint64_t mTotalFileBytes = 0;
  std::size_t mFileCount = 0;
  std::size_t mFolderCount = 0;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_TYPES_HPP_
