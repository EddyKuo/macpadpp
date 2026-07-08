#include "app/MainWindow.h"

#include "core/EditorWidget.h"
#include "core/LexerFactory.h"
#include "features/search/FindReplaceDialog.h"
#include "persistence/RecentFiles.h"
#include "persistence/SessionStore.h"
#include "persistence/SettingsStore.h"
#include "platform/ThemeManager.h"
#include "ui/EditorPane.h"
#include "extension/ExtensionRegistry.h"
#include "extension/builtin/WordCountExtension.h"
#include "extension/builtin/MarkdownPreviewExtension.h"
#include "features/findinfiles/FindInFilesDock.h"
#include "features/run/RunDock.h"
#include "features/run/RunCommand.h"
#include "features/udl/UdlLexer.h"
#include "features/export/HtmlExporter.h"
#include "features/textops/TextOps.h"
#include "features/mime/MimeTools.h"
#include "features/autocomplete/ApiDatabase.h"
#include "persistence/ThemeStore.h"
#include "ui/ThemePickerDialog.h"
#include "ui/SnapshotRecoveryDialog.h"
#include "features/findall/FindAllEngine.h"
#include "features/findall/FindAllDock.h"
#include "features/backup/BackupService.h"

#include <QDateTime>

#include <Qsci/qsciprinter.h>
#include <QPrintDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QTabBar>
#include "ui/DocumentListDock.h"
#include "ui/Panels.h"
#include "ui/CharacterPanel.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QClipboard>
#include <QScrollBar>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTabBar>
#include <QUrl>
#include "ui/WorkspaceDock.h"
#include "ui/PreferencesDialog.h"
#include "ui/ColumnEditorDialog.h"
#include "ui/StyleConfiguratorDialog.h"
#include "ui/ShortcutMapperDialog.h"
#include "ui/UdlEditorDialog.h"
#include "ui/ProjectPanelDock.h"
#include "ui/MacroManagerDialog.h"
#include "features/functionlist/FunctionListParser.h"
#include "features/columneditor/ColumnEditor.h"

#include <QCryptographicHash>
#include <QHash>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolBar>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

#include <Qsci/qscimacro.h>

#include <QApplication>
#include <QFileSystemWatcher>
#include <QStyleHints>
#include <QTimer>

using macpad::ui::EditorPane;

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QFrame>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QShortcut>
#include <QRadioButton>
#include <QSpinBox>
#include <QKeySequenceEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent>

using macpad::core::EditorWidget;


MainWindow::MainWindow(QWidget *parent, bool restoreSessionOnLaunch)
    : QMainWindow(parent)
{
    // Dual-View（雙檢視，複刻 Notepad++ 兩欄檢視）：兩個 QTabWidget 並排於水平 QSplitter。
    // 第二檢視預設隱藏，使用者把文件移動/複製過去時才出現，空了又自動隱藏。
    m_tabs = new QTabWidget;
    m_tabs2 = new QTabWidget;
    m_viewSplit = new QSplitter(Qt::Horizontal, this);
    m_viewSplit->addWidget(m_tabs);
    m_viewSplit->addWidget(m_tabs2);
    setCentralWidget(m_viewSplit);
    m_tabs2->hide();
    m_activeTabs = m_tabs;

    wireTabWidget(m_tabs);
    wireTabWidget(m_tabs2);

    // 作用中檢視追蹤：焦點落在哪個檢視內，該檢視即為作用中（狀態列/面板隨之切換）
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget *, QWidget *now) {
        if (!now)
            return;
        if (m_tabs && m_tabs->isAncestorOf(now))
            setActiveTabWidget(m_tabs);
        else if (m_tabs2 && m_tabs2->isAncestorOf(now))
            setActiveTabWidget(m_tabs2);
    });

    // 文件切換清單（FR-026）
    m_docList = new macpad::ui::DocumentListDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_docList);
    m_docList->hide();  // 預設隱藏，由 View 選單開啟
    connect(m_docList, &macpad::ui::DocumentListDock::activated, this, [this](int idx) {
        // 合併清單的列索引→(檢視, 分頁索引)（FR-062）
        if (idx < 0 || idx >= m_docListMap.size())
            return;
        QTabWidget *w = m_docListMap[idx].first;
        const int tab = m_docListMap[idx].second;
        if (w && tab >= 0 && tab < w->count()) {
            setActiveTabWidget(w);
            w->setCurrentIndex(tab);
        }
    });
    // 文件清單中鍵/右鍵「Close」→ 關閉對應分頁（同樣以合併索引解碼回檢視+分頁）
    connect(m_docList, &macpad::ui::DocumentListDock::closeRequested, this, [this](int idx) {
        if (idx < 0 || idx >= m_docListMap.size())
            return;
        QTabWidget *w = m_docListMap[idx].first;
        const int tab = m_docListMap[idx].second;
        if (w && tab >= 0 && tab < w->count()) {
            setActiveTabWidget(w);
            closeTabIn(w, tab);
        }
    });

    // 停靠面板（FR-029）：Function List / Clipboard History / Document Map
    m_funcList = new macpad::ui::FunctionListDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_funcList);
    m_funcList->hide();
    connect(m_funcList, &macpad::ui::FunctionListDock::symbolActivated, this, [this](int line) {
        if (EditorWidget *e = currentEditor()) {
            e->setCursorPosition(qMax(0, line - 1), 0);
            e->ensureLineVisible(qMax(0, line - 1));
            e->setFocus();
        }
    });

    m_clipHistory = new macpad::ui::ClipboardHistoryDock(this);
    addDockWidget(Qt::RightDockWidgetArea, m_clipHistory);
    m_clipHistory->hide();
    connect(m_clipHistory, &macpad::ui::ClipboardHistoryDock::pasteRequested, this,
            [this](const QString &text) {
        if (EditorWidget *e = currentEditor())
            e->insert(text);
    });

    m_docMap = new macpad::ui::DocumentMapDock(this);
    addDockWidget(Qt::RightDockWidgetArea, m_docMap);
    m_docMap->hide();
    connect(m_docMap, &macpad::ui::DocumentMapDock::lineClicked, this, [this](int line) {
        if (EditorWidget *e = currentEditor())
            e->ensureLineVisible(line);
    });

    // Folder as Workspace（FR-030）
    m_workspace = new macpad::ui::WorkspaceDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_workspace);
    m_workspace->hide();
    connect(m_workspace, &macpad::ui::WorkspaceDock::fileActivated,
            this, &MainWindow::openFile);
    // 工作區右鍵「在此資料夾中尋找」→ 開啟範圍限定於該資料夾的 Find in Files
    connect(m_workspace, &macpad::ui::WorkspaceDock::findInFolderRequested, this,
            [this](const QString &dir) {
        if (!m_findInFiles) {
            m_findInFiles = new macpad::features::FindInFilesDock(this);
            addDockWidget(Qt::BottomDockWidgetArea, m_findInFiles);
            connect(m_findInFiles, &macpad::features::FindInFilesDock::openLocation,
                    this, &MainWindow::openFileAtLine);
        }
        m_findInFiles->setSearchRoot(dir);
        m_findInFiles->show();
        m_findInFiles->raise();
    });

    // Character Panel（Edit ▸ Character Panel，Notepad++ 對等）
    m_charPanel = new macpad::ui::CharacterPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_charPanel);
    m_charPanel->hide();
    connect(m_charPanel, &macpad::ui::CharacterPanel::charChosen, this,
            [this](const QString &text) {
        if (EditorWidget *e = currentEditor()) {
            e->insert(text);
            int line = 0, col = 0;
            e->getCursorPosition(&line, &col);
            e->setCursorPosition(line, col + int(text.size()));
            e->setFocus();
        }
    });

    // Project Panel（Notepad++ Project Panel；邏輯樹組織多 project，FR-030 延伸）
    m_projectPanel = new macpad::ui::ProjectPanelDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectPanel);
    if (m_workspace)
        tabifyDockWidget(m_workspace, m_projectPanel);  // 與 Workspace 併為同一停靠標籤組
    m_projectPanel->hide();
    m_projectPanel->load();  // 讀取 projects.json（缺檔則為空，安全 no-op）
    connect(m_projectPanel, &macpad::ui::ProjectPanelDock::openFileRequested,
            this, &MainWindow::openFile);

    // Notepad++ 風格的圖示工具列——於 createMenus 前建立，讓 View 選單可加入其開關
    buildToolbar();

    createMenus();

    // 依偏好套用視窗層級外觀（工具列/狀態列/分頁列可見性與圖示大小、分頁關閉鈕等）
    applyWindowPrefs(macpad::persistence::SettingsStore::load());
    // 依已儲存的巨集/Run 指令快捷鍵建立全域快捷鍵
    rebuildMacroShortcuts();
    rebuildRunShortcuts();

    // 文件切換器（MISC 頁 docSwitcherEnabled）：Ctrl+Tab / Ctrl+Shift+Tab 前後切換分頁。
    // 於觸發時讀取即時設定，關閉時不動作。
    // docPeekerEnabled 的文件預覽由 Document List（DocumentListDock）hover tooltip 實作，見 refreshDocList。
    {
        auto *nextDoc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
        connect(nextDoc, &QShortcut::activated, this, [this] {
            if (macpad::persistence::SettingsStore::load().docSwitcherEnabled)
                activateTabRelative(1);
        });
        auto *prevDoc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
        connect(prevDoc, &QShortcut::activated, this, [this] {
            if (macpad::persistence::SettingsStore::load().docSwitcherEnabled)
                activateTabRelative(-1);
        });
    }

    // 狀態列（FR-022）——複刻 Notepad++ 的分格式狀態列（6 格，各格下凹分隔）
    createStatusCells();

    // 外部檔案異動監控（FR-018, IR-001）
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onFileChangedOnDisk);

    // 深色模式跟隨系統（FR-021, IR-002）——訂閱系統外觀變更
    if (QStyleHints *h = QApplication::styleHints()) {
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyTheme(); });
    }

    resize(1000, 720);

    m_udl.loadAll();  // 載入使用者自訂語言（FR-032）

    // 還原上次 session（FR-016）；若無則開空白分頁
    const auto settings = macpad::persistence::SettingsStore::load();
    if (settings.restoreOnLaunch && restoreSessionOnLaunch)
        restoreSession();
    if (m_tabs->count() == 0)
        newFile();
    rebuildRecentMenu();
    applyTheme();  // 還原後統一套用主題（含 lexer paper）

    // 當機復原（FR-054）：偵測上次未正常關閉遺留的快照，讓使用者選擇還原或丟棄。
    {
        const QStringList pending = macpad::features::BackupService::pendingSnapshots();
        if (!pending.isEmpty()) {
            macpad::ui::SnapshotRecoveryDialog dlg(pending, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (dlg.discardAll()) {
                    macpad::features::BackupService::clearSnapshots();
                } else {
                    for (const QString &id : dlg.selectedSnapshots()) {
                        EditorWidget *e = addEditorTab();
                        e->setText(QString::fromUtf8(
                            macpad::features::BackupService::readSnapshot(id)));
                    }
                    macpad::features::BackupService::clearSnapshots();  // 還原後清除，避免重覆提示
                }
            }
        }
    }

    // 擴充協定 dogfood（FR-035）：內建 WordCount 擴充透過協定掛載，驗證可擴充性
    m_extensions = std::make_unique<macpad::extension::ExtensionRegistry>(this);
    m_extensions->load(std::make_unique<macpad::extension::WordCountExtension>());
    m_extensions->load(std::make_unique<macpad::extension::MarkdownPreviewExtension>());

    // 自動儲存（FR-015）：預設關閉；啟用時定期存已命名之未存分頁
    if (settings.autosaveEnabled) {
        auto *timer = new QTimer(this);
        // 夾限區間（同 PreferencesDialog 的 [5,3600]），防止手改/損毀設定檔導致 0/負值 QTimer 空轉
        const int intervalSec = qBound(5, settings.autosaveIntervalSec, 3600);
        timer->setInterval(intervalSec * 1000);
        connect(timer, &QTimer::timeout, this, [this] {
            forEachEditor([](EditorWidget *e) {
                if (e && !e->isUntitled() && e->isDirty())
                    e->saveFile(e->filePath());
            });
            updateTabTitle();
        });
        timer->start();
    }

    // 當機復原快照（FR-054）：定期把「有未存變更」的分頁內容寫入 snapshot；
    // 正常關閉時 closeEvent 會 clearSnapshots()，故啟動時殘留者即代表上次未正常結束。
    // 是否啟用與間隔由偏好設定（enableSessionSnapshot / snapshotIntervalSec）決定，
    // 於 Preferences 變更後即時重新套用（見 Preferences 接受處）。
    m_snapshotTimer = new QTimer(this);
    connect(m_snapshotTimer, &QTimer::timeout, this, [this] {
        int i = 0;
        forEachEditor([&i](EditorWidget *e) {
            const int idx = i++;
            if (e && e->isDirty()) {
                const QString docId = e->isUntitled()
                    ? QStringLiteral("untitled-%1").arg(idx)
                    : QFileInfo(e->filePath()).fileName();
                macpad::features::BackupService::writeSnapshot(
                    QStringLiteral("session"), docId, e->text().toUtf8());
            }
        });
    });
    applySnapshotTimerSettings(settings);

    // 失焦自動儲存（FR-053）：視窗失去作用時存已命名之未存分頁
    connect(qApp, &QApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive
            && macpad::persistence::SettingsStore::load().autosaveOnFocusLoss) {
            forEachEditor([](EditorWidget *e) {
                if (e && !e->isUntitled() && e->isDirty())
                    e->saveFile(e->filePath());
            });
            updateTabTitle();
        }
    });
}


MainWindow::~MainWindow() = default;


// --- IHostServices（extension protocol，FR-035）--------------------------

EditorWidget *MainWindow::activeEditor()
{
    return currentEditor();
}


void MainWindow::addMenuAction(const QString &menuTitle, const QString &text,
                               std::function<void()> callback)
{
    // 先以 objectName(英文鍵)比對——翻譯後 title 會變,故不能用 title；再退回 title 比對
    QMenu *target = nullptr;
    const auto menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (m->objectName() == menuTitle) { target = m; break; }
    }
    if (!target) {
        for (QMenu *m : menus) {
            if (QString(m->title()).remove(QLatin1Char('&')) == menuTitle) { target = m; break; }
        }
    }
    if (!target) {
        target = menuBar()->addMenu(menuTitle);
        target->setObjectName(menuTitle);
    }
    target->addAction(text, this, [cb = std::move(callback)] { cb(); });
}


void MainWindow::showStatusMessage(const QString &message, int timeoutMs)
{
    statusBar()->showMessage(message, timeoutMs);
}

// 依目前 lexer 反查 LexerFactory 語言鍵（FR-052 languageOverride）。
// 以 LexerFactory::languages() 逐一建立範例 lexer 比對 language() 字串。
QString MainWindow::languageKeyForLexer(QsciLexer *lex)
{
    if (!lex)
        return QString();
    static const QHash<QString, QString> nameToKey = [] {
        QHash<QString, QString> map;
        for (const auto &entry : macpad::core::LexerFactory::languages()) {
            if (entry.key.isEmpty())
                continue;
            std::unique_ptr<QsciLexer> tmp(macpad::core::LexerFactory::createForLanguage(entry.key, nullptr));
            if (tmp)
                map.insert(QString::fromLatin1(tmp->language()), entry.key);
        }
        return map;
    }();
    return nameToKey.value(QString::fromLatin1(lex->language()));
}
