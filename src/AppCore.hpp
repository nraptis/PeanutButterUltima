#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_HPP_

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Encryption/Crypt.hpp"
#include "FormatConstants.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {

inline constexpr std::size_t MAX_ARCHIVE_BLOCK_COUNT = 2000;

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

class SessionLogger final : public Logger {
 public:
  explicit SessionLogger(std::function<void(const std::string&, bool)> pSink = {});

  void SetFileSystem(FileSystem* pFileSystem);
  void BeginSession(const std::string& pFilePath);
  void EndSession();

  void LogStatus(const std::string& pMessage) override;
  void LogError(const std::string& pMessage) override;

 private:
  void LogLine(const std::string& pMessage, bool pIsError);

  FileSystem* mFileSystem = nullptr;
  std::function<void(const std::string&, bool)> mSink;
  std::string mFilePath;
  std::mutex mMutex;
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

struct RuntimeSettings {
  std::size_t mArchiveFileLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * 3);
  std::size_t mLogThrottleBlockSize = 100;
  std::size_t mLogThrottleIgnoreLast = 10;
};

struct BundleRequest {
  // File-or-folder source path.
  std::string mSourceDirectory;
  std::string mDestinationDirectory;
  std::string mArchivePrefix;
  std::string mArchiveSuffix;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  std::size_t mArchiveBlockCount = 3;
  bool mUseEncryption = false;
};

struct UnbundleRequest {
  // File-or-folder archive input path.
  std::string mArchiveDirectory;
  std::string mDestinationDirectory;
  std::string mPasswordOne;
  std::string mPasswordTwo;
  bool mUseEncryption = false;
};

struct RecoverRequest {
  // File-or-folder archive input path.
  std::string mArchiveDirectory;
  // Optional file-or-folder override for recovery start selection.
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
