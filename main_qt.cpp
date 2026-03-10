#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStackedLayout>
#include <QMetaObject>
#include <QPointer>
#include <QStyleFactory>
#include <QString>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstdint>

#include "AppCore.hpp"
#include "Encryption/RotateMaskBlockCipher.hpp"
#include "IO/LocalFileSystem.hpp"
#include "QtAppController.hpp"

namespace {

constexpr unsigned char kQtRotateMask = 0xAA;
constexpr int kQtRotateShift = 3;

struct ArchiveSizeOption {
  const char* mLabel = "";
  std::uint32_t mBlocks = 0;
};

const std::array<ArchiveSizeOption, 8> archive_size_options = {{
    {"1 Block", 1},
    {"5 Blocks", 5},
    {"10 Blocks", 10},
    {"20 Blocks", 20},
    {"50 Blocks", 50},
    {"100 Blocks", 100},
    {"200 Blocks", 200},
    {"400 Blocks", 400},
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
    if (mTargetType == TargetType::Folder) {
      return mFileSystem.IsDirectory(aLocalPath.toStdString()) ? aLocalPath : QString();
    }
    return mFileSystem.IsFile(aLocalPath.toStdString()) ? aLocalPath : QString();
  }

  const peanutbutter::FileSystem& mFileSystem;
  QLineEdit* mEdit = nullptr;
  TargetType mTargetType = TargetType::Folder;
};

QJsonObject loadConfigDefaults(const peanutbutter::FileSystem& pFileSystem) {
  std::string aConfigText;
  if (!pFileSystem.ReadTextFile(resolveConfigPath(pFileSystem), aConfigText)) {
    return {};
  }

  const QJsonDocument aDocument = QJsonDocument::fromJson(QByteArray::fromStdString(aConfigText));
  return aDocument.isObject() ? aDocument.object() : QJsonObject{};
}

QString configStringValue(const QJsonObject& pObject, const char* pKey, const char* pFallback) {
  const QJsonValue aValue = pObject.value(QString::fromUtf8(pKey));
  return aValue.isString() ? aValue.toString() : QString::fromUtf8(pFallback);
}

std::uint32_t configArchiveBlockCountValue(const QJsonObject& pObject, std::uint32_t pFallback) {
  const QJsonValue aValue = pObject.value(QStringLiteral("default_archive_blocks"));
  return aValue.isDouble() ? static_cast<std::uint32_t>(aValue.toDouble()) : pFallback;
}

class FakeAppShellQt final : public peanutbutter::AppShell {
 public:
  FakeAppShellQt(QWidget* pWindow,
                 QStackedLayout* pContentStack,
                 QWidget* pContentWidget,
                 QWidget* pLoadingWidget,
                 QStackedLayout* pActionRowStack,
                 QWidget* pActionButtonsRow,
                 QWidget* pActionSpinnerRow,
                 QPlainTextEdit* pDebugConsole)
      : mWindow(pWindow),
        mContentStack(pContentStack),
        mContentWidget(pContentWidget),
        mLoadingWidget(pLoadingWidget),
        mActionRowStack(pActionRowStack),
        mActionButtonsRow(pActionButtonsRow),
        mActionSpinnerRow(pActionSpinnerRow),
        mDebugConsole(pDebugConsole) {}

  void AppendLog(const QString& pMessage) {
    QPointer<QPlainTextEdit> aDebugConsole = mDebugConsole;
    QMetaObject::invokeMethod(
        mWindow,
        [aDebugConsole, pMessage]() {
          if (aDebugConsole == nullptr) {
            return;
          }
          aDebugConsole->appendPlainText(pMessage);
          QScrollBar* const aScrollBar = aDebugConsole->verticalScrollBar();
          if (aScrollBar != nullptr) {
            aScrollBar->setValue(aScrollBar->maximum());
          }
        },
        Qt::QueuedConnection);
  }

  void SetLoading(bool pEnabled) override {
    if (mContentStack != nullptr && mContentWidget != nullptr && mLoadingWidget != nullptr) {
      mContentStack->setCurrentWidget(pEnabled ? mLoadingWidget : mContentWidget);
    }
    if (mActionRowStack != nullptr && mActionButtonsRow != nullptr && mActionSpinnerRow != nullptr) {
      mActionRowStack->setCurrentWidget(pEnabled ? mActionSpinnerRow : mActionButtonsRow);
    }
  }

  void ShowError(const std::string& pTitle, const std::string& pMessage) override {
    if (mWindow == nullptr) {
      return;
    }
    QMessageBox::warning(mWindow, QString::fromStdString(pTitle), QString::fromStdString(pMessage));
  }

  peanutbutter::DestinationAction PromptDestinationAction(
      const std::string& pOperationName,
      const std::string& pDestinationPath) override {
    if (mWindow == nullptr) {
      return peanutbutter::DestinationAction::Cancel;
    }

    QMessageBox aBox(mWindow);
    aBox.setIcon(QMessageBox::Question);
    aBox.setWindowTitle(QString::fromStdString(pOperationName + " Destination"));
    aBox.setText(QString::fromStdString("Choose how to use:\n" + pDestinationPath));
    QPushButton* const aCancelButton = aBox.addButton("Cancel", QMessageBox::RejectRole);
    QPushButton* const aClearButton = aBox.addButton("Clear", QMessageBox::DestructiveRole);
    QPushButton* const aMergeButton = aBox.addButton("Merge", QMessageBox::AcceptRole);
    aBox.exec();
    if (aBox.clickedButton() == aClearButton) {
      return peanutbutter::DestinationAction::Clear;
    }
    if (aBox.clickedButton() == aMergeButton) {
      return peanutbutter::DestinationAction::Merge;
    }
    return peanutbutter::DestinationAction::Cancel;
  }

 private:
  QWidget* mWindow = nullptr;
  QStackedLayout* mContentStack = nullptr;
  QWidget* mContentWidget = nullptr;
  QWidget* mLoadingWidget = nullptr;
  QStackedLayout* mActionRowStack = nullptr;
  QWidget* mActionButtonsRow = nullptr;
  QWidget* mActionSpinnerRow = nullptr;
  QPlainTextEdit* mDebugConsole = nullptr;
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QWidget window;
  window.setWindowTitle("peanut butter ultima");
  window.setMinimumSize(720, 640);

  auto* layout = new QGridLayout(&window);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);
  for (int aColumn = 0; aColumn < 8; ++aColumn) {
    layout->setColumnStretch(aColumn, aColumn == 7 ? 0 : 1);
  }

  auto* source_edit = new QLineEdit(&window);
  auto* archive_edit = new QLineEdit(&window);
  auto* unarchive_edit = new QLineEdit(&window);
  auto* recovery_edit = new QLineEdit(&window);
  auto* file_prefix_edit = new QLineEdit(&window);
  auto* file_suffix_edit = new QLineEdit(&window);
  auto* password1_edit = new QLineEdit(&window);
  auto* password2_edit = new QLineEdit(&window);
  auto* archive_size_combo = new QComboBox(&window);
  auto* clear_logs_button = new QPushButton("Clear Logs", &window);
  auto* source_clear_button = new QToolButton(&window);
  auto* source_pick_button = new QToolButton(&window);
  auto* archive_clear_button = new QToolButton(&window);
  auto* archive_pick_button = new QToolButton(&window);
  auto* unarchive_clear_button = new QToolButton(&window);
  auto* unarchive_pick_button = new QToolButton(&window);
  auto* recovery_clear_button = new QToolButton(&window);
  auto* recovery_pick_button = new QToolButton(&window);
  auto* divider_inputs = new QFrame(&window);
  auto* divider_passwords = new QFrame(&window);
  auto* divider_actions = new QFrame(&window);
  auto* password_row_host = new QWidget(&window);
  auto* password_row_layout = new QHBoxLayout(password_row_host);
  auto* naming_row_host = new QWidget(&window);
  auto* naming_row_layout = new QHBoxLayout(naming_row_host);
  auto* pack_button = new QPushButton("Bundle", &window);
  auto* unpack_button = new QPushButton("Unbundle", &window);
  auto* sanity_button = new QPushButton("Sanity", &window);
  auto* recover_button = new QPushButton("Recover", &window);
  auto* action_spinner = new QProgressBar(&window);
  auto* controls_row_host = new QWidget(&window);
  auto* controls_row_layout = new QHBoxLayout(controls_row_host);
  auto* action_row_host = new QWidget(&window);
  auto* action_buttons_row = new QWidget(action_row_host);
  auto* action_spinner_row = new QWidget(action_row_host);
  auto* action_row_stack = new QStackedLayout(action_row_host);
  auto* action_buttons_layout = new QHBoxLayout(action_buttons_row);
  auto* action_spinner_layout = new QHBoxLayout(action_spinner_row);
  auto* action_left_group = new QWidget(action_buttons_row);
  auto* action_right_group = new QWidget(action_buttons_row);
  auto* action_left_layout = new QHBoxLayout(action_left_group);
  auto* action_right_layout = new QHBoxLayout(action_right_group);
  auto* debug_console = new QPlainTextEdit(&window);
  auto* content_host = new QWidget(&window);
  auto* content_stack = new QStackedLayout(content_host);
  auto* content_widget = new QWidget(content_host);
  auto* loading_widget = new QWidget(content_host);
  auto* loading_layout = new QVBoxLayout(loading_widget);
  auto* loading_indicator = new QProgressBar(loading_widget);

  source_edit->setPlaceholderText("pack source folder -> archive folder");
  archive_edit->setPlaceholderText("archive folder -> unarchive folder");
  unarchive_edit->setPlaceholderText("unarchive folder");
  recovery_edit->setPlaceholderText("recovery start file");
  file_prefix_edit->setPlaceholderText("file_prefix");
  file_suffix_edit->setPlaceholderText("file_suffix");
  password1_edit->setPlaceholderText("Password1");
  password2_edit->setPlaceholderText("Password2");
  archive_size_combo->setToolTip("Select archive size in L3 blocks");
  password1_edit->setEchoMode(QLineEdit::Password);
  password2_edit->setEchoMode(QLineEdit::Password);

  for (QLineEdit* aEdit : {source_edit, archive_edit, unarchive_edit, recovery_edit,
                           file_prefix_edit, file_suffix_edit, password1_edit, password2_edit}) {
    aEdit->setTextMargins(0, 4, 0, 4);
  }

  source_clear_button->setText("X");
  archive_clear_button->setText("X");
  unarchive_clear_button->setText("X");
  recovery_clear_button->setText("X");
  source_pick_button->setText("Folder...");
  archive_pick_button->setText("Folder...");
  unarchive_pick_button->setText("Folder...");
  recovery_pick_button->setText("File...");

  constexpr int kInputHeight = 44;
  constexpr int kActionHeight = 54;
  for (QLineEdit* aEdit : {source_edit, archive_edit, unarchive_edit, recovery_edit,
                           file_prefix_edit, file_suffix_edit, password1_edit, password2_edit}) {
    aEdit->setFixedHeight(kInputHeight);
  }
  archive_size_combo->setFixedHeight(kActionHeight);
  archive_size_combo->setMinimumHeight(kActionHeight);
  archive_size_combo->setMaximumHeight(kActionHeight);
  archive_size_combo->setStyle(QStyleFactory::create("Fusion"));
  archive_size_combo->setStyleSheet(
      "QComboBox {"
      "  min-height: 44px;"
      "  max-height: 44px;"
      "  height: 44px;"
      "  padding: 0 0px;"
      "  border: 1px solid palette(mid);"
      "  border-radius: 0px;"
      "  background: palette(base);"
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
  archive_size_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  source_clear_button->setFixedSize(54, kInputHeight);
  archive_clear_button->setFixedSize(54, kInputHeight);
  unarchive_clear_button->setFixedSize(54, kInputHeight);
  recovery_clear_button->setFixedSize(54, kInputHeight);
  source_pick_button->setFixedSize(72, kInputHeight);
  archive_pick_button->setFixedSize(72, kInputHeight);
  unarchive_pick_button->setFixedSize(72, kInputHeight);
  recovery_pick_button->setFixedSize(72, kInputHeight);
  clear_logs_button->setFixedHeight(kActionHeight);
  clear_logs_button->setMinimumHeight(kActionHeight);
  clear_logs_button->setMaximumHeight(kActionHeight);
  clear_logs_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  for (QFrame* aDivider : {divider_inputs, divider_passwords, divider_actions}) {
    aDivider->setFrameShape(QFrame::HLine);
    aDivider->setFrameShadow(QFrame::Sunken);
  }

  for (QPushButton* aButton : {pack_button, unpack_button, sanity_button, recover_button}) {
    aButton->setFixedHeight(kActionHeight);
    aButton->setMinimumHeight(kActionHeight);
    aButton->setMaximumHeight(kActionHeight);
    aButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  action_spinner->setRange(0, 0);
  action_spinner->setTextVisible(false);
  action_spinner->setFixedHeight(kActionHeight);
  action_spinner->setMinimumHeight(kActionHeight);
  action_spinner->setMaximumHeight(kActionHeight);
  action_spinner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  controls_row_host->setFixedHeight(kActionHeight);
  controls_row_host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  password_row_layout->setContentsMargins(0, 0, 0, 0);
  password_row_layout->setSpacing(8);
  password_row_layout->addWidget(password1_edit, 1);
  password_row_layout->addWidget(password2_edit, 1);
  naming_row_layout->setContentsMargins(0, 0, 0, 0);
  naming_row_layout->setSpacing(8);
  naming_row_layout->addWidget(file_prefix_edit, 1);
  naming_row_layout->addWidget(file_suffix_edit, 1);
  controls_row_layout->setContentsMargins(0, 0, 0, 0);
  controls_row_layout->setSpacing(8);
  controls_row_layout->addWidget(archive_size_combo, 1);
  controls_row_layout->addWidget(clear_logs_button, 1);
  action_row_host->setFixedHeight(kActionHeight);
  action_row_host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  action_buttons_layout->setContentsMargins(0, 0, 0, 0);
  action_buttons_layout->setSpacing(8);
  action_left_layout->setContentsMargins(0, 0, 0, 0);
  action_left_layout->setSpacing(8);
  action_left_layout->addWidget(pack_button, 1);
  action_left_layout->addWidget(unpack_button, 1);
  action_right_layout->setContentsMargins(0, 0, 0, 0);
  action_right_layout->setSpacing(8);
  action_right_layout->addWidget(sanity_button, 1);
  action_right_layout->addWidget(recover_button, 1);
  action_buttons_layout->addWidget(action_left_group, 1);
  action_buttons_layout->addWidget(action_right_group, 1);
  action_spinner_layout->setContentsMargins(0, 0, 0, 0);
  action_spinner_layout->setSpacing(0);
  action_spinner_layout->addWidget(action_spinner, 1);
  action_row_stack->setContentsMargins(0, 0, 0, 0);
  action_row_stack->addWidget(action_buttons_row);
  action_row_stack->addWidget(action_spinner_row);
  action_row_stack->setCurrentWidget(action_buttons_row);

  debug_console->setReadOnly(true);
  debug_console->setPlaceholderText("Debug console");

  loading_indicator->setRange(0, 0);
  loading_indicator->setTextVisible(false);
  loading_indicator->setFixedSize(56, 56);
  loading_widget->setAutoFillBackground(true);
  loading_layout->setContentsMargins(0, 0, 0, 0);
  loading_layout->addStretch(1);
  loading_layout->addWidget(loading_indicator, 0, Qt::AlignHCenter);
  loading_layout->addStretch(1);

  peanutbutter::LocalFileSystem aFileSystem;
  const QJsonObject aConfigDefaults = loadConfigDefaults(aFileSystem);
  source_edit->setText(configStringValue(aConfigDefaults, "default_source_path", "input"));
  archive_edit->setText(configStringValue(aConfigDefaults, "default_archive_path", "archive"));
  unarchive_edit->setText(configStringValue(aConfigDefaults, "default_unarchive_path", "unzipped"));
  recovery_edit->setText(configStringValue(aConfigDefaults, "default_recovery_path", "recovery_start.PBTR"));
  file_prefix_edit->setText(configStringValue(aConfigDefaults, "default_file_prefix", "bundle_"));
  file_suffix_edit->setText(configStringValue(aConfigDefaults, "default_file_suffix", ".PBTR"));
  password1_edit->setText(configStringValue(aConfigDefaults, "default_password_1", ""));
  password2_edit->setText(configStringValue(aConfigDefaults, "default_password_2", ""));

  const std::uint32_t aConfiguredArchiveBlocks =
      configArchiveBlockCountValue(aConfigDefaults, archive_size_options[0].mBlocks);
  int aDefaultArchiveIndex = 0;
  for (std::size_t aIndex = 0; aIndex < archive_size_options.size(); ++aIndex) {
    const ArchiveSizeOption& aOption = archive_size_options[aIndex];
    archive_size_combo->addItem(QString::fromUtf8(aOption.mLabel) +
                                    QStringLiteral(" (%1 bytes)")
                                        .arg(QString::number(static_cast<qulonglong>(
                                            static_cast<std::uint64_t>(peanutbutter::SB_L3_LENGTH) * aOption.mBlocks))),
                                QVariant::fromValue(static_cast<qulonglong>(aOption.mBlocks)));
    if (aOption.mBlocks == aConfiguredArchiveBlocks) {
      aDefaultArchiveIndex = static_cast<int>(aIndex);
    }
  }
  archive_size_combo->setCurrentIndex(aDefaultArchiveIndex);

  for (QLineEdit* aEdit : {source_edit, archive_edit, unarchive_edit, recovery_edit}) {
    aEdit->setAcceptDrops(true);
  }
  source_edit->installEventFilter(new PathDropFilter(aFileSystem, source_edit, PathDropFilter::TargetType::Folder));
  archive_edit->installEventFilter(new PathDropFilter(aFileSystem, archive_edit, PathDropFilter::TargetType::Folder));
  unarchive_edit->installEventFilter(new PathDropFilter(aFileSystem, unarchive_edit, PathDropFilter::TargetType::Folder));
  recovery_edit->installEventFilter(new PathDropFilter(aFileSystem, recovery_edit, PathDropFilter::TargetType::File));

  auto* content_layout = new QGridLayout(content_widget);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(8);
  for (int aColumn = 0; aColumn < 8; ++aColumn) {
    content_layout->setColumnStretch(aColumn, aColumn == 7 ? 0 : 1);
  }

  content_layout->addWidget(source_clear_button, 0, 0);
  content_layout->addWidget(source_edit, 0, 1, 1, 6);
  content_layout->addWidget(source_pick_button, 0, 7);
  content_layout->addWidget(archive_clear_button, 1, 0);
  content_layout->addWidget(archive_edit, 1, 1, 1, 6);
  content_layout->addWidget(archive_pick_button, 1, 7);
  content_layout->addWidget(unarchive_clear_button, 2, 0);
  content_layout->addWidget(unarchive_edit, 2, 1, 1, 6);
  content_layout->addWidget(unarchive_pick_button, 2, 7);
  content_layout->addWidget(recovery_clear_button, 3, 0);
  content_layout->addWidget(recovery_edit, 3, 1, 1, 6);
  content_layout->addWidget(recovery_pick_button, 3, 7);
  content_layout->addWidget(divider_inputs, 4, 0, 1, 8);
  content_layout->addWidget(password_row_host, 5, 0, 1, 8);
  content_layout->addWidget(naming_row_host, 6, 0, 1, 8);
  content_layout->addWidget(divider_passwords, 7, 0, 1, 8);
  content_layout->addWidget(controls_row_host, 8, 0, 1, 8);
  content_layout->addWidget(divider_actions, 9, 0, 1, 8);
  content_layout->addWidget(action_row_host, 10, 0, 1, 8);
  content_layout->setRowStretch(11, 1);

  content_host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  debug_console->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  content_stack->setContentsMargins(0, 0, 0, 0);
  content_stack->addWidget(content_widget);
  content_stack->addWidget(loading_widget);
  content_stack->setCurrentWidget(content_widget);

  layout->addWidget(content_host, 0, 0, 12, 8);
  layout->addWidget(debug_console, 12, 0, 1, 8);
  layout->setRowStretch(12, 1);

  FakeAppShellQt aShell(&window,
                        content_stack,
                        content_widget,
                        loading_widget,
                        action_row_stack,
                        action_buttons_row,
                        action_spinner_row,
                        debug_console);
  peanutbutter::RotateMaskBlockCipher12 aCrypt(kQtRotateMask, kQtRotateShift);
  //peanutbutter::PassthroughCrypt aCrypt;
  

  peanutbutter::SessionLogger aLogger([&aShell](const std::string& pMessage, bool pIsError) {
    aShell.AppendLog(QString::fromStdString(pIsError ? "[error] " + pMessage : pMessage));
  });
  aLogger.SetFileSystem(&aFileSystem);
  const auto beginLogSession = [&](const char* pFileName) {
    aLogger.BeginSession(aFileSystem.JoinPath(inferredBaseDirectory(aFileSystem), pFileName));
  };
  peanutbutter::RuntimeSettings aSettings;
  const std::size_t aSelectedBlocks =
      archive_size_combo->currentData().value<qulonglong>() > 0
          ? static_cast<std::size_t>(archive_size_combo->currentData().value<qulonglong>())
          : 1;
  aSettings.mArchiveFileLength =
      peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * aSelectedBlocks);
  peanutbutter::ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);
  peanutbutter::QtAppController aEntryPoint(aShell, aCore, aFileSystem, &window);

  QObject::connect(source_clear_button, &QToolButton::clicked, source_edit, &QLineEdit::clear);
  QObject::connect(archive_clear_button, &QToolButton::clicked, archive_edit, &QLineEdit::clear);
  QObject::connect(unarchive_clear_button, &QToolButton::clicked, unarchive_edit, &QLineEdit::clear);
  QObject::connect(recovery_clear_button, &QToolButton::clicked, recovery_edit, &QLineEdit::clear);
  QObject::connect(clear_logs_button, &QPushButton::clicked, debug_console, &QPlainTextEdit::clear);

  QObject::connect(source_pick_button, &QToolButton::clicked, &window, [&]() {
    const QString aFolder = QFileDialog::getExistingDirectory(&window, "Pick pack source folder");
    if (!aFolder.isEmpty()) {
      source_edit->setText(aFolder);
    }
  });
  QObject::connect(archive_pick_button, &QToolButton::clicked, &window, [&]() {
    const QString aFolder = QFileDialog::getExistingDirectory(&window, "Pick archive folder");
    if (!aFolder.isEmpty()) {
      archive_edit->setText(aFolder);
    }
  });
  QObject::connect(unarchive_pick_button, &QToolButton::clicked, &window, [&]() {
    const QString aFolder = QFileDialog::getExistingDirectory(&window, "Pick unarchive folder");
    if (!aFolder.isEmpty()) {
      unarchive_edit->setText(aFolder);
    }
  });
  QObject::connect(recovery_pick_button, &QToolButton::clicked, &window, [&]() {
    const QString aFile = QFileDialog::getOpenFileName(&window, "Pick recovery start file");
    if (!aFile.isEmpty()) {
      recovery_edit->setText(aFile);
    }
  });

  QObject::connect(pack_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("bundle_logging.txt");
    const std::size_t aSelectedBlocks =
        archive_size_combo->currentData().value<qulonglong>() > 0
            ? static_cast<std::size_t>(archive_size_combo->currentData().value<qulonglong>())
            : 1;
    aSettings.mArchiveFileLength =
        peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + (peanutbutter::SB_L3_LENGTH * aSelectedBlocks);
    aCore.SetSettings(aSettings);
    peanutbutter::BundleRequest aRequest;
    aRequest.mSourceDirectory = source_edit->text().toStdString();
    aRequest.mDestinationDirectory = archive_edit->text().toStdString();
    aRequest.mArchivePrefix = file_prefix_edit->text().toStdString();
    aRequest.mArchiveSuffix = file_suffix_edit->text().toStdString();
    aRequest.mPasswordOne = password1_edit->text().toStdString();
    aRequest.mPasswordTwo = password2_edit->text().toStdString();
    aRequest.mArchiveBlockCount = aSelectedBlocks;
    aRequest.mUseEncryption = true;
    aEntryPoint.TriggerBundleFlow(aRequest);
  });
  QObject::connect(unpack_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("unbundle_logging.txt");
    aCore.SetSettings(aSettings);
    peanutbutter::UnbundleRequest aRequest;
    aRequest.mArchiveDirectory = archive_edit->text().toStdString();
    aRequest.mDestinationDirectory = unarchive_edit->text().toStdString();
    aRequest.mPasswordOne = password1_edit->text().toStdString();
    aRequest.mPasswordTwo = password2_edit->text().toStdString();
    aRequest.mUseEncryption = true;
    aEntryPoint.TriggerUnbundleFlow(aRequest);
  });
  QObject::connect(sanity_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("sanity_logging.txt");
    aCore.SetSettings(aSettings);
    peanutbutter::ValidateRequest aRequest;
    aRequest.mLeftDirectory = source_edit->text().toStdString();
    aRequest.mRightDirectory = unarchive_edit->text().toStdString();
    aEntryPoint.TriggerSanityFlow(aRequest);
  });
  QObject::connect(recover_button, &QPushButton::clicked, &window, [&]() {
    beginLogSession("recover_logging.txt");
    aCore.SetSettings(aSettings);
    peanutbutter::RecoverRequest aRequest;
    aRequest.mArchiveDirectory = archive_edit->text().toStdString();
    aRequest.mRecoveryStartFilePath = recovery_edit->text().toStdString();
    aRequest.mDestinationDirectory = unarchive_edit->text().toStdString();
    aRequest.mPasswordOne = password1_edit->text().toStdString();
    aRequest.mPasswordTwo = password2_edit->text().toStdString();
    aRequest.mUseEncryption = true;
    aEntryPoint.TriggerRecoverFlow(aRequest);
  });

  window.resize(720, 640);
  window.show();
  return app.exec();
}
