#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTimer>
#include <QString>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "AppShell_Bundle.hpp"
#include "AppShell_Common.hpp"
#include "AppShell_Sanity.hpp"
#include "AppShell_Unbundle.hpp"
#include "Encryption/Crypt.hpp"
#include "StressCrypt.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using namespace std::chrono_literals;

constexpr char kDefaultArchiveExtension[] = ".PBTR";
constexpr peanutbutter::EncryptionStrength kDefaultBundleEncryptionStrength =
    peanutbutter::EncryptionStrength::kHigh;
// constexpr peanutbutter::EncryptionStrength kDefaultBundleEncryptionStrength =
//     peanutbutter::EncryptionStrength::kMedium;
// constexpr peanutbutter::EncryptionStrength kDefaultBundleEncryptionStrength =
//     peanutbutter::EncryptionStrength::kLow;

struct LayerCakeCipherCounts {
  std::size_t mLayer1 = 0u;
  std::size_t mLayer2 = 0u;
  std::size_t mLayer3 = 0u;
};

void LogCryptGenerationStatus(const peanutbutter::CryptGeneratorRequest& pRequest,
                              const std::string& pMessage) {
  if (pRequest.mLogStatus) {
    pRequest.mLogStatus(pMessage);
  }
}

void ReportCryptGenerationProgress(const peanutbutter::CryptGeneratorRequest& pRequest,
                                   peanutbutter::CryptGenerationStage pStage,
                                   double pStageFraction) {
  if (pRequest.mReportProgress) {
    pRequest.mReportProgress(pStage, pStageFraction);
  }
}

LayerCakeCipherCounts GetLayerCakeCipherCounts(peanutbutter::EncryptionStrength pStrength) {
  switch (pStrength) {
    case peanutbutter::EncryptionStrength::kHigh:
      return {10u, 8u, 6u};
    case peanutbutter::EncryptionStrength::kMedium:
      return {8u, 6u, 4u};
    case peanutbutter::EncryptionStrength::kLow:
      return {6u, 4u, 2u};
  }
  return {};
}

std::string FormatLayerCakeCipherCounts(const LayerCakeCipherCounts& pCounts) {
  return std::to_string(pCounts.mLayer1) + "|" +
         std::to_string(pCounts.mLayer2) + "|" +
         std::to_string(pCounts.mLayer3);
}

peanutbutter::CryptGenerator MakePresetCryptGenerator() {
  return [](const peanutbutter::CryptGeneratorRequest& pRequest,
            std::string* pErrorMessage) -> std::unique_ptr<peanutbutter::Crypt> {
    ReportCryptGenerationProgress(pRequest, peanutbutter::CryptGenerationStage::kExpansion, 0.0);
    LogCryptGenerationStatus(pRequest, "[Expansion] Generating tables...");
    ReportCryptGenerationProgress(pRequest, peanutbutter::CryptGenerationStage::kExpansion, 1.0);
    LogCryptGenerationStatus(pRequest, "[Expansion] Generating tables has finished.");

    ReportCryptGenerationProgress(pRequest, peanutbutter::CryptGenerationStage::kLayerCake, 0.0);
    LogCryptGenerationStatus(pRequest, "[LayerCake] Creating three layer crypt stack...");

    std::unique_ptr<peanutbutter::Crypt> aCrypt;
    const LayerCakeCipherCounts aCounts = GetLayerCakeCipherCounts(pRequest.mEncryptionStrength);
    switch (pRequest.mEncryptionStrength) {
      case peanutbutter::EncryptionStrength::kHigh:
        aCrypt = std::make_unique<peanutbutter::HighCrypt>();
        break;
      case peanutbutter::EncryptionStrength::kMedium:
        aCrypt = std::make_unique<peanutbutter::MediumCrypt>();
        break;
      case peanutbutter::EncryptionStrength::kLow:
        aCrypt = std::make_unique<peanutbutter::LowCrypt>();
        break;
    }
    if (aCrypt != nullptr) {
      ReportCryptGenerationProgress(pRequest, peanutbutter::CryptGenerationStage::kLayerCake, 1.0);
      LogCryptGenerationStatus(
          pRequest,
          "[LayerCake] Crypt created with " + FormatLayerCakeCipherCounts(aCounts) + " ciphers.");
      return aCrypt;
    }

    if (pErrorMessage != nullptr) {
      *pErrorMessage = "unsupported encryption strength.";
    }
    return {};
  };
}

QString ClampDebugLogMessage(const QString& pMessage) {
  const std::size_t aLimit = peanutbutter::kDebugLogLineCharacterLimit;
  if (aLimit == 0u) {
    return QString();
  }
  const qsizetype aQtLimit = static_cast<qsizetype>(aLimit);
  if (pMessage.size() <= aQtLimit) {
    return pMessage;
  }
  if (aQtLimit <= 3) {
    return pMessage.left(aQtLimit);
  }
  return pMessage.left(aQtLimit - 3) + "...";
}

struct ArchiveSizeOption {
  const char* mLabel = "";
  std::uint32_t mBlocks = 0;
};

struct EncryptionStrengthOption {
  const char* mLabel = "";
  peanutbutter::EncryptionStrength mStrength = peanutbutter::EncryptionStrength::kHigh;
};

const std::array<ArchiveSizeOption, 8> archive_size_options = {{
    {"10 Blocks", 10},
    {"25 Blocks", 25},
    {"50 Blocks", 50},
    {"100 Blocks", 100},
    {"250 Blocks", 250},
    {"500 Blocks", 500},
    {"1000 Blocks", 1000},
    {"2000 Blocks", 2000},
}};

const std::array<EncryptionStrengthOption, 3> encryption_strength_options = {{
    {"High", peanutbutter::EncryptionStrength::kLow},
    {"Extra High", peanutbutter::EncryptionStrength::kMedium},
    {"Extra Extra High", peanutbutter::EncryptionStrength::kHigh},
}};

std::string inferredBaseDirectory(const peanutbutter::FileSystem& pFileSystem) {
  return pFileSystem.CurrentWorkingDirectory();
}

std::string resolveConfigPath(const peanutbutter::FileSystem& pFileSystem) {
  return pFileSystem.JoinPath(inferredBaseDirectory(pFileSystem), "config.json");
}

class PathDropFilter final : public QObject {
 public:
  enum class TargetType {
    Any,
    Folder,
    File,
  };

  PathDropFilter(const peanutbutter::FileSystem& pFileSystem, QLineEdit* pEdit, TargetType pTargetType)
      : QObject(pEdit),
        mFileSystem(pFileSystem),
        mEdit(pEdit),
        mTargetType(pTargetType) {}

 protected:
  bool eventFilter(QObject* pWatched, QEvent* pEvent) override {
    if (pWatched != mEdit || mEdit == nullptr || pEvent == nullptr) {
      return QObject::eventFilter(pWatched, pEvent);
    }

    if (pEvent->type() == QEvent::DragEnter) {
      auto* aDragEvent = static_cast<QDragEnterEvent*>(pEvent);
      if (HasAcceptedPath(aDragEvent->mimeData())) {
        aDragEvent->acceptProposedAction();
        return true;
      }
      return false;
    }

    if (pEvent->type() == QEvent::Drop) {
      auto* aDropEvent = static_cast<QDropEvent*>(pEvent);
      const QString aPath = ExtractAcceptedPath(aDropEvent->mimeData());
      if (!aPath.isEmpty()) {
        mEdit->setText(aPath);
        aDropEvent->acceptProposedAction();
        return true;
      }
      return false;
    }

    return QObject::eventFilter(pWatched, pEvent);
  }

 private:
  bool HasAcceptedPath(const QMimeData* pMimeData) const {
    return !ExtractAcceptedPath(pMimeData).isEmpty();
  }

  QString ExtractAcceptedPath(const QMimeData* pMimeData) const {
    if (pMimeData == nullptr || !pMimeData->hasUrls()) {
      return QString();
    }

    const QList<QUrl> aUrls = pMimeData->urls();
    if (aUrls.size() != 1 || !aUrls.front().isLocalFile()) {
      return QString();
    }

    const QString aLocalPath = aUrls.front().toLocalFile();
    if (mTargetType == TargetType::Any) {
      const std::string aPath = aLocalPath.toStdString();
      return (mFileSystem.IsDirectory(aPath) || mFileSystem.IsFile(aPath)) ? aLocalPath : QString();
    }
    if (mTargetType == TargetType::Folder) {
      return mFileSystem.IsDirectory(aLocalPath.toStdString()) ? aLocalPath : QString();
    }
    return mFileSystem.IsFile(aLocalPath.toStdString()) ? aLocalPath : QString();
  }

  const peanutbutter::FileSystem& mFileSystem;
  QLineEdit* mEdit = nullptr;
  TargetType mTargetType = TargetType::Folder;
};

class WorkflowTabWheelFilter final : public QObject {
 public:
  explicit WorkflowTabWheelFilter(QTabWidget* pTabs, QObject* pParent = nullptr)
      : QObject(pParent), mTabs(pTabs) {}

 protected:
  bool eventFilter(QObject* pWatched, QEvent* pEvent) override {
    if (mTabs == nullptr || pEvent == nullptr || pEvent->type() != QEvent::Wheel) {
      return QObject::eventFilter(pWatched, pEvent);
    }

    auto* aWheelEvent = static_cast<QWheelEvent*>(pEvent);
    const int aDeltaY = aWheelEvent->angleDelta().y();
    if (aDeltaY == 0) {
      return QObject::eventFilter(pWatched, pEvent);
    }

    const int aTabCount = mTabs->count();
    if (aTabCount <= 0) {
      return true;
    }

    const int aCurrentIndex = mTabs->currentIndex();
    const int aNextIndex = (aDeltaY > 0)
                               ? ((aCurrentIndex + 1) % aTabCount)
                               : ((aCurrentIndex + aTabCount - 1) % aTabCount);
    mTabs->setCurrentIndex(aNextIndex);
    aWheelEvent->accept();
    return true;
  }

 private:
  QTabWidget* mTabs = nullptr;
};

QJsonObject loadConfigDefaults(const peanutbutter::FileSystem& pFileSystem) {
  std::string aConfigText;
  if (!pFileSystem.ReadTextFile(resolveConfigPath(pFileSystem), aConfigText)) {
    return {};
  }

  const QJsonDocument aDocument = QJsonDocument::fromJson(QByteArray::fromStdString(aConfigText));
  return aDocument.isObject() ? aDocument.object() : QJsonObject{};
}

QString configStringValue(const QJsonObject& pObject, const char* pKey, const QString& pFallback) {
  const QJsonValue aValue = pObject.value(QString::fromUtf8(pKey));
  return aValue.isString() ? aValue.toString() : pFallback;
}

QString configStringValue(const QJsonObject& pObject, const char* pKey, const char* pFallback) {
  return configStringValue(pObject, pKey, QString::fromUtf8(pFallback));
}

bool configBoolValue(const QJsonObject& pObject, const char* pKey, bool pFallback) {
  const QJsonValue aValue = pObject.value(QString::fromUtf8(pKey));
  return aValue.isBool() ? aValue.toBool() : pFallback;
}

std::uint32_t configArchiveBlockCountValue(const QJsonObject& pObject, std::uint32_t pFallback) {
  const QJsonValue aValue = pObject.value(QStringLiteral("default_archive_blocks"));
  return aValue.isDouble() ? static_cast<std::uint32_t>(aValue.toDouble()) : pFallback;
}

bool nativeFileDialogsAvailable() {
  const QString aPlatformName = QGuiApplication::platformName().toLower();
  return aPlatformName != "offscreen" && aPlatformName != "minimal" && aPlatformName != "minimalegl";
}

bool IsHiddenRelativePath(const std::string& pRelativePath) {
  if (pRelativePath.empty()) {
    return false;
  }
  std::size_t aStart = 0u;
  while (aStart < pRelativePath.size()) {
    const std::size_t aEnd = pRelativePath.find('/', aStart);
    const std::size_t aSegmentEnd = (aEnd == std::string::npos) ? pRelativePath.size() : aEnd;
    if (aSegmentEnd > aStart && pRelativePath[aStart] == '.') {
      return true;
    }
    if (aEnd == std::string::npos) {
      break;
    }
    aStart = aEnd + 1u;
  }
  return false;
}

bool DirectoryHasVisibleEntries(const peanutbutter::FileSystem& pFileSystem, const std::string& pPath) {
  if (!pFileSystem.IsDirectory(pPath)) {
    return false;
  }

  const std::vector<peanutbutter::DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pPath);
  for (const peanutbutter::DirectoryEntry& aEntry : aFiles) {
    if (!IsHiddenRelativePath(aEntry.mRelativePath)) {
      return true;
    }
  }

  const std::vector<peanutbutter::DirectoryEntry> aDirectories = pFileSystem.ListDirectoriesRecursive(pPath);
  for (const peanutbutter::DirectoryEntry& aEntry : aDirectories) {
    if (aEntry.mRelativePath.empty()) {
      continue;
    }
    if (!IsHiddenRelativePath(aEntry.mRelativePath)) {
      return true;
    }
  }

  return false;
}

QString pickFile(QWidget* pParent, const QString& pWhat) {
  if (!nativeFileDialogsAvailable()) {
    QMessageBox::warning(pParent, "File dialog unavailable", "The OS file dialog is not available on this platform.");
    return QString();
  }

  QFileDialog aDialog(pParent, pWhat);
  aDialog.setFileMode(QFileDialog::ExistingFile);
  aDialog.setOption(QFileDialog::DontUseNativeDialog, false);
  if (aDialog.exec() != QDialog::Accepted || aDialog.selectedFiles().isEmpty()) {
    return QString();
  }
  return aDialog.selectedFiles().front();
}

QString pickFolder(QWidget* pParent, const QString& pWhat) {
  if (!nativeFileDialogsAvailable()) {
    QMessageBox::warning(pParent, "Folder dialog unavailable", "The OS folder dialog is not available on this platform.");
    return QString();
  }

  QFileDialog aDialog(pParent, pWhat);
  aDialog.setFileMode(QFileDialog::Directory);
  aDialog.setOption(QFileDialog::ShowDirsOnly, true);
  aDialog.setOption(QFileDialog::DontUseNativeDialog, false);
  if (aDialog.exec() != QDialog::Accepted || aDialog.selectedFiles().isEmpty()) {
    return QString();
  }
  return aDialog.selectedFiles().front();
}

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

bool IsAbsolutePath(const std::string_view pPath) {
  if (pPath.empty()) {
    return false;
  }
  if (pPath[0] == '/' || pPath[0] == '\\') {
    return true;
  }
  return (pPath.size() > 2 &&
          std::isalpha(static_cast<unsigned char>(pPath[0])) != 0 &&
          pPath[1] == ':' &&
          (pPath[2] == '/' || pPath[2] == '\\'));
}

std::string CollapseRepeatedLetters(const std::string_view pPath) {
  std::string aCollapsed;
  aCollapsed.reserve(pPath.size());
  char aPrevious = '\0';
  bool aHasPrevious = false;
  for (const char aChar : pPath) {
    const bool aIsLetter = (aChar >= 'a' && aChar <= 'z') || (aChar >= 'A' && aChar <= 'Z');
    if (aIsLetter && aHasPrevious && aChar == aPrevious) {
      continue;
    }
    aCollapsed.push_back(aChar);
    aPrevious = aChar;
    aHasPrevious = true;
  }
  return aCollapsed;
}

std::string ResolveRuleBasePath(const peanutbutter::FileSystem& pFileSystem) {
  if (!BuildInReleaseMode()) {
    return pFileSystem.CurrentWorkingDirectory();
  }

  const std::string aAppDir = QCoreApplication::applicationDirPath().toStdString();
  const std::string aContentsDir = pFileSystem.ParentPath(aAppDir);
  const std::string aBundlePath = pFileSystem.ParentPath(aContentsDir);
  if (pFileSystem.FileName(aAppDir) == "MacOS" &&
      pFileSystem.FileName(aContentsDir) == "Contents" &&
      pFileSystem.Extension(aBundlePath) == ".app") {
    return pFileSystem.ParentPath(aBundlePath);
  }
  return aAppDir;
}

std::string ResolvePathToken(const peanutbutter::FileSystem& pFileSystem, const std::string_view pPath) {
  if (pPath.empty() || IsAbsolutePath(pPath)) {
    return std::string(pPath);
  }

  const std::string aBasePath = ResolveRuleBasePath(pFileSystem);
  const std::string aToken(pPath);
  const std::string aPrimaryPath = pFileSystem.JoinPath(aBasePath, aToken);
  if (!BuildInReleaseMode()) {
    return aPrimaryPath;
  }
  if (pFileSystem.Exists(aPrimaryPath)) {
    return aPrimaryPath;
  }

  if (!HasPathSeparator(aToken)) {
    const std::string aCollapsedToken = CollapseRepeatedLetters(aToken);
    if (aCollapsedToken != aToken) {
      const std::string aCollapsedPath = pFileSystem.JoinPath(aBasePath, aCollapsedToken);
      if (pFileSystem.Exists(aCollapsedPath)) {
        return aCollapsedPath;
      }
    }
  }
  return aPrimaryPath;
}

enum class PBDestinationAction {
  Cancel,
  Merge,
  Clear,
};

enum class PBPreflightSignal {
  GreenLight,
  YellowLight,
  RedLight,
};

enum class PBCancelPromptResult {
  CancelOperation,
  KeepRunning,
  Dismiss,
};

enum class PBDecodeIntent {
  Unbundle,
  Recover,
};

struct PBPreflightResult {
  PBPreflightSignal mSignal = PBPreflightSignal::RedLight;
  std::string mTitle;
  std::string mMessage;
};

struct PBOperationResult {
  bool mSucceeded = false;
  bool mCanceled = false;
  std::string mTitle;
  std::string mMessage;
};

struct PBBundleRequest {
  std::string mSourcePath;
  std::string mDestinationPath;
  std::string mArchivePrefix;
  std::string mPassword;
  std::size_t mArchiveBlockCount = 10;
  peanutbutter::EncryptionStrength mEncryptionStrength = kDefaultBundleEncryptionStrength;
  bool mUseEncryption = false;
};

struct PBDecodeRequest {
  std::string mSourcePath;
  std::string mDestinationPath;
  std::string mPassword;
  bool mUseEncryption = false;
  bool mRecoveryMode = false;
};

struct PBSanityRequest {
  std::string mLeftPath;
  std::string mRightPath;
};

PBBundleRequest ResolveRequestPaths(const peanutbutter::FileSystem& pFileSystem, const PBBundleRequest& pRequest) {
  PBBundleRequest aRequest = pRequest;
  aRequest.mSourcePath = ResolvePathToken(pFileSystem, aRequest.mSourcePath);
  aRequest.mDestinationPath = ResolvePathToken(pFileSystem, aRequest.mDestinationPath);
  return aRequest;
}

PBDecodeRequest ResolveRequestPaths(const peanutbutter::FileSystem& pFileSystem, const PBDecodeRequest& pRequest) {
  PBDecodeRequest aRequest = pRequest;
  aRequest.mSourcePath = ResolvePathToken(pFileSystem, aRequest.mSourcePath);
  aRequest.mDestinationPath = ResolvePathToken(pFileSystem, aRequest.mDestinationPath);
  return aRequest;
}

PBSanityRequest ResolveRequestPaths(const peanutbutter::FileSystem& pFileSystem, const PBSanityRequest& pRequest) {
  PBSanityRequest aRequest = pRequest;
  aRequest.mLeftPath = ResolvePathToken(pFileSystem, aRequest.mLeftPath);
  aRequest.mRightPath = ResolvePathToken(pFileSystem, aRequest.mRightPath);
  return aRequest;
}

class PBLogSession final {
 public:
  PBLogSession(peanutbutter::FileSystem& pFileSystem,
               std::function<void(const std::string&, bool)> pSink,
               std::function<void(const peanutbutter::ProgressInfo&)> pProgressSink = {})
      : mFileSystem(pFileSystem),
        mSink(std::move(pSink)),
        mProgressSink(std::move(pProgressSink)) {}

  void BeginSession(const std::string& pPrimaryFilePath, const std::string& pMirrorFilePath) {
    {
      std::lock_guard<std::mutex> aLock(mMutex);
      mPrimaryFilePath = pPrimaryFilePath;
      mMirrorFilePath = pMirrorFilePath;
    }
    if (!pPrimaryFilePath.empty()) {
      (void)mFileSystem.WriteTextFile(pPrimaryFilePath, std::string());
    }
    if (!pMirrorFilePath.empty() && pMirrorFilePath != pPrimaryFilePath) {
      (void)mFileSystem.WriteTextFile(pMirrorFilePath, std::string());
    }
  }

  void LogStatus(const std::string& pMessage) {
    LogLine(pMessage, false);
  }

  void LogError(const std::string& pMessage) {
    LogLine(pMessage, true);
  }

  void LogProgress(const peanutbutter::ProgressInfo& pProgress) {
    std::function<void(const peanutbutter::ProgressInfo&)> aProgressSink;
    {
      std::lock_guard<std::mutex> aLock(mMutex);
      aProgressSink = mProgressSink;
    }
    if (aProgressSink) {
      aProgressSink(pProgress);
    }
  }

 private:
  void LogLine(const std::string& pMessage, bool pIsError) {
    std::string aPrimaryFilePath;
    std::string aMirrorFilePath;
    {
      std::lock_guard<std::mutex> aLock(mMutex);
      aPrimaryFilePath = mPrimaryFilePath;
      aMirrorFilePath = mMirrorFilePath;
    }

    if (mSink) {
      mSink(pMessage, pIsError);
    }

    std::string aLine = pIsError ? "[error] " + pMessage + "\n" : pMessage + "\n";
    if (!aPrimaryFilePath.empty()) {
      (void)mFileSystem.AppendTextFile(aPrimaryFilePath, aLine);
    }
    if (!aMirrorFilePath.empty()) {
      (void)mFileSystem.AppendTextFile(aMirrorFilePath, aLine);
    }
  }

  peanutbutter::FileSystem& mFileSystem;
  std::function<void(const std::string&, bool)> mSink;
  std::function<void(const peanutbutter::ProgressInfo&)> mProgressSink;
  std::string mPrimaryFilePath;
  std::string mMirrorFilePath;
  std::mutex mMutex;
};

class PBQtShell final {
 public:
  struct TabLoadingView {
    QStackedLayout* mStack = nullptr;
    QWidget* mContentWidget = nullptr;
    QWidget* mLoadingWidget = nullptr;
    QProgressBar* mLoadingIndicator = nullptr;
    QLabel* mLoadingTitle = nullptr;
    QLabel* mLoadingDetail = nullptr;
    QPushButton* mLoadingCancelButton = nullptr;
  };

  PBQtShell(QWidget* pWindow,
            std::vector<TabLoadingView> pTabLoadingViews,
            QPlainTextEdit* pDebugConsole)
      : mWindow(pWindow),
        mTabLoadingViews(std::move(pTabLoadingViews)),
        mDebugConsole(pDebugConsole) {
    if (mDebugConsole != nullptr && peanutbutter::kDebugLogLineLimit > 0u) {
      int aMaxBlocks = 0;
      if (peanutbutter::kDebugLogLineLimit > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        aMaxBlocks = std::numeric_limits<int>::max();
      } else {
        aMaxBlocks = static_cast<int>(peanutbutter::kDebugLogLineLimit);
      }
      mDebugConsole->setMaximumBlockCount(aMaxBlocks);
    }
  }

  void AppendLog(const QString& pMessage) {
    const QString aSafeMessage = ClampDebugLogMessage(pMessage);
    QPointer<QPlainTextEdit> aDebugConsole = mDebugConsole;
    QMetaObject::invokeMethod(
        mWindow,
        [aDebugConsole, aSafeMessage]() {
          if (aDebugConsole == nullptr) {
            return;
          }
          aDebugConsole->appendPlainText(aSafeMessage);
          if (QScrollBar* const aScrollBar = aDebugConsole->verticalScrollBar(); aScrollBar != nullptr) {
            aScrollBar->setValue(aScrollBar->maximum());
          }
        },
        Qt::QueuedConnection);
  }

  void SetLoading(bool pEnabled, const QString& pTitle = QString(), const QString& pDetail = QString()) {
    mLoadingEnabled = pEnabled;
    for (TabLoadingView& aView : mTabLoadingViews) {
      if (aView.mLoadingIndicator != nullptr) {
        aView.mLoadingIndicator->setRange(0, 1000);
        aView.mLoadingIndicator->setValue(0);
      }
      if (aView.mLoadingTitle != nullptr) {
        aView.mLoadingTitle->setText(pTitle);
      }
      if (aView.mLoadingDetail != nullptr) {
        aView.mLoadingDetail->setText(pDetail);
      }
      if (aView.mLoadingCancelButton != nullptr) {
        aView.mLoadingCancelButton->setEnabled(pEnabled);
      }
    }
    ApplyLoadingPresentation();
    if (!mLoadingEnabled) {
      SchedulePendingErrorDrain();
    }
  }

  void UpdateLoadingProgress(const peanutbutter::ProgressInfo& pProgress) {
    if (mWindow == nullptr) {
      return;
    }

    const double aClampedFraction = peanutbutter::ClampProgressFraction(pProgress.mOverallFraction);
    const int aValue = static_cast<int>(aClampedFraction * 1000.0 + 0.5);
    const QString aPhase = QString::fromUtf8(peanutbutter::ProgressPhaseToString(pProgress.mPhase));
    const QString aPercent = QString::number(aClampedFraction * 100.0, 'f', 1) + "%";
    const QString aDetail = pProgress.mDetail.empty()
                                ? (aPhase + " " + aPercent)
                                : (QString::fromStdString(pProgress.mDetail) + "\n" + aPhase + " " + aPercent);

    QPointer<QWidget> aWindow = mWindow;
    QMetaObject::invokeMethod(
        mWindow,
        [this, aWindow, aValue, aDetail]() {
          if (aWindow == nullptr) {
            return;
          }
          for (TabLoadingView& aView : mTabLoadingViews) {
            if (aView.mLoadingIndicator != nullptr) {
              aView.mLoadingIndicator->setRange(0, 1000);
              aView.mLoadingIndicator->setValue(std::max(0, std::min(1000, aValue)));
            }
            if (aView.mLoadingDetail != nullptr) {
              aView.mLoadingDetail->setText(aDetail);
            }
          }
        },
        Qt::QueuedConnection);
  }

  void ShowError(const std::string& pTitle, const std::string& pMessage) {
    if (mWindow == nullptr) {
      return;
    }
    if (HasBlockingDialogState()) {
      QueuePendingError(pTitle, pMessage);
      SchedulePendingErrorDrain();
      return;
    }
    PresentErrorDialog(pTitle, pMessage);
  }

  void ShowErrorDeferred(const std::string& pTitle, const std::string& pMessage) {
    if (mWindow == nullptr) {
      return;
    }
    QueuePendingError(pTitle, pMessage);
    SchedulePendingErrorDrain();
  }

  PBDestinationAction PromptDestinationAction(const std::string& pOperationName, const std::string& pDestinationPath) {
    if (mWindow == nullptr) {
      return PBDestinationAction::Cancel;
    }

    return RunTrackedDialog(false, [&]() {
      QMessageBox aBox(mWindow);
      aBox.setIcon(QMessageBox::Question);
      aBox.setWindowTitle(QString::fromStdString(pOperationName + " Destination"));
      aBox.setText(QString::fromStdString("Choose how to use:\n" + pDestinationPath));
      QPushButton* const aCancelButton = aBox.addButton("Cancel", QMessageBox::RejectRole);
      QPushButton* const aClearButton = aBox.addButton("Clear", QMessageBox::DestructiveRole);
      QPushButton* const aMergeButton = aBox.addButton("Merge", QMessageBox::AcceptRole);
      aBox.exec();
      if (aBox.clickedButton() == aClearButton) {
        return PBDestinationAction::Clear;
      }
      if (aBox.clickedButton() == aMergeButton) {
        return PBDestinationAction::Merge;
      }
      if (aBox.clickedButton() == aCancelButton) {
        return PBDestinationAction::Cancel;
      }
      return PBDestinationAction::Cancel;
    });
  }

  PBCancelPromptResult PromptCancelOperation() {
    if (mWindow == nullptr) {
      return PBCancelPromptResult::Dismiss;
    }

    // Exception to the normal dialog/loading exclusivity:
    // while the user is deciding cancel/keep-running, keep loading UI visible.
    return RunTrackedDialog(false, [&]() {
      QMessageBox aBox(mWindow);
      aBox.setIcon(QMessageBox::Question);
      aBox.setWindowTitle("Cancel Active Operation");
      aBox.setText("Yes cancels the active operation.\nNo keeps it running.\nCancel closes this prompt.");
      aBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      aBox.setDefaultButton(QMessageBox::No);
      const int aResult = aBox.exec();
      if (aResult == QMessageBox::Yes) {
        return PBCancelPromptResult::CancelOperation;
      }
      if (aResult == QMessageBox::No) {
        return PBCancelPromptResult::KeepRunning;
      }
      return PBCancelPromptResult::Dismiss;
    });
  }

 private:
  template <typename tCallback>
  auto RunTrackedDialog(bool pSuspendLoading, tCallback pCallback) -> decltype(pCallback()) {
    const bool aWasShowingLoading = pSuspendLoading && mLoadingEnabled;
    if (aWasShowingLoading) {
      ApplyContentPresentation();
    }

    ++mActiveDialogCount;
    auto aResult = pCallback();
    --mActiveDialogCount;

    if (aWasShowingLoading) {
      ApplyLoadingPresentation();
    }
    SchedulePendingErrorDrain();
    return aResult;
  }

  void ApplyLoadingPresentation() {
    for (const TabLoadingView& aView : mTabLoadingViews) {
      if (aView.mStack != nullptr && aView.mContentWidget != nullptr && aView.mLoadingWidget != nullptr) {
        aView.mStack->setCurrentWidget(mLoadingEnabled ? aView.mLoadingWidget : aView.mContentWidget);
      }
    }
  }

  void ApplyContentPresentation() {
    for (const TabLoadingView& aView : mTabLoadingViews) {
      if (aView.mStack != nullptr && aView.mContentWidget != nullptr) {
        aView.mStack->setCurrentWidget(aView.mContentWidget);
      }
    }
  }

  bool HasBlockingDialogState() const {
    return mLoadingEnabled || mActiveDialogCount > 0 || QApplication::activeModalWidget() != nullptr;
  }

  void QueuePendingError(const std::string& pTitle, const std::string& pMessage) {
    mPendingErrorTitle = pTitle;
    mPendingErrorMessage = pMessage;
    mHasPendingError = true;
  }

  void SchedulePendingErrorDrain() {
    if (mWindow == nullptr || !mHasPendingError || mPendingErrorDrainScheduled) {
      return;
    }
    mPendingErrorDrainScheduled = true;
    QTimer::singleShot(0, mWindow, [this]() {
      mPendingErrorDrainScheduled = false;
      DrainPendingError();
    });
  }

  void DrainPendingError() {
    if (!mHasPendingError) {
      return;
    }
    if (HasBlockingDialogState()) {
      SchedulePendingErrorDrain();
      return;
    }

    const std::string aTitle = mPendingErrorTitle;
    const std::string aMessage = mPendingErrorMessage;
    mHasPendingError = false;
    mPendingErrorTitle.clear();
    mPendingErrorMessage.clear();
    PresentErrorDialog(aTitle, aMessage);
  }

  void PresentErrorDialog(const std::string& pTitle, const std::string& pMessage) {
    RunTrackedDialog(false, [&]() {
      QMessageBox aBox(mWindow);
      aBox.setIcon(QMessageBox::Warning);
      aBox.setWindowTitle(QString::fromStdString(pTitle));
      aBox.setText(QString::fromStdString(pMessage));
      aBox.setStandardButtons(QMessageBox::Ok);
      aBox.exec();
      return 0;
    });
  }

  QWidget* mWindow = nullptr;
  std::vector<TabLoadingView> mTabLoadingViews;
  QPlainTextEdit* mDebugConsole = nullptr;
  bool mLoadingEnabled = false;
  int mActiveDialogCount = 0;
  bool mHasPendingError = false;
  bool mPendingErrorDrainScheduled = false;
  std::string mPendingErrorTitle;
  std::string mPendingErrorMessage;
};

class PBMockEngine final {
 public:
  PBMockEngine(peanutbutter::FileSystem& pFileSystem,
               peanutbutter::CryptGenerator pCryptGenerator,
               PBLogSession& pLogger)
      : mFileSystem(pFileSystem),
        mCryptGenerator(std::move(pCryptGenerator)),
        mLogger(pLogger) {}

  PBPreflightResult CheckBundle(const PBBundleRequest& pRequest) const {
    if (pRequest.mSourcePath.empty() || pRequest.mDestinationPath.empty()) {
      return {PBPreflightSignal::RedLight, "Bundle blocked", "Bundle source and destination are required."};
    }
    if (!mFileSystem.Exists(pRequest.mSourcePath) ||
        (!mFileSystem.IsDirectory(pRequest.mSourcePath) && !mFileSystem.IsFile(pRequest.mSourcePath))) {
      return {PBPreflightSignal::RedLight, "Bundle blocked", "Bundle source must be an existing file or folder."};
    }
    if (mFileSystem.Exists(pRequest.mDestinationPath) && !mFileSystem.IsDirectory(pRequest.mDestinationPath)) {
      return {PBPreflightSignal::RedLight, "Bundle blocked", "Bundle destination must be a folder path."};
    }
    if (mFileSystem.Exists(pRequest.mDestinationPath) &&
        DirectoryHasVisibleEntries(mFileSystem, pRequest.mDestinationPath)) {
      return {PBPreflightSignal::YellowLight, "Bundle destination prompt", "Destination has existing files."};
    }
    return {PBPreflightSignal::GreenLight, "Bundle ready", "Bundle can proceed."};
  }

  PBPreflightResult CheckDecode(const PBDecodeRequest& pRequest, PBDecodeIntent pIntent) const {
    if (pRequest.mSourcePath.empty() || pRequest.mDestinationPath.empty()) {
      return {PBPreflightSignal::RedLight, "Decode blocked", "Decode source and destination are required."};
    }
    if (!mFileSystem.Exists(pRequest.mSourcePath) ||
        (!mFileSystem.IsDirectory(pRequest.mSourcePath) && !mFileSystem.IsFile(pRequest.mSourcePath))) {
      return {PBPreflightSignal::RedLight, "Decode blocked", "Archive source must be an existing file or folder."};
    }
    if (mFileSystem.Exists(pRequest.mDestinationPath) && !mFileSystem.IsDirectory(pRequest.mDestinationPath)) {
      return {PBPreflightSignal::RedLight, "Decode blocked", "Decode destination must be a folder path."};
    }
    if (mFileSystem.Exists(pRequest.mDestinationPath) &&
        DirectoryHasVisibleEntries(mFileSystem, pRequest.mDestinationPath)) {
      return {PBPreflightSignal::YellowLight, "Decode destination prompt", "Destination has existing files."};
    }
    return {PBPreflightSignal::GreenLight, "Decode ready", "Decode can proceed without a destination prompt."};
  }

  PBPreflightResult CheckSanity(const PBSanityRequest& pRequest) const {
    if (pRequest.mLeftPath.empty() || pRequest.mRightPath.empty()) {
      return {PBPreflightSignal::RedLight, "Sanity blocked", "Sanity requires both compare paths."};
    }
    if (!mFileSystem.IsDirectory(pRequest.mLeftPath) || !mFileSystem.IsDirectory(pRequest.mRightPath)) {
      return {PBPreflightSignal::RedLight, "Sanity blocked", "Both compare paths must be existing folders."};
    }
    return {PBPreflightSignal::GreenLight, "Sanity ready", "Sanity can proceed."};
  }

  PBOperationResult RunBundle(const PBBundleRequest& pRequest,
                              PBDestinationAction pAction,
                              const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
    LogBuildConstantsOnce();
    mLogger.LogStatus("Bundle: request accepted.");
    mLogger.LogStatus("[Bundle][Discovery] Source: '" + pRequest.mSourcePath + "'.");
    mLogger.LogStatus("[Bundle][Discovery] Destination: '" + pRequest.mDestinationPath + "'.");
    PBEngineLogger aEngineLogger(mLogger);
    peanutbutter::CancelCoordinator aCancelCoordinator(pCancelRequested.get(), &aEngineLogger, "Bundle");
    if (aCancelCoordinator.ShouldCancelNow()) {
      return MakeCanceled("Bundle", &aCancelCoordinator);
    }
    if (!ApplyDestinationAction(pRequest.mDestinationPath, pAction, "Bundle")) {
      return {false, false, "Bundle failed", "Bundle destination action failed."};
    }

    std::vector<peanutbutter::SourceEntry> aSourceEntries;
    std::string aEntryError;
    if (!CollectBundleSourceEntries(pRequest.mSourcePath, aSourceEntries, aEntryError)) {
      mLogger.LogError(aEntryError);
      return {false, false, "Bundle failed", aEntryError};
    }

    peanutbutter::BundleRequest aCoreRequest;
    aCoreRequest.mDestinationDirectory = pRequest.mDestinationPath;
    aCoreRequest.mSourceStem = ResolveBundleSourceStem(pRequest.mSourcePath);
    aCoreRequest.mArchivePrefix = pRequest.mArchivePrefix.empty() ? "bundle_" : pRequest.mArchivePrefix;
    aCoreRequest.mArchiveSuffix = kDefaultArchiveExtension;
    aCoreRequest.mPassword = pRequest.mPassword;
    aCoreRequest.mArchiveBlockCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(
            std::max<std::size_t>(1u, pRequest.mArchiveBlockCount),
            static_cast<std::size_t>(peanutbutter::kMaxBlocksPerArchive)));
    aCoreRequest.mUseEncryption = pRequest.mUseEncryption;
    aCoreRequest.mEncryptionStrength = pRequest.mEncryptionStrength;
    aCoreRequest.mCryptGenerator = mCryptGenerator;

    const peanutbutter::OperationResult aCoreResult =
        peanutbutter::Bundle(aCoreRequest, aSourceEntries, mFileSystem, aEngineLogger, &aCancelCoordinator);
    return MapCoreResult("Bundle", aCoreResult, &aCancelCoordinator);
  }

  PBOperationResult RunDecode(const PBDecodeRequest& pRequest,
                              PBDecodeIntent pIntent,
                              PBDestinationAction pAction,
                              const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
    LogBuildConstantsOnce();
    const std::string aModeName = (pIntent == PBDecodeIntent::Recover) ? "Recover" : "Unbundle";
    const std::string aDiscoveryPrefix = (pIntent == PBDecodeIntent::Recover) ? "[Recover][Discovery]" : "[Decode][Discovery]";
    mLogger.LogStatus(aModeName + ": request accepted.");
    mLogger.LogStatus(aDiscoveryPrefix + " Source: '" + pRequest.mSourcePath + "'.");
    mLogger.LogStatus(aDiscoveryPrefix + " Destination: '" + pRequest.mDestinationPath + "'.");
    PBEngineLogger aEngineLogger(mLogger);
    peanutbutter::CancelCoordinator aCancelCoordinator(pCancelRequested.get(), &aEngineLogger, aModeName);
    if (aCancelCoordinator.ShouldCancelNow()) {
      return MakeCanceled(aModeName, &aCancelCoordinator);
    }
    if (!ApplyDestinationAction(pRequest.mDestinationPath, pAction, aModeName)) {
      return {false, false, aModeName + " failed", aModeName + " destination action failed."};
    }

    std::vector<std::string> aArchiveFiles;
    std::string aCollectError;
    mLogger.LogStatus(aDiscoveryPrefix + " Collecting archive file candidates...");
    if (!CollectArchiveFileList(pRequest.mSourcePath, aArchiveFiles, aCollectError)) {
      mLogger.LogError(aCollectError);
      return {false, false, aModeName + " failed", aCollectError};
    }
    mLogger.LogStatus(aDiscoveryPrefix + " Collected " + std::to_string(aArchiveFiles.size()) + " archive file candidates.");
    if (aArchiveFiles.empty()) {
      return {false, false, aModeName + " failed", "No archive files were found in the selected source."};
    }

    peanutbutter::UnbundleRequest aCoreRequest;
    aCoreRequest.mDestinationDirectory = pRequest.mDestinationPath;
    aCoreRequest.mPassword = pRequest.mPassword;
    aCoreRequest.mUseEncryption = pRequest.mUseEncryption;
    aCoreRequest.mRecoverMode = (pIntent == PBDecodeIntent::Recover);
    aCoreRequest.mCryptGenerator = mCryptGenerator;

    peanutbutter::OperationResult aCoreResult;
    if (pIntent == PBDecodeIntent::Recover) {
      aCoreResult = peanutbutter::Recover(aCoreRequest,
                                          aArchiveFiles,
                                          mFileSystem,
                                          aEngineLogger,
                                          &aCancelCoordinator);
    } else {
      aCoreResult = peanutbutter::Unbundle(aCoreRequest,
                                           aArchiveFiles,
                                           mFileSystem,
                                           aEngineLogger,
                                           &aCancelCoordinator);
    }
    return MapCoreResult(aModeName, aCoreResult, &aCancelCoordinator);
  }

  PBOperationResult RunSanity(const PBSanityRequest& pRequest,
                              const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
    LogBuildConstantsOnce();
    mLogger.LogStatus("Validate: request accepted.");
    PBEngineLogger aEngineLogger(mLogger);
    peanutbutter::CancelCoordinator aCancelCoordinator(pCancelRequested.get(), &aEngineLogger, "Validate");
    if (aCancelCoordinator.ShouldCancelNow()) {
      return MakeCanceled("Validate", &aCancelCoordinator);
    }
    peanutbutter::ValidateRequest aCoreRequest;
    aCoreRequest.mLeftDirectory = pRequest.mLeftPath;
    aCoreRequest.mRightDirectory = pRequest.mRightPath;
    const peanutbutter::OperationResult aCoreResult =
        peanutbutter::RunSanity(aCoreRequest, mFileSystem, aEngineLogger, &aCancelCoordinator);
    return MapCoreResult("Validate", aCoreResult, &aCancelCoordinator);
  }

 private:
  void LogBuildConstantsOnce() {
    if (mLoggedBuildConstants) {
      return;
    }
    mLoggedBuildConstants = true;

    std::string aMode;
#if defined(PEANUT_BUTTER_ULTIMA_TEST_BUILD)
    aMode = "test";
#elif defined(PEANUT_BUTTER_ULTIMA_RELEASE_BUILD)
    aMode = "release";
#elif defined(PEANUT_BUTTER_ULTIMA_DEBUG_BUILD)
    aMode = "debug";
#else
    aMode = "unknown";
#endif

    mLogger.LogStatus("[Build] Mode=" + aMode +
                      ", BLOCK_SIZE_L3=" + std::to_string(peanutbutter::kBlockSizeL3) +
                      ", ARCHIVE_HEADER_LENGTH=" + std::to_string(peanutbutter::kArchiveHeaderLength) +
                      ", RECOVERY_HEADER_LENGTH=" + std::to_string(peanutbutter::kRecoveryHeaderLength) + ".");
  }

  class PBEngineLogger final : public peanutbutter::Logger {
   public:
    explicit PBEngineLogger(PBLogSession& pSession) : mSession(pSession) {}
    void LogStatus(const std::string& pMessage) override {
      mSession.LogStatus(pMessage);
    }
    void LogError(const std::string& pMessage) override {
      mSession.LogError(pMessage);
    }
    void LogProgress(const peanutbutter::ProgressInfo& pProgress) override {
      mSession.LogProgress(pProgress);
    }

   private:
    PBLogSession& mSession;
  };

  PBOperationResult MakeCanceled(const std::string& pModeName,
                                 peanutbutter::CancelCoordinator* pCancelCoordinator = nullptr) {
    if (pCancelCoordinator != nullptr) {
      pCancelCoordinator->LogEndingJob();
      pCancelCoordinator->LogModeCancelled(pModeName);
    } else {
      mLogger.LogStatus("[Cancel] Ending job, thank you!");
      mLogger.LogStatus("[" + pModeName + "][Mode] " + pModeName + " was cancelled!");
    }
    return {false, true, pModeName + " Cancelled", pModeName + " canceled by user."};
  }

  PBOperationResult MapCoreResult(const std::string& pModeName,
                                  const peanutbutter::OperationResult& pCoreResult,
                                  peanutbutter::CancelCoordinator* pCancelCoordinator = nullptr) {
    PBOperationResult aResult;
    aResult.mSucceeded = pCoreResult.mSucceeded;
    aResult.mCanceled = pCoreResult.mCanceled;
    aResult.mTitle = pModeName + (pCoreResult.mSucceeded ? " complete" : " failed");
    if (!pCoreResult.mSucceeded && !pCoreResult.mCanceled) {
      std::string aMessage = pCoreResult.mFailureMessage.empty()
                                 ? std::string("operation failed.")
                                 : pCoreResult.mFailureMessage;
      if (pCoreResult.mErrorCode != peanutbutter::ErrorCode::kNone) {
        aMessage = "[" + std::string(peanutbutter::ErrorCodeToString(pCoreResult.mErrorCode)) + "] " + aMessage;
      }
      aResult.mMessage = aMessage;
    }
    if (aResult.mCanceled) {
      if (pCancelCoordinator != nullptr) {
        pCancelCoordinator->LogEndingJob();
        pCancelCoordinator->LogModeCancelled(pModeName);
      } else {
        mLogger.LogStatus("[Cancel] Ending job, thank you!");
        mLogger.LogStatus("[" + pModeName + "][Mode] " + pModeName + " was cancelled!");
      }
      if (aResult.mMessage.empty()) {
        aResult.mMessage = pModeName + " canceled by user.";
      }
    }
    return aResult;
  }

  bool ApplyDestinationAction(const std::string& pDestinationPath,
                              PBDestinationAction pAction,
                              const std::string& pModeName) {
    if (pAction == PBDestinationAction::Cancel) {
      return false;
    }
    if (pAction == PBDestinationAction::Clear) {
      if (!mFileSystem.ClearDirectory(pDestinationPath)) {
        mLogger.LogError(pModeName + ": failed to clear destination directory.");
        return false;
      }
    }
    if (!mFileSystem.EnsureDirectory(pDestinationPath)) {
      mLogger.LogError(pModeName + ": failed to create destination directory.");
      return false;
    }
    return true;
  }

  std::string ResolveBundleSourceStem(const std::string& pSourcePath) const {
    if (mFileSystem.IsFile(pSourcePath)) {
      return mFileSystem.StemName(pSourcePath);
    }
    const std::string aName = mFileSystem.FileName(pSourcePath);
    return aName.empty() ? std::string("archive_data") : aName;
  }

  bool CollectBundleSourceEntries(const std::string& pSourcePath,
                                  std::vector<peanutbutter::SourceEntry>& pEntries,
                                  std::string& pErrorMessage) const {
    pEntries.clear();
    if (mFileSystem.IsFile(pSourcePath)) {
      peanutbutter::SourceEntry aEntry;
      aEntry.mSourcePath = pSourcePath;
      aEntry.mRelativePath = mFileSystem.FileName(pSourcePath);
      aEntry.mIsDirectory = false;
      pEntries.push_back(aEntry);
      return true;
    }
    if (!mFileSystem.IsDirectory(pSourcePath)) {
      pErrorMessage = "Bundle source is not a readable file or folder.";
      return false;
    }

    std::vector<peanutbutter::DirectoryEntry> aFiles = mFileSystem.ListFilesRecursive(pSourcePath);
    std::sort(aFiles.begin(), aFiles.end(), [](const peanutbutter::DirectoryEntry& pLeft,
                                               const peanutbutter::DirectoryEntry& pRight) {
      return pLeft.mRelativePath < pRight.mRelativePath;
    });
    for (const peanutbutter::DirectoryEntry& aEntry : aFiles) {
      if (aEntry.mIsDirectory) {
        continue;
      }
      peanutbutter::SourceEntry aSourceEntry;
      aSourceEntry.mSourcePath = aEntry.mPath;
      aSourceEntry.mRelativePath = aEntry.mRelativePath;
      aSourceEntry.mIsDirectory = false;
      pEntries.push_back(aSourceEntry);
    }

    std::vector<peanutbutter::DirectoryEntry> aDirs = mFileSystem.ListDirectoriesRecursive(pSourcePath);
    std::sort(aDirs.begin(), aDirs.end(), [](const peanutbutter::DirectoryEntry& pLeft,
                                             const peanutbutter::DirectoryEntry& pRight) {
      return pLeft.mRelativePath < pRight.mRelativePath;
    });
    for (const peanutbutter::DirectoryEntry& aEntry : aDirs) {
      if (aEntry.mRelativePath.empty()) {
        continue;
      }
      if (!mFileSystem.DirectoryHasEntries(aEntry.mPath)) {
        peanutbutter::SourceEntry aSourceEntry;
        aSourceEntry.mSourcePath.clear();
        aSourceEntry.mRelativePath = aEntry.mRelativePath;
        aSourceEntry.mIsDirectory = true;
        pEntries.push_back(aSourceEntry);
      }
    }

    if (pEntries.empty()) {
      pErrorMessage = "Bundle source did not produce any file or empty-directory entries.";
      return false;
    }
    return true;
  }

  bool CollectArchiveFileList(const std::string& pSourcePath,
                              std::vector<std::string>& pArchiveFiles,
                              std::string& pErrorMessage) const {
    pArchiveFiles.clear();
    if (mFileSystem.IsFile(pSourcePath)) {
      pArchiveFiles.push_back(pSourcePath);
      return true;
    }
    if (!mFileSystem.IsDirectory(pSourcePath)) {
      pErrorMessage = "Decode source is not a readable file or folder.";
      return false;
    }

    std::vector<peanutbutter::DirectoryEntry> aFiles = mFileSystem.ListFiles(pSourcePath);
    if (aFiles.empty()) {
      mLogger.LogStatus("[Decode][Discovery] No top-level files found; falling back to recursive scan.");
      std::size_t aLastProgressLogCount = 0u;
      aFiles = mFileSystem.ListFilesRecursive(
          pSourcePath,
          [this, &aLastProgressLogCount](std::size_t pCount) {
            const std::size_t aLogInterval =
                static_cast<std::size_t>(std::max<std::uint32_t>(1u, peanutbutter::kProgressCountLogIntervalDefault));
            if (pCount >= aLastProgressLogCount + aLogInterval) {
              mLogger.LogStatus("[Decode][Discovery] Recursive scan discovered " +
                                std::to_string(pCount) + " files so far.");
              aLastProgressLogCount = pCount;
            }
            return true;
          });
    }
    for (const peanutbutter::DirectoryEntry& aEntry : aFiles) {
      if (!aEntry.mIsDirectory) {
        pArchiveFiles.push_back(aEntry.mPath);
      }
    }
    std::sort(pArchiveFiles.begin(), pArchiveFiles.end());
    return true;
  }

  peanutbutter::FileSystem& mFileSystem;
  peanutbutter::CryptGenerator mCryptGenerator;
  PBLogSession& mLogger;
  bool mLoggedBuildConstants = false;
};

class PBQtController final : public QObject {
 public:
  PBQtController(PBQtShell& pShell, PBMockEngine& pEngine, const peanutbutter::FileSystem& pFileSystem, QObject* pParent = nullptr)
      : QObject(pParent),
        mShell(pShell),
        mEngine(pEngine),
        mFileSystem(pFileSystem) {}

  ~PBQtController() override {
    if (mCancelRequested != nullptr) {
      mCancelRequested->store(true);
    }
    JoinWorkerThreadIfNeeded();
  }

  bool IsBusy() const {
    return mBusy;
  }

  void TriggerBundleFlow(const PBBundleRequest& pRequest) {
    TriggerFileFlow(
        "Bundle",
        ResolveRequestPaths(mFileSystem, pRequest),
        [this](const PBBundleRequest& pValue) { return mEngine.CheckBundle(pValue); },
        [this](const PBBundleRequest& pValue,
               PBDestinationAction pAction,
               const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
          return mEngine.RunBundle(pValue, pAction, pCancelRequested);
        },
        [](const PBBundleRequest& pValue) { return pValue.mDestinationPath; });
  }

  void TriggerUnbundleFlow(const PBDecodeRequest& pRequest) {
    TriggerFileFlow(
        "Unbundle",
        ResolveRequestPaths(mFileSystem, pRequest),
        [this](const PBDecodeRequest& pValue) { return mEngine.CheckDecode(pValue, PBDecodeIntent::Unbundle); },
        [this](const PBDecodeRequest& pValue,
               PBDestinationAction pAction,
               const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
          return mEngine.RunDecode(pValue, PBDecodeIntent::Unbundle, pAction, pCancelRequested);
        },
        [](const PBDecodeRequest& pValue) { return pValue.mDestinationPath; });
  }

  void TriggerRecoverFlow(const PBDecodeRequest& pRequest) {
    TriggerFileFlow(
        "Recover",
        ResolveRequestPaths(mFileSystem, pRequest),
        [this](const PBDecodeRequest& pValue) { return mEngine.CheckDecode(pValue, PBDecodeIntent::Recover); },
        [this](const PBDecodeRequest& pValue,
               PBDestinationAction pAction,
               const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
          return mEngine.RunDecode(pValue, PBDecodeIntent::Recover, pAction, pCancelRequested);
        },
        [](const PBDecodeRequest& pValue) { return pValue.mDestinationPath; });
  }

  void TriggerSanityFlow(const PBSanityRequest& pRequest) {
    const PBSanityRequest aResolvedRequest = ResolveRequestPaths(mFileSystem, pRequest);
    if (mBusy) {
      return;
    }
    const PBPreflightResult aPreflight = mEngine.CheckSanity(aResolvedRequest);
    if (aPreflight.mSignal == PBPreflightSignal::RedLight) {
      mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
      return;
    }

    StartOperation("Sanity", "Green-light flow", [this, aResolvedRequest](const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
      return mEngine.RunSanity(aResolvedRequest, pCancelRequested);
    });
  }

  void PromptCancelActiveOperation() {
    if (!mBusy || mCancelRequested == nullptr) {
      return;
    }

    switch (mShell.PromptCancelOperation()) {
      case PBCancelPromptResult::CancelOperation:
        mCancelRequested->store(true);
        break;
      case PBCancelPromptResult::KeepRunning:
        mShell.AppendLog("Cancellation dismissed.");
        break;
      case PBCancelPromptResult::Dismiss:
        break;
    }
  }

 private:
  template <typename tRequest, typename tCheck, typename tRun, typename tDestinationAccessor>
  void TriggerFileFlow(const std::string& pOperationName,
                       const tRequest& pRequest,
                       tCheck pCheck,
                       tRun pRun,
                       tDestinationAccessor pDestinationAccessor) {
    if (mBusy) {
      return;
    }

    const PBPreflightResult aPreflight = pCheck(pRequest);
    if (aPreflight.mSignal == PBPreflightSignal::RedLight) {
      mShell.ShowError(aPreflight.mTitle, aPreflight.mMessage);
      return;
    }

    PBDestinationAction aDestinationAction = PBDestinationAction::Merge;
    if (aPreflight.mSignal == PBPreflightSignal::YellowLight) {
      aDestinationAction = mShell.PromptDestinationAction(pOperationName, pDestinationAccessor(pRequest));
      if (aDestinationAction == PBDestinationAction::Cancel) {
        return;
      }
    }

    StartOperation(QString::fromStdString(pOperationName),
                   QString::fromStdString(pOperationName + " is running."),
                   [pRequest, aDestinationAction, pRun](const std::shared_ptr<std::atomic_bool>& pCancelRequested) {
                     return pRun(pRequest, aDestinationAction, pCancelRequested);
                   });
  }

  template <typename tOperation>
  void StartOperation(const QString& pTitle, const QString& pDetail, tOperation pOperation) {
    JoinWorkerThreadIfNeeded();
    mBusy = true;
    mCancelRequested = std::make_shared<std::atomic_bool>(false);
    mShell.SetLoading(true, pTitle, pDetail);

    QPointer<PBQtController> aSelf(this);
    std::shared_ptr<std::atomic_bool> aCancelRequested = mCancelRequested;
    mWorkerThread = std::thread([aSelf, aCancelRequested, aOperation = std::move(pOperation)]() mutable {
      const PBOperationResult aResult = aOperation(aCancelRequested);
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
  }

  void FinishOperation(const PBOperationResult& pResult) {
    mShell.SetLoading(false);
    mBusy = false;
    mCancelRequested.reset();
    JoinWorkerThreadIfNeeded();

    if (pResult.mCanceled) {
      mShell.AppendLog(QString::fromStdString(pResult.mMessage));
      return;
    }
  }

  void JoinWorkerThreadIfNeeded() {
    if (!mWorkerThread.joinable()) {
      return;
    }
    if (mWorkerThread.get_id() == std::this_thread::get_id()) {
      mWorkerThread.detach();
      return;
    }
    mWorkerThread.join();
  }

  PBQtShell& mShell;
  PBMockEngine& mEngine;
  const peanutbutter::FileSystem& mFileSystem;
  bool mBusy = false;
  std::shared_ptr<std::atomic_bool> mCancelRequested;
  std::thread mWorkerThread;
};

QWidget* makeLabeledPathRow(QWidget* pParent,
                            const QString& pLabelText,
                            QLineEdit* pPathEdit,
                            QPushButton* pPickFileButton,
                            QPushButton* pPickFolderButton) {
  auto* aHost = new QWidget(pParent);
  auto* aLayout = new QHBoxLayout(aHost);
  auto* aLabel = new QLabel(pLabelText, aHost);
  aLayout->setContentsMargins(0, 0, 0, 0);
  aLayout->setSpacing(8);
  aLabel->setMinimumWidth(140);
  aLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  pPathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  aLayout->addWidget(aLabel);
  aLayout->addWidget(pPathEdit, 1);
  if (pPickFileButton != nullptr) {
    aLayout->addWidget(pPickFileButton);
  }
  if (pPickFolderButton != nullptr) {
    aLayout->addWidget(pPickFolderButton);
  }
  return aHost;
}

QFrame* makePanelSeparator(QWidget* pParent) {
  auto* aDivider = new QFrame(pParent);
  aDivider->setFrameShape(QFrame::HLine);
  aDivider->setFrameShadow(QFrame::Sunken);
  aDivider->setStyleSheet("QFrame { color: #2a2a2a; }");
  return aDivider;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QWidget window;
  window.setWindowTitle(
      QString("PB Crypt, L3=%1, PAY=%2, MPL=%3")
          .arg(static_cast<qulonglong>(peanutbutter::kBlockSizeL3))
          .arg(static_cast<qulonglong>(peanutbutter::kPayloadBytesPerL3))
          .arg(static_cast<qulonglong>(peanutbutter::kMaxPathLength)));
  window.setMinimumSize(1120, 720);

  constexpr int kElementHeight = 56;
  constexpr int kButtonGroupWidth = 228;
  constexpr int kSmallButtonWidth = 110;
  constexpr int kFileButtonWidth = 110;
  constexpr int kTabHeight = 44;

  auto* layout = new QVBoxLayout(&window);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);

  auto* make_archive_source_edit = new QLineEdit(&window);
  auto* make_archive_destination_edit = new QLineEdit(&window);
  auto* make_archive_source_file_button = new QPushButton("File...", &window);
  auto* make_archive_source_folder_button = new QPushButton("Folder...", &window);
  auto* make_archive_destination_folder_button = new QPushButton("Folder...", &window);
  auto* make_archive_file_prefix_edit = new QLineEdit(&window);
  auto* make_archive_password_edit = new QLineEdit(&window);
  auto* archive_size_combo = new QComboBox(&window);
  auto* encryption_strength_combo = new QComboBox(&window);
  auto* bundle_button = new QPushButton("Bundle", &window);
  auto* open_archive_source_edit = new QLineEdit(&window);
  auto* open_archive_destination_edit = new QLineEdit(&window);
  auto* open_archive_source_file_button = new QPushButton("File...", &window);
  auto* open_archive_source_folder_button = new QPushButton("Folder...", &window);
  auto* open_archive_destination_folder_button = new QPushButton("Folder...", &window);
  auto* open_archive_password_edit = new QLineEdit(&window);
  auto* recovery_mode_checkbox = new QCheckBox("Recovery Mode", &window);
  auto* unbundle_button = new QPushButton("Unbundle", &window);
  auto* compare_left_edit = new QLineEdit(&window);
  auto* compare_right_edit = new QLineEdit(&window);
  auto* compare_left_folder_button = new QPushButton("Folder...", &window);
  auto* compare_right_folder_button = new QPushButton("Folder...", &window);
  auto* compare_button = new QPushButton("Compare", &window);
  auto* clear_logs_button = new QPushButton("Clear Logs", &window);
  auto* scroll_to_bottom_button = new QPushButton("Scroll to Bottom", &window);

  auto* debug_console = new QPlainTextEdit(&window);
  auto* workflow_tabs = new QTabWidget(&window);
  auto* make_archive_tab = new QWidget(workflow_tabs);
  auto* make_archive_tab_layout = new QVBoxLayout(make_archive_tab);
  auto* make_archive_stack_host = new QWidget(make_archive_tab);
  auto* make_archive_stack = new QStackedLayout(make_archive_stack_host);
  auto* make_archive_content = new QWidget(make_archive_stack_host);
  auto* make_archive_layout = new QVBoxLayout(make_archive_content);
  auto* make_archive_loading = new QWidget(make_archive_stack_host);
  auto* make_archive_loading_layout = new QVBoxLayout(make_archive_loading);
  auto* make_archive_loading_title = new QLabel("Working", make_archive_loading);
  auto* make_archive_loading_indicator = new QProgressBar(make_archive_loading);
  auto* make_archive_loading_detail = new QLabel("Operation is running.", make_archive_loading);
  auto* make_archive_loading_cancel_button = new QPushButton("Cancel", make_archive_loading);
  auto* open_archive_tab = new QWidget(workflow_tabs);
  auto* open_archive_tab_layout = new QVBoxLayout(open_archive_tab);
  auto* open_archive_stack_host = new QWidget(open_archive_tab);
  auto* open_archive_stack = new QStackedLayout(open_archive_stack_host);
  auto* open_archive_content = new QWidget(open_archive_stack_host);
  auto* open_archive_layout = new QVBoxLayout(open_archive_content);
  auto* open_archive_loading = new QWidget(open_archive_stack_host);
  auto* open_archive_loading_layout = new QVBoxLayout(open_archive_loading);
  auto* open_archive_loading_title = new QLabel("Working", open_archive_loading);
  auto* open_archive_loading_indicator = new QProgressBar(open_archive_loading);
  auto* open_archive_loading_detail = new QLabel("Operation is running.", open_archive_loading);
  auto* open_archive_loading_cancel_button = new QPushButton("Cancel", open_archive_loading);
  auto* compare_tab = new QWidget(workflow_tabs);
  auto* compare_tab_layout = new QVBoxLayout(compare_tab);
  auto* compare_stack_host = new QWidget(compare_tab);
  auto* compare_stack = new QStackedLayout(compare_stack_host);
  auto* compare_content = new QWidget(compare_stack_host);
  auto* compare_layout = new QVBoxLayout(compare_content);
  auto* compare_loading = new QWidget(compare_stack_host);
  auto* compare_loading_layout = new QVBoxLayout(compare_loading);
  auto* compare_loading_title = new QLabel("Working", compare_loading);
  auto* compare_loading_indicator = new QProgressBar(compare_loading);
  auto* compare_loading_detail = new QLabel("Operation is running.", compare_loading);
  auto* compare_loading_cancel_button = new QPushButton("Cancel", compare_loading);
  auto* logs_row_host = new QWidget(&window);
  auto* logs_row_layout = new QHBoxLayout(logs_row_host);
  auto* make_archive_separator_1 = makePanelSeparator(make_archive_tab);
  auto* make_archive_separator_2 = makePanelSeparator(make_archive_tab);
  auto* make_archive_separator_3 = makePanelSeparator(make_archive_tab);
  auto* open_archive_separator_1 = makePanelSeparator(open_archive_tab);
  auto* open_archive_separator_2 = makePanelSeparator(open_archive_tab);
  auto* compare_separator_1 = makePanelSeparator(compare_tab);
  auto* compare_separator_2 = makePanelSeparator(compare_tab);

  make_archive_source_edit->setPlaceholderText("Make Archive Source");
  make_archive_destination_edit->setPlaceholderText("Archive Output Folder");
  open_archive_source_edit->setPlaceholderText("Archive Source");
  open_archive_destination_edit->setPlaceholderText("Open Archive Destination");
  compare_left_edit->setPlaceholderText("Left Directory");
  compare_right_edit->setPlaceholderText("Right Directory");
  make_archive_file_prefix_edit->setPlaceholderText("Archive File Prefix");
  make_archive_password_edit->setPlaceholderText("Password");
  open_archive_password_edit->setPlaceholderText("Password");
  make_archive_password_edit->setEchoMode(QLineEdit::Password);
  open_archive_password_edit->setEchoMode(QLineEdit::Password);
  archive_size_combo->setToolTip("Select archive size in L3 blocks");
  encryption_strength_combo->setToolTip("Select encryption strength");
  debug_console->setReadOnly(true);
  debug_console->setPlaceholderText("Debug console");
  debug_console->setStyleSheet(
      "QPlainTextEdit {"
      "  border: 1px solid #2f3a2f;"
      "  border-radius: 8px;"
      "  padding: 8px;"
      "  background-color: #030503;"
      "  color: #39ff14;"
      "  selection-background-color: #1f4d1f;"
      "}");
  for (QLabel* aLabel : {make_archive_loading_title,
                         make_archive_loading_detail,
                         open_archive_loading_title,
                         open_archive_loading_detail,
                         compare_loading_title,
                         compare_loading_detail}) {
    aLabel->setAlignment(Qt::AlignHCenter);
  }
  for (QLabel* aLabel : {make_archive_loading_detail, open_archive_loading_detail, compare_loading_detail}) {
    aLabel->setWordWrap(true);
  }
  if (QTabBar* const aTabBar = workflow_tabs->tabBar(); aTabBar != nullptr) {
    aTabBar->setFixedHeight(kTabHeight);
    aTabBar->installEventFilter(new WorkflowTabWheelFilter(workflow_tabs, aTabBar));
  }

  for (QLineEdit* aEdit : {make_archive_source_edit,
                           make_archive_destination_edit,
                           open_archive_source_edit,
                           open_archive_destination_edit,
                           compare_left_edit,
                           compare_right_edit,
                           make_archive_file_prefix_edit,
                           make_archive_password_edit,
                           open_archive_password_edit}) {
    aEdit->setFixedHeight(kElementHeight);
    aEdit->setTextMargins(0, 0, 0, 0);
    aEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 8px 12px;"
        "  border: 1px solid #3a3a3a;"
        "  border-radius: 8px;"
        "  background-color: #101010;"
        "}");
  }

  for (QPushButton* aButton : {make_archive_source_folder_button, open_archive_source_folder_button}) {
    aButton->setFixedHeight(kElementHeight);
    aButton->setFixedWidth(kSmallButtonWidth);
  }
  for (QPushButton* aButton : {make_archive_source_file_button, open_archive_source_file_button}) {
    aButton->setFixedHeight(kElementHeight);
    aButton->setFixedWidth(kFileButtonWidth);
  }

  for (QPushButton* aButton : {make_archive_destination_folder_button,
                               open_archive_destination_folder_button,
                               compare_left_folder_button,
                               compare_right_folder_button}) {
    aButton->setFixedHeight(kElementHeight);
    aButton->setFixedWidth(kButtonGroupWidth);
    aButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  for (QPushButton* aButton : {bundle_button, unbundle_button, compare_button, clear_logs_button}) {
    aButton->setFixedHeight(kElementHeight);
    aButton->setFixedWidth(kButtonGroupWidth);
    aButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  for (QPushButton* aButton : {make_archive_source_file_button,
                               make_archive_source_folder_button,
                               make_archive_destination_folder_button,
                               open_archive_source_file_button,
                               open_archive_source_folder_button,
                               open_archive_destination_folder_button,
                               compare_left_folder_button,
                               compare_right_folder_button,
                               bundle_button,
                               unbundle_button,
                               compare_button,
                               clear_logs_button,
                               scroll_to_bottom_button,
                               make_archive_loading_cancel_button,
                               open_archive_loading_cancel_button,
                               compare_loading_cancel_button}) {
    aButton->setEnabled(true);
    aButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #000000;"
        "  color: #f0f0f0;"
        "  border: 1px solid #2a2a2a;"
        "  border-radius: 8px;"
        "  padding: 0 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #111111;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1a1a1a;"
        "}"
        "QPushButton:disabled {"
        "  color: palette(mid);"
        "}");
  }

  bundle_button->setStyleSheet(
      "QPushButton {"
      "  background-color: #173e22;"
      "  color: #f7fff8;"
      "  border: 1px solid #3b8f54;"
      "  border-radius: 8px;"
      "  padding: 0 12px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #1d4c2a;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #14361e;"
      "}"
      "QPushButton:disabled {"
      "  color: palette(mid);"
      "}");
  unbundle_button->setStyleSheet(
      "QPushButton {"
      "  background-color: #162c46;"
      "  color: #f4f9ff;"
      "  border: 1px solid #447fbc;"
      "  border-radius: 8px;"
      "  padding: 0 12px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #1b395a;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #132438;"
      "}"
      "QPushButton:disabled {"
      "  color: palette(mid);"
      "}");
  compare_button->setStyleSheet(
      "QPushButton {"
      "  background-color: #4a3215;"
      "  color: #fffaf2;"
      "  border: 1px solid #c28b3b;"
      "  border-radius: 8px;"
      "  padding: 0 12px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #5b3d18;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #3c2811;"
      "}"
      "QPushButton:disabled {"
      "  color: palette(mid);"
      "}");

  clear_logs_button->setFixedHeight(24);
  clear_logs_button->setFixedWidth(120);
  clear_logs_button->setStyleSheet(
      "QPushButton {"
      "  background-color: #000000;"
      "  color: #f0f0f0;"
      "  border: 1px solid #2a2a2a;"
      "  border-radius: 8px;"
      "  padding: 0 10px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #111111;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #1a1a1a;"
      "}"
      "QPushButton:disabled {"
      "  color: palette(mid);"
      "}");
  scroll_to_bottom_button->setFixedHeight(24);
  scroll_to_bottom_button->setFixedWidth(140);
  scroll_to_bottom_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  scroll_to_bottom_button->setStyleSheet(
      "QPushButton {"
      "  background-color: #000000;"
      "  color: #f0f0f0;"
      "  border: 1px solid #2a2a2a;"
      "  border-radius: 8px;"
      "  padding: 0 10px;"
      "}"
      "QPushButton:hover {"
      "  background-color: #111111;"
      "}"
      "QPushButton:pressed {"
      "  background-color: #1a1a1a;"
      "}"
      "QPushButton:disabled {"
      "  color: palette(mid);"
      "}");

  for (QComboBox* aComboBox : {archive_size_combo, encryption_strength_combo}) {
    aComboBox->setFixedHeight(kElementHeight - 6);
    aComboBox->setMinimumHeight(kElementHeight - 6);
    aComboBox->setMaximumHeight(kElementHeight - 6);
    aComboBox->setFixedWidth(kButtonGroupWidth);
    aComboBox->setStyle(QStyleFactory::create("Fusion"));
    aComboBox->setStyleSheet(
        "QComboBox {"
        "  min-height: 50px;"
        "  max-height: 50px;"
        "  height: 50px;"
        "  padding: 2px 12px;"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "  background: #000000;"
        "  color: #f0f0f0;"
        "}"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: top right;"
        "  width: 28px;"
        "  border-left: 1px solid palette(mid);"
        "  border-top: 0px;"
        "  border-right: 0px;"
        "  border-bottom: 0px;"
        "}"
        "QComboBox::down-arrow {"
        "  width: 12px;"
        "  height: 12px;"
        "}");
    aComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  recovery_mode_checkbox->setFixedHeight(kElementHeight);
  recovery_mode_checkbox->setFixedWidth(kButtonGroupWidth);
  recovery_mode_checkbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  recovery_mode_checkbox->setLayoutDirection(Qt::RightToLeft);
  recovery_mode_checkbox->setStyleSheet("QCheckBox { spacing: 10px; }");

  auto* make_archive_options_host = new QWidget(make_archive_tab);
  auto* make_archive_options_layout = new QHBoxLayout(make_archive_options_host);
  auto* make_archive_options_label = new QLabel("Archive Options", make_archive_options_host);
  make_archive_options_layout->setContentsMargins(0, 0, 0, 0);
  make_archive_options_layout->setSpacing(8);
  make_archive_options_label->setMinimumWidth(140);
  make_archive_options_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  make_archive_options_layout->addWidget(make_archive_options_label);
  make_archive_options_layout->addWidget(make_archive_file_prefix_edit, 1);
  make_archive_options_layout->addWidget(archive_size_combo, 0, Qt::AlignRight);
  make_archive_options_layout->addWidget(encryption_strength_combo, 0, Qt::AlignRight);

  auto* make_archive_footer_host = new QWidget(make_archive_tab);
  auto* make_archive_footer_layout = new QHBoxLayout(make_archive_footer_host);
  auto* make_archive_password_label = new QLabel("Password", make_archive_footer_host);
  make_archive_footer_layout->setContentsMargins(0, 0, 0, 0);
  make_archive_footer_layout->setSpacing(8);
  make_archive_password_label->setMinimumWidth(140);
  make_archive_password_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  make_archive_footer_layout->addWidget(make_archive_password_label);
  make_archive_footer_layout->addWidget(make_archive_password_edit, 1);
  make_archive_footer_layout->addWidget(bundle_button, 0, Qt::AlignRight);

  auto* open_archive_footer_host = new QWidget(open_archive_tab);
  auto* open_archive_footer_layout = new QHBoxLayout(open_archive_footer_host);
  auto* open_archive_password_label = new QLabel("Password", open_archive_footer_host);
  auto* open_archive_checkbox_host = new QWidget(open_archive_tab);
  auto* open_archive_checkbox_layout = new QHBoxLayout(open_archive_checkbox_host);
  auto* open_archive_checkbox_spacer = new QLabel("", open_archive_checkbox_host);
  open_archive_footer_layout->setContentsMargins(0, 0, 0, 0);
  open_archive_footer_layout->setSpacing(8);
  open_archive_password_label->setMinimumWidth(140);
  open_archive_password_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  open_archive_footer_layout->addWidget(open_archive_password_label);
  open_archive_footer_layout->addWidget(open_archive_password_edit, 1);
  open_archive_footer_layout->addWidget(unbundle_button, 0, Qt::AlignRight);

  open_archive_checkbox_layout->setContentsMargins(0, 0, 0, 0);
  open_archive_checkbox_layout->setSpacing(8);
  open_archive_checkbox_spacer->setMinimumWidth(140);
  open_archive_checkbox_layout->addWidget(open_archive_checkbox_spacer);
  open_archive_checkbox_layout->addStretch(1);
  open_archive_checkbox_layout->addWidget(recovery_mode_checkbox, 0, Qt::AlignRight | Qt::AlignVCenter);

  auto* compare_footer_host = new QWidget(compare_tab);
  auto* compare_footer_layout = new QHBoxLayout(compare_footer_host);
  compare_footer_layout->setContentsMargins(0, 0, 0, 0);
  compare_footer_layout->setSpacing(8);
  compare_footer_layout->addStretch(1);
  compare_footer_layout->addWidget(compare_button, 0, Qt::AlignRight);

  for (QProgressBar* aIndicator : {make_archive_loading_indicator, open_archive_loading_indicator, compare_loading_indicator}) {
    aIndicator->setRange(0, 1000);
    aIndicator->setValue(0);
    aIndicator->setTextVisible(true);
    aIndicator->setFormat("%p%");
    aIndicator->setFixedHeight(kElementHeight);
    aIndicator->setFixedWidth(kButtonGroupWidth);
  }
  for (QPushButton* aButton : {make_archive_loading_cancel_button, open_archive_loading_cancel_button, compare_loading_cancel_button}) {
    aButton->setFixedHeight(kElementHeight);
    aButton->setFixedWidth(kButtonGroupWidth);
  }
  for (auto* aLayout : {make_archive_loading_layout, open_archive_loading_layout, compare_loading_layout}) {
    aLayout->setContentsMargins(0, 0, 0, 8);
    aLayout->addStretch(1);
  }
  make_archive_loading_layout->addWidget(make_archive_loading_title, 0, Qt::AlignHCenter);
  make_archive_loading_layout->addSpacing(16);
  make_archive_loading_layout->addWidget(make_archive_loading_indicator, 0, Qt::AlignHCenter);
  make_archive_loading_layout->addSpacing(16);
  make_archive_loading_layout->addWidget(make_archive_loading_detail, 0, Qt::AlignHCenter);
  make_archive_loading_layout->addStretch(1);
  make_archive_loading_layout->addWidget(make_archive_loading_cancel_button, 0, Qt::AlignHCenter | Qt::AlignBottom);
  open_archive_loading_layout->addWidget(open_archive_loading_title, 0, Qt::AlignHCenter);
  open_archive_loading_layout->addSpacing(16);
  open_archive_loading_layout->addWidget(open_archive_loading_indicator, 0, Qt::AlignHCenter);
  open_archive_loading_layout->addSpacing(16);
  open_archive_loading_layout->addWidget(open_archive_loading_detail, 0, Qt::AlignHCenter);
  open_archive_loading_layout->addStretch(1);
  open_archive_loading_layout->addWidget(open_archive_loading_cancel_button, 0, Qt::AlignHCenter | Qt::AlignBottom);
  compare_loading_layout->addWidget(compare_loading_title, 0, Qt::AlignHCenter);
  compare_loading_layout->addSpacing(16);
  compare_loading_layout->addWidget(compare_loading_indicator, 0, Qt::AlignHCenter);
  compare_loading_layout->addSpacing(16);
  compare_loading_layout->addWidget(compare_loading_detail, 0, Qt::AlignHCenter);
  compare_loading_layout->addStretch(1);
  compare_loading_layout->addWidget(compare_loading_cancel_button, 0, Qt::AlignHCenter | Qt::AlignBottom);

  peanutbutter::LocalFileSystem aFileSystem;
  const QJsonObject aConfigDefaults = loadConfigDefaults(aFileSystem);

  const QString aDefaultBundleSource = configStringValue(aConfigDefaults, "default_source_path", "input");
  const QString aDefaultArchivePath = configStringValue(aConfigDefaults, "default_archive_path", "archive");
  const QString aDefaultUnarchivePath = configStringValue(aConfigDefaults, "default_unarchive_path", "unzipped");

  make_archive_source_edit->setText(aDefaultBundleSource);
  make_archive_destination_edit->setText(aDefaultArchivePath);
  open_archive_source_edit->setText(aDefaultArchivePath);
  open_archive_destination_edit->setText(aDefaultUnarchivePath);
  compare_left_edit->setText(aDefaultBundleSource);
  compare_right_edit->setText(aDefaultUnarchivePath);
  make_archive_file_prefix_edit->setText(configStringValue(aConfigDefaults, "default_file_prefix", "bundle_"));
  const QString aDefaultPassword =
      configStringValue(aConfigDefaults, "default_password", "");
  make_archive_password_edit->setText(aDefaultPassword);
  open_archive_password_edit->setText(aDefaultPassword);
  recovery_mode_checkbox->setChecked(configBoolValue(aConfigDefaults, "default_recovery_mode", false));

  const std::uint32_t aConfiguredArchiveBlocks =
      configArchiveBlockCountValue(aConfigDefaults, archive_size_options[0].mBlocks);
  int aDefaultArchiveIndex = 4;
  for (std::size_t aIndex = 0; aIndex < archive_size_options.size(); ++aIndex) {
    const ArchiveSizeOption& aOption = archive_size_options[aIndex];
    archive_size_combo->addItem(QString::fromUtf8(aOption.mLabel) +
                                    QStringLiteral(" (%1 bytes)")
                                        .arg(QString::number(static_cast<qulonglong>(
                                            static_cast<std::uint64_t>(peanutbutter::kBlockSizeL3) * aOption.mBlocks))),
                                QVariant::fromValue(static_cast<qulonglong>(aOption.mBlocks)));
    if (aOption.mBlocks == aConfiguredArchiveBlocks) {
      aDefaultArchiveIndex = static_cast<int>(aIndex);
    }
  }
  archive_size_combo->setCurrentIndex(aDefaultArchiveIndex);

  int aDefaultEncryptionStrengthIndex = 0;
  for (std::size_t aIndex = 0; aIndex < encryption_strength_options.size(); ++aIndex) {
    const EncryptionStrengthOption& aOption = encryption_strength_options[aIndex];
    encryption_strength_combo->addItem(
        QString::fromUtf8(aOption.mLabel),
        QVariant::fromValue(static_cast<int>(static_cast<std::uint8_t>(aOption.mStrength))));
    if (aOption.mStrength == kDefaultBundleEncryptionStrength) {
      aDefaultEncryptionStrengthIndex = static_cast<int>(aIndex);
    }
  }
  encryption_strength_combo->setCurrentIndex(aDefaultEncryptionStrengthIndex);

  for (QLineEdit* aEdit : {make_archive_source_edit,
                           make_archive_destination_edit,
                           open_archive_source_edit,
                           open_archive_destination_edit,
                           compare_left_edit,
                           compare_right_edit}) {
    aEdit->setAcceptDrops(true);
  }

  make_archive_source_edit->installEventFilter(
      new PathDropFilter(aFileSystem, make_archive_source_edit, PathDropFilter::TargetType::Any));
  make_archive_destination_edit->installEventFilter(
      new PathDropFilter(aFileSystem, make_archive_destination_edit, PathDropFilter::TargetType::Folder));
  open_archive_source_edit->installEventFilter(
      new PathDropFilter(aFileSystem, open_archive_source_edit, PathDropFilter::TargetType::Any));
  open_archive_destination_edit->installEventFilter(
      new PathDropFilter(aFileSystem, open_archive_destination_edit, PathDropFilter::TargetType::Folder));
  compare_left_edit->installEventFilter(
      new PathDropFilter(aFileSystem, compare_left_edit, PathDropFilter::TargetType::Folder));
  compare_right_edit->installEventFilter(
      new PathDropFilter(aFileSystem, compare_right_edit, PathDropFilter::TargetType::Folder));

  make_archive_layout->setContentsMargins(12, 12, 12, 12);
  make_archive_layout->setSpacing(10);
  make_archive_layout->addWidget(makeLabeledPathRow(make_archive_tab,
                                                    "Source",
                                                    make_archive_source_edit,
                                                    make_archive_source_file_button,
                                                    make_archive_source_folder_button));
  make_archive_layout->addWidget(make_archive_separator_1);
  make_archive_layout->addWidget(makeLabeledPathRow(make_archive_tab,
                                                    "Archive Folder",
                                                    make_archive_destination_edit,
                                                    nullptr,
                                                    make_archive_destination_folder_button));
  make_archive_layout->addWidget(make_archive_separator_2);
  make_archive_layout->addWidget(make_archive_options_host);
  make_archive_layout->addWidget(make_archive_separator_3);
  make_archive_layout->addWidget(make_archive_footer_host);
  make_archive_layout->addStretch(1);

  open_archive_layout->setContentsMargins(12, 12, 12, 12);
  open_archive_layout->setSpacing(10);
  open_archive_layout->addWidget(makeLabeledPathRow(open_archive_tab,
                                                    "Archive Source",
                                                    open_archive_source_edit,
                                                    open_archive_source_file_button,
                                                    open_archive_source_folder_button));
  open_archive_layout->addWidget(open_archive_separator_1);
  open_archive_layout->addWidget(makeLabeledPathRow(open_archive_tab,
                                                    "Output Folder",
                                                    open_archive_destination_edit,
                                                    nullptr,
                                                    open_archive_destination_folder_button));
  open_archive_layout->addWidget(open_archive_separator_2);
  open_archive_layout->addWidget(open_archive_footer_host);
  open_archive_layout->addWidget(open_archive_checkbox_host);
  open_archive_layout->addStretch(1);

  compare_layout->setContentsMargins(12, 12, 12, 12);
  compare_layout->setSpacing(10);
  compare_layout->addWidget(makeLabeledPathRow(compare_tab,
                                               "Left Directory",
                                               compare_left_edit,
                                               nullptr,
                                               compare_left_folder_button));
  compare_layout->addWidget(compare_separator_1);
  compare_layout->addWidget(makeLabeledPathRow(compare_tab,
                                               "Right Directory",
                                               compare_right_edit,
                                               nullptr,
                                               compare_right_folder_button));
  compare_layout->addWidget(compare_separator_2);
  compare_layout->addWidget(compare_footer_host);
  compare_layout->addStretch(1);

  make_archive_stack->setContentsMargins(0, 0, 0, 0);
  make_archive_stack->addWidget(make_archive_content);
  make_archive_stack->addWidget(make_archive_loading);
  make_archive_stack->setCurrentWidget(make_archive_content);
  make_archive_tab_layout->setContentsMargins(0, 0, 0, 0);
  make_archive_tab_layout->addWidget(make_archive_stack_host);

  open_archive_stack->setContentsMargins(0, 0, 0, 0);
  open_archive_stack->addWidget(open_archive_content);
  open_archive_stack->addWidget(open_archive_loading);
  open_archive_stack->setCurrentWidget(open_archive_content);
  open_archive_tab_layout->setContentsMargins(0, 0, 0, 0);
  open_archive_tab_layout->addWidget(open_archive_stack_host);

  compare_stack->setContentsMargins(0, 0, 0, 0);
  compare_stack->addWidget(compare_content);
  compare_stack->addWidget(compare_loading);
  compare_stack->setCurrentWidget(compare_content);
  compare_tab_layout->setContentsMargins(0, 0, 0, 0);
  compare_tab_layout->addWidget(compare_stack_host);

  workflow_tabs->addTab(make_archive_tab, "Make Archive");
  workflow_tabs->addTab(open_archive_tab, "Open Archive");
  workflow_tabs->addTab(compare_tab, "Compare Directories");
  layout->addWidget(workflow_tabs, 1);
  logs_row_layout->setContentsMargins(0, 0, 0, 0);
  logs_row_layout->setSpacing(8);
  logs_row_host->setFixedHeight(24);
  logs_row_layout->addWidget(clear_logs_button, 0, Qt::AlignLeft);
  logs_row_layout->addWidget(scroll_to_bottom_button, 0, Qt::AlignLeft);
  logs_row_layout->addStretch(1);
  layout->addWidget(logs_row_host);
  layout->addWidget(debug_console, 1);

  const std::string aBaseDirectory = inferredBaseDirectory(aFileSystem);
  const std::string aSharedLogFilePath = aFileSystem.JoinPath(aBaseDirectory, "log_text_bundle_shared.txt");
  (void)aFileSystem.WriteTextFile(aSharedLogFilePath, std::string());

  PBQtShell aShell(&window,
                   {
                       {make_archive_stack,
                        make_archive_content,
                        make_archive_loading,
                        make_archive_loading_indicator,
                        make_archive_loading_title,
                        make_archive_loading_detail,
                        make_archive_loading_cancel_button},
                       {open_archive_stack,
                        open_archive_content,
                        open_archive_loading,
                        open_archive_loading_indicator,
                        open_archive_loading_title,
                        open_archive_loading_detail,
                        open_archive_loading_cancel_button},
                       {compare_stack,
                        compare_content,
                        compare_loading,
                        compare_loading_indicator,
                        compare_loading_title,
                        compare_loading_detail,
                        compare_loading_cancel_button},
                   },
                   debug_console);
  PBLogSession aLogger(
      aFileSystem,
      [&aShell](const std::string& pMessage, bool pIsError) {
        aShell.AppendLog(QString::fromStdString(pIsError ? "[error] " + pMessage : pMessage));
      },
      [&aShell](const peanutbutter::ProgressInfo& pProgress) {
        aShell.UpdateLoadingProgress(pProgress);
      });
  peanutbutter::CryptGenerator aCryptGenerator = MakePresetCryptGenerator();

  PBMockEngine aEngine(aFileSystem, std::move(aCryptGenerator), aLogger);
  PBQtController aEntryPoint(aShell, aEngine, aFileSystem, &window);

  const auto beginLogSession = [&](const char* pPrimaryFileName, bool pMirrorShared) {
    aLogger.BeginSession(aFileSystem.JoinPath(aBaseDirectory, pPrimaryFileName),
                         pMirrorShared ? aSharedLogFilePath : std::string());
  };

  QObject::connect(clear_logs_button, &QPushButton::clicked, debug_console, &QPlainTextEdit::clear);
  QObject::connect(scroll_to_bottom_button, &QPushButton::clicked, &window, [&]() {
    if (QScrollBar* const aScrollBar = debug_console->verticalScrollBar(); aScrollBar != nullptr) {
      aScrollBar->setValue(aScrollBar->maximum());
    }
  });

  QObject::connect(make_archive_source_file_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFile(&window, "Choose bundle source file");
    if (!aPath.isEmpty()) {
      make_archive_source_edit->setText(aPath);
    }
  });
  QObject::connect(make_archive_source_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose bundle source folder");
    if (!aPath.isEmpty()) {
      make_archive_source_edit->setText(aPath);
    }
  });
  QObject::connect(make_archive_destination_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose bundle destination folder");
    if (!aPath.isEmpty()) {
      make_archive_destination_edit->setText(aPath);
    }
  });

  QObject::connect(open_archive_source_file_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFile(&window, "Choose unbundle source file");
    if (!aPath.isEmpty()) {
      open_archive_source_edit->setText(aPath);
    }
  });
  QObject::connect(open_archive_source_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose unbundle source folder");
    if (!aPath.isEmpty()) {
      open_archive_source_edit->setText(aPath);
    }
  });
  QObject::connect(open_archive_destination_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose unbundle destination folder");
    if (!aPath.isEmpty()) {
      open_archive_destination_edit->setText(aPath);
    }
  });

  QObject::connect(compare_left_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose left compare folder");
    if (!aPath.isEmpty()) {
      compare_left_edit->setText(aPath);
    }
  });
  QObject::connect(compare_right_folder_button, &QPushButton::clicked, &window, [&]() {
    const QString aPath = pickFolder(&window, "Choose right compare folder");
    if (!aPath.isEmpty()) {
      compare_right_edit->setText(aPath);
    }
  });

  for (QPushButton* aButton : {make_archive_loading_cancel_button, open_archive_loading_cancel_button, compare_loading_cancel_button}) {
    QObject::connect(aButton, &QPushButton::clicked, &window, [&]() {
      aEntryPoint.PromptCancelActiveOperation();
    });
  }

  QObject::connect(bundle_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("log_text_bundle.txt", true);
    const std::size_t aSelectedBlocks =
        archive_size_combo->currentData().value<qulonglong>() > 0
            ? static_cast<std::size_t>(archive_size_combo->currentData().value<qulonglong>())
            : 1;
    const int aStrengthValue = encryption_strength_combo->currentData().toInt();
    PBBundleRequest aRequest;
    aRequest.mSourcePath = make_archive_source_edit->text().toStdString();
    aRequest.mDestinationPath = make_archive_destination_edit->text().toStdString();
    aRequest.mArchivePrefix = make_archive_file_prefix_edit->text().toStdString();
    aRequest.mPassword = make_archive_password_edit->text().toStdString();
    aRequest.mArchiveBlockCount = aSelectedBlocks;
    aRequest.mEncryptionStrength =
        static_cast<peanutbutter::EncryptionStrength>(static_cast<std::uint8_t>(aStrengthValue));
    aRequest.mUseEncryption = true;
    aEntryPoint.TriggerBundleFlow(aRequest);
  });

  QObject::connect(unbundle_button, &QPushButton::clicked, &window, [&]() {
    PBDecodeRequest aRequest;
    aRequest.mSourcePath = open_archive_source_edit->text().toStdString();
    aRequest.mDestinationPath = open_archive_destination_edit->text().toStdString();
    aRequest.mPassword = open_archive_password_edit->text().toStdString();
    aRequest.mUseEncryption = true;
    aRequest.mRecoveryMode = recovery_mode_checkbox->isChecked();
    if (aRequest.mRecoveryMode) {
      beginLogSession("log_text_recover.txt", true);
      aEntryPoint.TriggerRecoverFlow(aRequest);
      return;
    }
    beginLogSession("log_text_unbundle.txt", true);
    aEntryPoint.TriggerUnbundleFlow(aRequest);
  });

  QObject::connect(compare_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("sanity_logging.txt", false);
    PBSanityRequest aRequest;
    aRequest.mLeftPath = compare_left_edit->text().toStdString();
    aRequest.mRightPath = compare_right_edit->text().toStdString();
    aEntryPoint.TriggerSanityFlow(aRequest);
  });

  window.resize(1120, 720);
  window.show();
  return app.exec();
}
