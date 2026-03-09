#ifndef PEANUT_BUTTER_ULTIMA_QT_APP_CONTROLLER_HPP_
#define PEANUT_BUTTER_ULTIMA_QT_APP_CONTROLLER_HPP_

#include <QObject>

#include "AppCore.hpp"

namespace peanutbutter::ultima {

class AppShell {
 public:
  virtual ~AppShell() = default;
  virtual void SetLoading(bool pEnabled) = 0;
  virtual void ShowError(const std::string& pTitle, const std::string& pMessage) = 0;
  virtual DestinationAction PromptDestinationAction(const std::string& pOperationName,
                                                    const std::string& pDestinationPath) = 0;
};

class QtAppController final : public QObject {
 public:
  QtAppController(AppShell& pShell, ApplicationCore& pCore, QObject* pParent = nullptr);

  void TriggerBundleFlow(const BundleRequest& pRequest);
  void TriggerUnbundleFlow(const UnbundleRequest& pRequest);
  void TriggerSanityFlow(const ValidateRequest& pRequest);
  void TriggerRecoverFlow(const RecoverRequest& pRequest);

 bool IsBusy() const;

 private:
  template <typename tOperation>
  void LaunchOperation(tOperation pOperation);
  void FinishOperation(const OperationResult& pResult);

 private:
  AppShell& mShell;
  ApplicationCore& mCore;
  bool mBusy = false;
};

}  // namespace peanutbutter::ultima

#endif  // PEANUT_BUTTER_ULTIMA_QT_APP_CONTROLLER_HPP_
