#include "AppShell_Common.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "AppShell_Types.hpp"

namespace peanutbutter {
namespace {

inline std::uint64_t CurrentSteadySeconds() {
  const auto aNow = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(aNow).count());
}

inline void AppendDurationPart(std::ostringstream& pStream,
                               bool& pHasParts,
                               std::uint64_t pValue,
                               const char* pSingular,
                               const char* pPlural) {
  if (pValue == 0u) {
    return;
  }
  if (pHasParts) {
    pStream << " ";
  }
  pStream << pValue << " " << (pValue == 1u ? pSingular : pPlural);
  pHasParts = true;
}

}  // namespace

std::string FormatHumanDurationSeconds(std::uint64_t pTotalSeconds) {
  const std::uint64_t aDays = pTotalSeconds / 86400u;
  pTotalSeconds %= 86400u;
  const std::uint64_t aHours = pTotalSeconds / 3600u;
  pTotalSeconds %= 3600u;
  const std::uint64_t aMinutes = pTotalSeconds / 60u;
  const std::uint64_t aSeconds = pTotalSeconds % 60u;

  std::ostringstream aOut;
  bool aHasParts = false;
  AppendDurationPart(aOut, aHasParts, aDays, "day", "days");
  AppendDurationPart(aOut, aHasParts, aHours, "hour", "hours");
  AppendDurationPart(aOut, aHasParts, aMinutes, "minute", "minutes");
  if (!aHasParts || aSeconds > 0u) {
    AppendDurationPart(aOut, aHasParts, aSeconds, "second", "seconds");
  }
  return aOut.str();
}

std::string FormatHumanBytes(std::uint64_t pBytes) {
  static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
  double aValue = static_cast<double>(pBytes);
  std::size_t aUnit = 0u;
  while (aValue >= 1024.0 && aUnit + 1u < (sizeof(kUnits) / sizeof(kUnits[0]))) {
    aValue /= 1024.0;
    ++aUnit;
  }

  std::ostringstream aOut;
  if (aUnit == 0u) {
    aOut << pBytes << " " << kUnits[aUnit];
    return aOut.str();
  }
  aOut << std::fixed << std::setprecision(aValue >= 10.0 ? 0 : 1) << aValue << " " << kUnits[aUnit];
  return aOut.str();
}

ElapsedTimeLogGate::ElapsedTimeLogGate(const std::string& pModeLabel, Logger& pLogger)
    : mModeLabel(pModeLabel),
      mLogger(&pLogger),
      mStartSeconds(CurrentSteadySeconds()),
      mNextLogSeconds(mStartSeconds + static_cast<std::uint64_t>(kElapsedTimeLogIntervalSeconds)) {}

void ElapsedTimeLogGate::MaybeLog() {
  if (mLogger == nullptr || kElapsedTimeLogIntervalSeconds == 0u) {
    return;
  }
  const std::uint64_t aNowSeconds = CurrentSteadySeconds();
  if (aNowSeconds < mNextLogSeconds) {
    return;
  }

  const std::uint64_t aElapsed = aNowSeconds >= mStartSeconds ? (aNowSeconds - mStartSeconds) : 0u;
  mLogger->LogStatus("[" + mModeLabel + "] Time elapsed is " + FormatHumanDurationSeconds(aElapsed) + ".");

  const std::uint64_t aStep = static_cast<std::uint64_t>(kElapsedTimeLogIntervalSeconds);
  while (mNextLogSeconds <= aNowSeconds) {
    mNextLogSeconds += aStep;
  }
}

CancelCoordinator::CancelCoordinator(const std::atomic_bool* pCancelRequested,
                                     Logger* pLogger,
                                     const std::string& pModeName,
                                     double pGraceSeconds)
    : mCancelRequested(pCancelRequested),
      mLogger(pLogger),
      mModeName(pModeName),
      mGraceSeconds(pGraceSeconds > 0.0 ? pGraceSeconds : 0.0) {}

bool CancelCoordinator::HasCancelToken() const {
  return mCancelRequested != nullptr;
}

bool CancelCoordinator::IsCancelRequested() const {
  return mCancelRequested != nullptr && mCancelRequested->load();
}

void CancelCoordinator::SetActivity(CancelActivityKind pKind, const std::string& pPath) {
  mActivity = pKind;
  mActivePath = pPath;
  if (!pPath.empty()) {
    mMostRecentPath = pPath;
  }
}

void CancelCoordinator::SetReadingPath(const std::string& pPath) {
  SetActivity(CancelActivityKind::kReading, pPath);
}

void CancelCoordinator::SetWritingPath(const std::string& pPath) {
  SetActivity(CancelActivityKind::kWriting, pPath);
}

void CancelCoordinator::ClearActivity() {
  mActivity = CancelActivityKind::kNone;
  mActivePath.clear();
}

void CancelCoordinator::NoteFinishedWriting(const std::string& pPath) {
  if (!pPath.empty()) {
    mMostRecentPath = pPath;
  }
  if (mSawCancel && mLogger != nullptr && !pPath.empty()) {
    mLogger->LogStatus("[Cancel] Finished writing: " + PathLabel(pPath) + ".");
  }
}

bool CancelCoordinator::ShouldCancelNow() {
  if (!IsCancelRequested()) {
    return false;
  }

  const auto aNow = std::chrono::steady_clock::now();
  if (!mSawCancel) {
    mSawCancel = true;
    mCancelSeenAt = aNow;
    mLastWaitLogAt = aNow - std::chrono::seconds(2);
    if (mLogger != nullptr) {
      mLogger->LogStatus("[Cancel] Cancellation request received... Waiting for tasks to finish...");
    }
  }

  if (mActivity == CancelActivityKind::kNone) {
    return true;
  }

  if (!mLoggedTryFinish && mLogger != nullptr) {
    const std::string aAction = (mActivity == CancelActivityKind::kWriting) ? "writing" : "reading";
    mLogger->LogStatus("[Cancel] Trying to finish " + aAction + " " + PathLabel(mActivePath) + "...");
    mLoggedTryFinish = true;
  }

  const double aElapsedSeconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(aNow - mCancelSeenAt).count() / 1000.0;
  const double aRemainingSeconds = std::max(0.0, mGraceSeconds - aElapsedSeconds);
  if (aElapsedSeconds < mGraceSeconds) {
    if (mLogger != nullptr && (aNow - mLastWaitLogAt) >= std::chrono::seconds(1)) {
      const std::string aAction = (mActivity == CancelActivityKind::kWriting) ? "writing" : "reading";
      mLogger->LogStatus("[Cancel] Waiting " + FormatSeconds(aRemainingSeconds) +
                         " more seconds to finish " + aAction + "...");
      mLastWaitLogAt = aNow;
    }
    return false;
  }

  if (!mLoggedTimeout && mLogger != nullptr) {
    mLogger->LogStatus("[Cancel] Waited " + FormatSeconds(aElapsedSeconds) + " seconds and could not finish");
    if (mActivity == CancelActivityKind::kWriting) {
      const std::string aPathForPartial = !mActivePath.empty() ? mActivePath : mMostRecentPath;
      if (!aPathForPartial.empty()) {
        mLogger->LogStatus("[Cancel] Partial file exists: " + PathLabel(aPathForPartial) + ".");
      }
    }
    mLoggedTimeout = true;
  }
  return true;
}

bool CancelCoordinator::IsWithinGracePeriod() const {
  if (!mSawCancel) {
    return false;
  }
  const auto aNow = std::chrono::steady_clock::now();
  const double aElapsedSeconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(aNow - mCancelSeenAt).count() / 1000.0;
  return aElapsedSeconds < mGraceSeconds;
}

void CancelCoordinator::LogEndingJob() {
  if (mLoggedEnding || mLogger == nullptr) {
    return;
  }
  mLogger->LogStatus("[Cancel] Ending job, thank you!");
  mLoggedEnding = true;
}

void CancelCoordinator::LogModeCancelled(const std::string& pModeName) {
  if (mLogger == nullptr) {
    return;
  }
  if (std::find(mLoggedModeMessages.begin(), mLoggedModeMessages.end(), pModeName) != mLoggedModeMessages.end()) {
    return;
  }
  mLoggedModeMessages.push_back(pModeName);
  mLogger->LogStatus("[" + pModeName + "][Mode] " + pModeName + " was cancelled!");
}

const std::string& CancelCoordinator::MostRecentPath() const {
  return mMostRecentPath;
}

std::string CancelCoordinator::PathLabel(const std::string& pPath) const {
  if (pPath.empty()) {
    return std::string("<unknown>");
  }
  std::size_t aEnd = pPath.size();
  while (aEnd > 0u && (pPath[aEnd - 1u] == '/' || pPath[aEnd - 1u] == '\\')) {
    --aEnd;
  }
  if (aEnd == 0u) {
    return pPath;
  }
  const std::size_t aSlash = pPath.find_last_of("/\\", aEnd - 1u);
  if (aSlash == std::string::npos || aSlash + 1u >= aEnd) {
    return pPath.substr(0u, aEnd);
  }
  return pPath.substr(aSlash + 1u, aEnd - (aSlash + 1u));
}

std::string CancelCoordinator::FormatSeconds(double pSeconds) const {
  std::ostringstream aOut;
  aOut << std::fixed << std::setprecision(2) << std::max(0.0, pSeconds);
  return aOut.str();
}

}  // namespace peanutbutter
