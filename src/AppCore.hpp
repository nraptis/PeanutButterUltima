#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Encryption/Crypt.hpp"
#include "FormatConstants.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

inline constexpr std::size_t kArchiveHeaderLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
inline constexpr std::uint32_t kMagicHeaderBytes = peanutbutter::MAGIC_HEADER_BYTES;
inline constexpr std::uint32_t kMagicFooterBytes = peanutbutter::MAGIC_FOOTER_BYTES;
inline constexpr std::uint32_t kMajorVersion = peanutbutter::MAJOR_VERSION;
inline constexpr std::uint32_t kMinorVersion = peanutbutter::MINOR_VERSION;
inline constexpr std::size_t kPackAndUnpackLogThrottleBlockSize = 100;
inline constexpr std::size_t kPackAndUnpackLogThrottleIgnoreLast = 10;

class Logger {
 public:
  virtual ~Logger() = default;
  virtual void LogStatus(const std::string& pMessage) = 0;
  virtual void LogError(const std::string& pMessage) = 0;
};

class FunctionLogger final : public Logger {
 public:
  explicit FunctionLogger(std::function<void(const std::string&, bool)> pSink);

  void LogStatus(const std::string& pMessage) override;
  void LogError(const std::string& pMessage) override;

 private:
  std::function<void(const std::string&, bool)> mSink;
};

class CapturingLogger final : public Logger {
 public:
  void LogStatus(const std::string& pMessage) override;
  void LogError(const std::string& pMessage) override;

  const std::vector<std::string>& StatusMessages() const;
  const std::vector<std::string>& ErrorMessages() const;

 private:
  std::vector<std::string> mStatusMessages;
  std::vector<std::string> mErrorMessages;
};

enum class DestinationAction {
  Cancel,
  Merge,
  Clear,
};

enum class PreflightSignal {
  GreenLight,
  YellowLight,
  RedLight,
};

struct PreflightResult {
  PreflightSignal mSignal = PreflightSignal::RedLight;
  std::string mTitle;
  std::string mMessage;
};

struct OperationResult {
  bool mSucceeded = false;
  std::string mTitle;
  std::string mMessage;
};

enum class UnpackIntegerFailure {
  kNone = 0,
  kFileNameLengthGreaterThanMaxValidFilePathLength = 1,
  kFileNameLengthGreaterThanRemainingBytesInUnpackJob = 2,
  kFileNameLengthIsZero = 3,
  kFileNameLengthLandsInsideRecoveryHeader = 4,
  kFileNameLengthLandsInsideArchiveHeader = 5,
  kFileDataLengthGreaterThanRemainingBytesInUnpackJob = 6,
  kFileDataLengthLandsInsideRecoveryHeader = 7,
  kFileDataLengthLandsInsideArchiveHeader = 8,
  kNonFirstRecoveryNextFileDistanceGreaterThanRemainingBytesInUnpackJob = 10,
  kNonFirstRecoveryNextFileDistanceIsZero = 11,
  kNonFirstRecoveryNextFileDistanceLandsInsideRecoveryHeader = 12,
  kNonFirstRecoveryNextFileDistanceLandsInsideArchiveHeader = 13,
  kRecoverySpecialFlowDistanceLandsOutsideSelectedArchive = 14,
  kManifestFolderLengthIsZero = 15,
  kManifestFolderLengthGreaterThanRemainingBytesInUnpackJob = 16,
  kManifestFolderLengthGreaterThanMaxValidFilePathLength = 17,
  kDanglingArchives = 18,
  kDanglingBytes = 19,
  kUnknown = 1000,
};

struct UnpackFailureInfo {
  UnpackIntegerFailure mCode = UnpackIntegerFailure::kNone;
  std::string mMessage;
};

struct ArchiveHeader {
  bool mRecoveryEnabled = false;
  std::uint64_t mSequence = 0;
  std::array<unsigned char, 8> mArchiveIdentifier{};
};

struct ArchiveHeaderRecord {
  std::string mPath;
  std::string mName;
  ArchiveHeader mHeader;
  std::size_t mPayloadLength = 0;
};

struct RuntimeSettings {
  std::size_t mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * 3);
  std::size_t mLogThrottleBlockSize = kPackAndUnpackLogThrottleBlockSize;
  std::size_t mLogThrottleIgnoreLast = kPackAndUnpackLogThrottleIgnoreLast;
};

struct BundleRequest {
  std::string mSourceDirectory;
  std::string mDestinationDirectory;
  std::string mArchivePrefix;
  std::string mArchiveSuffix;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
};

struct UnbundleRequest {
  std::string mArchiveDirectory;
  std::string mDestinationDirectory;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
};

struct RecoverRequest {
  std::string mArchiveDirectory;
  std::string mRecoveryStartFilePath;
  std::string mDestinationDirectory;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
};

struct ValidateRequest {
  std::string mLeftDirectory;
  std::string mRightDirectory;
};

class ApplicationCore {
 public:
  ApplicationCore(FileSystem& pFileSystem, Crypt& pCrypt, Logger& pLogger, RuntimeSettings pSettings);

  PreflightResult CheckBundle(const BundleRequest& pRequest) const;
  PreflightResult CheckUnbundle(const UnbundleRequest& pRequest) const;
  PreflightResult CheckRecover(const RecoverRequest& pRequest) const;
  PreflightResult CheckValidate(const ValidateRequest& pRequest) const;

  void SetSettings(RuntimeSettings pSettings);

  OperationResult RunBundle(const BundleRequest& pRequest, DestinationAction pAction);
  OperationResult RunUnbundle(const UnbundleRequest& pRequest, DestinationAction pAction);
  OperationResult RunRecover(const RecoverRequest& pRequest, DestinationAction pAction);
  OperationResult RunValidate(const ValidateRequest& pRequest);

 private:
  FileSystem& mFileSystem;
  Crypt& mCrypt;
  Logger& mLogger;
  RuntimeSettings mSettings;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_HPP_
