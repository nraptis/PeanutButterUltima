#include "QtAppController.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <filesystem>
#include <string_view>
#include <thread>

namespace peanutbutter {

namespace {

bool BuildInReleaseMode() {
#if defined(PEANUT_BUTTER_ULTIMA_RELEASE_BUILD)
  return true;
#elif defined(PEANUT_BUTTER_ULTIMA_DEBUG_BUILD)
  return false;
#elif defined(NDEBUG)
  return true;
#else
  return false;
#endif
}

bool HasPathSeparator(const std::string_view pPath) {
  return pPath.find('/') != std::string::npos || pPath.find('\\') != std::string::npos;
}

std::filesystem::path ResolveRuleBasePath() {
  if (!BuildInReleaseMode()) {
    return std::filesystem::current_path();
  }

  const std::filesystem::path aAppDir(QCoreApplication::applicationDirPath().toStdString());
  const std::filesystem::path aContentsDir = aAppDir.parent_path();
  const std::filesystem::path aBundlePath = aContentsDir.parent_path();
  if (aAppDir.filename() == "MacOS" &&
      aContentsDir.filename() == "Contents" &&
      aBundlePath.extension() == ".app") {
    // In a macOS app bundle, resolve simple path tokens next to the .app package.
    return aBundlePath.parent_path();
  }
  return aAppDir;
}

std::string ResolvePathToken(const std::string_view pPath) {
  if (pPath.empty() || HasPathSeparator(pPath)) {
    return std::string(pPath);
  }

  const std::filesystem::path aBase = ResolveRuleBasePath();
  return (aBase / pPath).string();
}

std::string ResolveRecoveryFilePath(const std::string_view pPath) {
  if (pPath.empty() || HasPathSeparator(pPath)) {
    return std::string(pPath);
  }

  const std::filesystem::path aCandidate = ResolveRuleBasePath() / pPath;
  std::error_code aError;
  if (std::filesystem::is_regular_file(aCandidate, aError) && !aError) {
    return aCandidate.string();
  }
  return std::string(pPath);
}

BundleRequest ResolveRequestPaths(const BundleRequest& pRequest) {
  BundleRequest aRequest = pRequest;
  aRequest.mSourceDirectory = ResolvePathToken(aRequest.mSourceDirectory);
  aRequest.mDestinationDirectory = ResolvePathToken(aRequest.mDestinationDirectory);
  return aRequest;
}

UnbundleRequest ResolveRequestPaths(const UnbundleRequest& pRequest) {
  UnbundleRequest aRequest = pRequest;
  aRequest.mArchiveDirectory = ResolvePathToken(aRequest.mArchiveDirectory);
  aRequest.mDestinationDirectory = ResolvePathToken(aRequest.mDestinationDirectory);
  return aRequest;
}

RecoverRequest ResolveRequestPaths(const RecoverRequest& pRequest) {
  RecoverRequest aRequest = pRequest;
  aRequest.mArchiveDirectory = ResolvePathToken(aRequest.mArchiveDirectory);
  aRequest.mRecoveryStartFilePath = ResolveRecoveryFilePath(aRequest.mRecoveryStartFilePath);
  aRequest.mDestinationDirectory = ResolvePathToken(aRequest.mDestinationDirectory);
  return aRequest;
}

ValidateRequest ResolveRequestPaths(const ValidateRequest& pRequest) {
  ValidateRequest aRequest = pRequest;
  aRequest.mLeftDirectory = ResolvePathToken(aRequest.mLeftDirectory);
  aRequest.mRightDirectory = ResolvePathToken(aRequest.mRightDirectory);
  return aRequest;
}

DestinationAction ResolveDestinationAction(AppShell& pShell,
                                           const PreflightResult& pPreflight,
                                           const std::string& pOperationName,
                                           const std::string& pDestinationPath) {
  if (pPreflight.mSignal == PreflightSignal::YellowLight) {
    return pShell.PromptDestinationAction(pOperationName, pDestinationPath);
  }
  return DestinationAction::Merge;
}

}  // namespace

QtAppController::QtAppController(AppShell& pShell, ApplicationCore& pCore, QObject* pParent)
    : QObject(pParent),
      mShell(pShell),
      mCore(pCore) {}

bool QtAppController::IsBusy() const {
  return mBusy;
}

template <typename tOperation>
void QtAppController::LaunchOperation(tOperation pOperation) {
  QPointer<QtAppController> aSelf(this);
  std::thread aWorker([aSelf, aOperation = std::move(pOperation)]() mutable {
    const OperationResult aResult = aOperation();
    if (aSelf == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        aSelf,
        [aSelf, aResult]() {
          if (aSelf != nullptr) {
            aSelf->FinishOperation(aResult);
          }
        },
        Qt::QueuedConnection);
  });
  aWorker.detach();
}

void QtAppController::FinishOperation(const OperationResult& pResult) {
  mShell.SetLoading(false);
  mBusy = false;
  if (!pResult.mSucceeded) {
    const OperationResult aResult = pResult;
    QTimer::singleShot(0, this, [this, aResult]() { mShell.ShowError(aResult.mTitle, aResult.mMessage); });
  }
}

template <typename tRequest, typename tCheck, typename tRun, typename tDestinationAccessor>
void QtAppController::TriggerFileFlow(const std::string& pOperationName,
                                     const tRequest& pRequest,
                                     tCheck pCheck,
                                     tRun pRun,
                                     tDestinationAccessor pDestinationAccessor) {
  const tRequest aResolvedRequest = ResolveRequestPaths(pRequest);
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = pCheck(aResolvedRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }
  const DestinationAction aAction =
      ResolveDestinationAction(mShell, aPreflight, pOperationName, pDestinationAccessor(aResolvedRequest));
  if (aAction == DestinationAction::Cancel) {
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, aResolvedRequest, aAction, pRun]() { return pRun(aResolvedRequest, aAction); });
}

void QtAppController::TriggerBundleFlow(const BundleRequest& pRequest) {
  TriggerFileFlow("Bundle",
                  pRequest,
                  [this](const BundleRequest& pValue) { return mCore.CheckBundle(pValue); },
                  [this](const BundleRequest& pValue, DestinationAction pAction) { return mCore.RunBundle(pValue, pAction); },
                  [](const BundleRequest& pValue) { return pValue.mDestinationDirectory; });
}

void QtAppController::TriggerUnbundleFlow(const UnbundleRequest& pRequest) {
  TriggerFileFlow("Unbundle",
                  pRequest,
                  [this](const UnbundleRequest& pValue) { return mCore.CheckUnbundle(pValue); },
                  [this](const UnbundleRequest& pValue, DestinationAction pAction) { return mCore.RunUnbundle(pValue, pAction); },
                  [](const UnbundleRequest& pValue) { return pValue.mDestinationDirectory; });
}

void QtAppController::TriggerSanityFlow(const ValidateRequest& pRequest) {
  const ValidateRequest aResolvedRequest = ResolveRequestPaths(pRequest);
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = mCore.CheckValidate(aResolvedRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, aResolvedRequest]() { return mCore.RunValidate(aResolvedRequest); });
}

void QtAppController::TriggerRecoverFlow(const RecoverRequest& pRequest) {
  TriggerFileFlow("Recover",
                  pRequest,
                  [this](const RecoverRequest& pValue) { return mCore.CheckRecover(pValue); },
                  [this](const RecoverRequest& pValue, DestinationAction pAction) { return mCore.RunRecover(pValue, pAction); },
                  [](const RecoverRequest& pValue) { return pValue.mDestinationDirectory; });
}

}  // namespace peanutbutter
