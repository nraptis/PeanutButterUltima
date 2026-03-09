#include "QtAppController.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <thread>

namespace peanutbutter::ultima {

namespace {

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

void QtAppController::TriggerBundleFlow(const BundleRequest& pRequest) {
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = mCore.CheckBundle(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }
  const DestinationAction aAction =
      ResolveDestinationAction(mShell, aPreflight, "Bundle", pRequest.mDestinationDirectory);
  if (aAction == DestinationAction::Cancel) {
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, pRequest, aAction]() { return mCore.RunBundle(pRequest, aAction); });
}

void QtAppController::TriggerUnbundleFlow(const UnbundleRequest& pRequest) {
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = mCore.CheckUnbundle(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }
  const DestinationAction aAction =
      ResolveDestinationAction(mShell, aPreflight, "Unbundle", pRequest.mDestinationDirectory);
  if (aAction == DestinationAction::Cancel) {
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, pRequest, aAction]() { return mCore.RunUnbundle(pRequest, aAction); });
}

void QtAppController::TriggerSanityFlow(const ValidateRequest& pRequest) {
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = mCore.CheckValidate(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, pRequest]() { return mCore.RunValidate(pRequest); });
}

void QtAppController::TriggerRecoverFlow(const RecoverRequest& pRequest) {
  if (mBusy) {
    return;
  }
  const PreflightResult aPreflight = mCore.CheckRecover(pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
    return;
  }
  const DestinationAction aAction =
      ResolveDestinationAction(mShell, aPreflight, "Recover", pRequest.mDestinationDirectory);
  if (aAction == DestinationAction::Cancel) {
    return;
  }

  mBusy = true;
  mShell.SetLoading(true);
  LaunchOperation([this, pRequest, aAction]() { return mCore.RunRecover(pRequest, aAction); });
}

}  // namespace peanutbutter::ultima
