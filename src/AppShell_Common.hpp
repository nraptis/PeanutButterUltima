#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_COMMON_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_COMMON_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "PeanutButter.hpp"

namespace peanutbutter {

class Logger;

std::string FormatHumanDurationSeconds(std::uint64_t pTotalSeconds);
std::string FormatHumanBytes(std::uint64_t pBytes);

class ElapsedTimeLogGate final {
 public:
  ElapsedTimeLogGate(const std::string& pModeLabel, Logger& pLogger);
  void MaybeLog();

 private:
  std::string mModeLabel;
  Logger* mLogger = nullptr;
  std::uint64_t mStartSeconds = 0u;
  std::uint64_t mNextLogSeconds = 0u;
};

enum class CancelActivityKind {
  kNone,
  kReading,
  kWriting,
};

class CancelCoordinator final {
 public:
  CancelCoordinator(const std::atomic_bool* pCancelRequested,
                    Logger* pLogger,
                    const std::string& pModeName,
                    double pGraceSeconds = 3.0);

  bool HasCancelToken() const;
  bool IsCancelRequested() const;

  void SetActivity(CancelActivityKind pKind, const std::string& pPath);
  void SetReadingPath(const std::string& pPath);
  void SetWritingPath(const std::string& pPath);
  void ClearActivity();
  void NoteFinishedWriting(const std::string& pPath);

  bool ShouldCancelNow();
  bool IsWithinGracePeriod() const;

  void LogEndingJob();
  void LogModeCancelled(const std::string& pModeName);

  const std::string& MostRecentPath() const;

 private:
  std::string PathLabel(const std::string& pPath) const;
  std::string FormatSeconds(double pSeconds) const;

 private:
  const std::atomic_bool* mCancelRequested = nullptr;
  Logger* mLogger = nullptr;
  std::string mModeName;
  double mGraceSeconds = 10.0;
  bool mSawCancel = false;
  bool mLoggedTryFinish = false;
  bool mLoggedTimeout = false;
  bool mLoggedEnding = false;
  std::vector<std::string> mLoggedModeMessages;
  std::chrono::steady_clock::time_point mCancelSeenAt{};
  std::chrono::steady_clock::time_point mLastWaitLogAt{};
  CancelActivityKind mActivity = CancelActivityKind::kNone;
  std::string mActivePath;
  std::string mMostRecentPath;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_COMMON_HPP_
