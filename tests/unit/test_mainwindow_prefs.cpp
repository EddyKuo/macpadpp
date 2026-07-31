// 單元測試：偏好設定的「編輯 → 持久化 → 套用」整條路徑。
//   - src/ui/PreferencesDialog.cpp   ：20 個分頁的建構與 result() 的欄位對映
//   - src/app/MainWindow_Prefs.cpp   ：applyEditorPrefs / applyViewPrefs / applyWindowPrefs /
//                                      applyDelimiters / applyPerLangTabWidth /
//                                      startDirForDialog / 命令列視窗選項 / 系統匣 / UDL / 監控
//
// 測試策略：
//  1. 偏好設定的價值不在「對話框長出來了」，而在「按下 OK 之後設定真的寫進 settings.json，
//     而且真的改變了編輯器與視窗的行為」。因此斷言一律看
//     SettingsStore::load() 的內容 + EditorWidget/QToolBar/QTabWidget 的實際狀態，
//     不看 widget 自己的值（那只是在驗證自己剛剛設下去的東西）。
//  2. PreferencesDialog 的成員全為 private，故一律以「分頁名稱 + 該頁直接子物件的建立順序」
//     定位 widget（見 prefWidget()）——這正是使用者在畫面上看到的順序，
//     頁面欄位若被重排或刪除，測試會立刻失敗而不是靜靜地少測一項。
//  3. 所有 exec() 的 modal 對話框都由 driveNextModal() 在事件迴圈中接手（填值→accept/reject），
//     並帶 3 秒看門狗；任何情況下測試都不會卡住。
//  4. 設定目錄以 AppPaths::setConfigDirOverride() 導向暫存目錄，完全不碰使用者真實設定。
//
// 刻意不測（於各處另有註解說明）：
//   - checkForUpdates() / downloadUpdate()：兩者第一件事就是建立 UpdateChecker /
//     UpdateDownloader 並對 GitHub 發出 HTTPS 請求。測試環境嚴禁對外連網，
//     且該路徑沒有可注入的網路層抽象（UpdateChecker 內部自建 QNetworkAccessManager），
//     無法在不連網的前提下驅動其 finished 回呼。改由 test_updatechecker.cpp
//     以純解析函式涵蓋版本比較/資產挑選邏輯。

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <Qsci/qsciscintillabase.h>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlLexer.h"
#include "features/udl/UdlManager.h"
#include "persistence/AppPaths.h"
#include "persistence/SettingsStore.h"
#include "platform/FileAssociation.h"
#include "ui/MultiRowTabBar.h"
#include "ui/PreferencesDialog.h"
#include "ui/WorkspaceDock.h"

using macpad::core::EditorWidget;
using macpad::persistence::AppPaths;
using macpad::persistence::BackupMode;
using macpad::persistence::DefaultDirPolicy;
using macpad::persistence::EdgeMode;
using macpad::persistence::FileStatusAutoDetectMode;
using macpad::persistence::FoldMarginStyle;
using macpad::persistence::MultiInstanceMode;
using macpad::persistence::Settings;
using macpad::persistence::SettingsStore;
using macpad::persistence::ThemeMode;
using macpad::persistence::ToolbarIconSize;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

// 以文字尋找視窗中的 QAction（含選單、應用程式選單 role 動作）。
// 這些動作沒有 objectName，且測試不載入翻譯，tr() 會原樣回傳原始字串。
static QAction *findAction(MainWindow &w, const QString &text)
{
    const auto acts = w.findChildren<QAction *>();
    for (QAction *a : acts) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

// 在下一個 modal 對話框出現時接手處理，避免 exec() 讓測試永久阻塞。
// fn 通常會 accept()/reject()；若沒關，這裡強制 reject()。
// 逾時（預設 3 秒）則放棄輪詢——代表根本沒有對話框出現，測試不會因此卡住。
static void driveNextModal(const std::function<void(QDialog *)> &fn,
                           bool *fired = nullptr, int timeoutMs = 3000)
{
    auto *timer = new QTimer;
    timer->setInterval(5);
    const QDeadlineTimer deadline(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, fn, fired, deadline] {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            timer->stop();
            timer->deleteLater();
            if (auto *dlg = qobject_cast<QDialog *>(modal)) {
                if (fired)
                    *fired = true;
                fn(dlg);
                if (dlg->isVisible())
                    dlg->reject();   // 保險：handler 沒關掉時強制收尾
            } else {
                modal->close();
            }
            return;
        }
        if (deadline.hasExpired()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

// Preferences 對話框中名為 tabName 的分頁
static QWidget *prefPage(QDialog *dlg, const QString &tabName)
{
    auto *tabs = dlg->findChild<QTabWidget *>();
    if (!tabs)
        return nullptr;
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabText(i) == tabName)
            return tabs->widget(i);
    }
    return nullptr;
}

// 分頁中第 index 個型別為 T 的「直接」子 widget（建立順序 = 畫面上的順序）。
// 必須是直接子物件：否則 QSpinBox/QComboBox 內部的 QLineEdit 會被一併撈進來。
template <typename T>
static T prefWidget(QDialog *dlg, const QString &tabName, int index)
{
    QWidget *page = prefPage(dlg, tabName);
    if (!page)
        return nullptr;
    const auto list = page->findChildren<T>(QString(), Qt::FindDirectChildrenOnly);
    return index < list.size() ? list.at(index) : nullptr;
}

// 主視窗的兩個檢視容器（中央 QSplitter 的直接子物件）
static QList<QTabWidget *> viewTabs(MainWindow &w)
{
    auto *split = qobject_cast<QSplitter *>(w.centralWidget());
    if (!split)
        return {};
    return split->findChildren<QTabWidget *>(QString(), Qt::FindDirectChildrenOnly);
}

static QToolBar *mainToolbar(MainWindow &w)
{
    return w.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
}

// 建立一個內容已知的暫存檔並回傳其路徑
static QString writeFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(content);
    f.close();
    return path;
}

// 「有意義地偏離預設值」的一組設定：每個欄位都與 Settings 的預設不同，
// 用於證明 round-trip 真的搬運了值，而不是兩邊剛好都等於預設。
static Settings makeDistinctiveSettings()
{
    Settings s;
    s.schemaVersion = 1;
    s.language = QStringLiteral("zh_TW");          // 對話框未暴露 → 必須被保留
    s.customColors = {QStringLiteral("#123456")};  // 同上

    s.theme = ThemeMode::Dark;
    s.tabWidth = 7;
    s.restoreOnLaunch = false;
    s.autosaveEnabled = true;
    s.autosaveIntervalSec = 15;
    s.singleInstance = false;

    s.showLineNumbers = false;
    s.showIndentGuides = false;
    s.wordWrap = true;
    s.showWhitespace = true;
    s.caretWidth = 3;
    s.currentLineHighlight = false;
    s.enableVirtualSpace = true;
    s.copyLineWithoutSelection = false;
    s.columnSelectionToMultiEdit = true;
    s.undoSelectionHistory = true;
    s.selectionDragDrop = false;
    s.syncZoomBetweenViews = true;
    s.openCopyAfterSaveACopy = true;
    s.advancedAutoIndent = false;

    s.hiddenToolbarButtons = {QStringLiteral("open"), QStringLiteral("find")};

    s.printHeader = QStringLiteral("H:$(FILE_NAME)");
    s.printFooter = QStringLiteral("F:$(CURRENT_PAGE)");
    s.printColourMode = 3;
    s.printMarginMm = 25;
    s.printFormFeedAsPageBreak = true;
    s.incrementalSearchCount = true;

    s.defaultEol = macpad::core::Eol::Cr;
    s.defaultEncoding = macpad::core::Encoding::Utf16BE;
    s.autoDetectFileStatus = false;
    s.sessionFileExt = QStringLiteral("mysession");

    s.backupMode = BackupMode::Verbose;
    s.backupDir = QStringLiteral("/tmp/macpad-backup-test");
    s.autosaveOnFocusLoss = true;
    s.enableSessionSnapshot = false;
    s.snapshotIntervalSec = 90;

    s.autoInsertPairs = false;
    s.wordAutoComplete = false;
    s.acThreshold = 5;
    s.showCallTips = false;

    s.largeFileMB = 321;
    s.disableAutoCompleteOverMB = 12;

    s.searchEngineUrl = QStringLiteral("https://example.invalid/?q=%s");
    s.keepFindDialogOpen = false;
    s.confirmReplaceAll = false;
    s.findInSelectionThreshold = 1234;

    s.smartHighlight = false;
    s.highlightMatchingTags = true;
    s.edgeColumn = 77;
    s.multiEdgeEnabled = true;
    s.showWrapSymbol = true;
    s.showEol = true;

    s.showToolbar = false;
    s.showStatusBar = false;
    s.showTabBar = false;
    s.caretBlinkRate = 250;

    s.toolbarIconSize = ToolbarIconSize::Large;

    s.tabBarMultiLine = true;
    s.tabBarVertical = true;
    s.tabBarShowCloseButton = false;
    s.tabBarDoubleClickCloses = true;
    s.tabBarLabelMaxLength = 42;
    s.tabBarUntitledNameFromFirstLine = true;

    s.edgeMode = EdgeMode::Background;
    s.foldMarginStyle = FoldMarginStyle::Circle;
    s.lineNumberMargin = false;

    s.defaultDirPolicy = DefaultDirPolicy::FixedPath;
    s.defaultDirFixedPath = QStringLiteral("/tmp");

    s.recentFilesMaxEntries = 25;
    s.recentFilesShowFullPath = true;
    s.recentFilesInSubmenu = true;

    s.disabledLanguages = {QStringLiteral("Python"), QStringLiteral("JSON")};
    s.perLangTabWidth = {{QStringLiteral("C++"), 4}, {QStringLiteral("python"), 2}};

    s.multiInstanceMode = MultiInstanceMode::AlwaysMulti;
    s.dateFormat = QStringLiteral("yyyy/MM/dd");
    s.customDateFormat = QStringLiteral("dd-MMM");

    s.delimiterChars = QStringLiteral(".:");
    s.ctrlDoubleClickWholeWord = false;

    s.docSwitcherEnabled = false;
    s.docPeekerEnabled = false;
    s.fileStatusAutoDetect = FileStatusAutoDetectMode::EnabledSilent;
    s.autoUpdater = true;
    s.enableSound = true;
    return s;
}

class TestMainWindowPrefs : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_cfgDir;

private slots:
    void initTestCase()
    {
        // 隔離設定目錄：測試模式 + 明確覆寫，完全不碰使用者真實的設定
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_cfgDir.isValid());
        AppPaths::setConfigDirOverride(m_cfgDir.path());
        // QFileDialog 必須是 Qt widget 而非 macOS 原生 panel，否則 modal 無法由測試接手關閉
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    void cleanupTestCase() { AppPaths::setConfigDirOverride(QString()); }

    // 每個測試從乾淨的預設設定開始，避免前一個測試寫下的偏好影響後續視窗建構
    void init() { QFile::remove(AppPaths::filePath(QStringLiteral("settings.json"))); }
    void cleanup() { QFile::remove(AppPaths::filePath(QStringLiteral("settings.json"))); }

    // ===================================================================
    // PreferencesDialog：分頁組成與 result() 欄位對映
    // ===================================================================

    // 每一個 Notepad++ 對等分頁都必須存在。少一頁＝一整組偏好在 UI 上無從設定。
    void dialogExposesEveryPage()
    {
        Settings s;
        macpad::ui::PreferencesDialog dlg(s);
        auto *tabs = dlg.findChild<QTabWidget *>();
        QVERIFY(tabs);

        const QStringList expected = {
            QStringLiteral("General"), QStringLiteral("Editing"),
            QStringLiteral("New Document"), QStringLiteral("Print"),
            QStringLiteral("Backup"), QStringLiteral("Auto-Completion"),
            QStringLiteral("Performance"), QStringLiteral("Search"),
            QStringLiteral("Highlighting"), QStringLiteral("Dark Mode"),
            QStringLiteral("Toolbar"), QStringLiteral("Tab Bar"),
            QStringLiteral("Margins/Border/Edge"), QStringLiteral("Default Directory"),
            QStringLiteral("Recent Files History"), QStringLiteral("Language"),
            QStringLiteral("File Association"), QStringLiteral("Multi-Instance & Date"),
            QStringLiteral("Delimiter"), QStringLiteral("MISC")};
        QCOMPARE(tabs->count(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(tabs->tabText(i), expected.at(i));
    }

    // 完整 round-trip：對話框以一組「每個欄位都非預設」的設定初始化，未做任何操作直接取
    // result()，每個欄位都必須原封不動地回來。這同時證明了兩件事：
    //   (1) 各 build*Page() 有把設定值填進 widget；
    //   (2) result() 的 widget→欄位對映沒有接錯線（接錯就會冒出預設值或別的欄位的值）。
    void dialogRoundTripsEveryField()
    {
        const Settings in = makeDistinctiveSettings();
        macpad::ui::PreferencesDialog dlg(in);
        const Settings out = dlg.result();

        // 對話框未暴露的欄位必須以 m_original 為底保留下來，不可被預設值洗掉
        QCOMPARE(out.language, in.language);
        QCOMPARE(out.customColors, in.customColors);
        QCOMPARE(out.schemaVersion, in.schemaVersion);

        QCOMPARE(out.theme, in.theme);
        QCOMPARE(out.tabWidth, in.tabWidth);
        QCOMPARE(out.restoreOnLaunch, in.restoreOnLaunch);
        QCOMPARE(out.autosaveEnabled, in.autosaveEnabled);
        QCOMPARE(out.autosaveIntervalSec, in.autosaveIntervalSec);
        QCOMPARE(out.singleInstance, in.singleInstance);

        QCOMPARE(out.showLineNumbers, in.showLineNumbers);
        QCOMPARE(out.showIndentGuides, in.showIndentGuides);
        QCOMPARE(out.wordWrap, in.wordWrap);
        QCOMPARE(out.showWhitespace, in.showWhitespace);
        QCOMPARE(out.caretWidth, in.caretWidth);
        QCOMPARE(out.currentLineHighlight, in.currentLineHighlight);
        QCOMPARE(out.enableVirtualSpace, in.enableVirtualSpace);
        QCOMPARE(out.copyLineWithoutSelection, in.copyLineWithoutSelection);
        QCOMPARE(out.columnSelectionToMultiEdit, in.columnSelectionToMultiEdit);
        QCOMPARE(out.undoSelectionHistory, in.undoSelectionHistory);
        QCOMPARE(out.selectionDragDrop, in.selectionDragDrop);
        QCOMPARE(out.syncZoomBetweenViews, in.syncZoomBetweenViews);
        QCOMPARE(out.openCopyAfterSaveACopy, in.openCopyAfterSaveACopy);
        QCOMPARE(out.advancedAutoIndent, in.advancedAutoIndent);

        // 隱藏按鈕清單以「顯示的按鈕」勾選框呈現，回來的順序依清單順序，故比對集合
        QCOMPARE(QSet<QString>(out.hiddenToolbarButtons.begin(), out.hiddenToolbarButtons.end()),
                 QSet<QString>(in.hiddenToolbarButtons.begin(), in.hiddenToolbarButtons.end()));

        QCOMPARE(out.printHeader, in.printHeader);
        QCOMPARE(out.printFooter, in.printFooter);
        QCOMPARE(out.printColourMode, in.printColourMode);
        QCOMPARE(out.printMarginMm, in.printMarginMm);
        QCOMPARE(out.printFormFeedAsPageBreak, in.printFormFeedAsPageBreak);
        QCOMPARE(out.incrementalSearchCount, in.incrementalSearchCount);

        QCOMPARE(out.defaultEol, in.defaultEol);
        QCOMPARE(out.defaultEncoding, in.defaultEncoding);
        QCOMPARE(out.autoDetectFileStatus, in.autoDetectFileStatus);
        QCOMPARE(out.sessionFileExt, in.sessionFileExt);

        QCOMPARE(out.backupMode, in.backupMode);
        QCOMPARE(out.backupDir, in.backupDir);
        QCOMPARE(out.autosaveOnFocusLoss, in.autosaveOnFocusLoss);
        QCOMPARE(out.enableSessionSnapshot, in.enableSessionSnapshot);
        QCOMPARE(out.snapshotIntervalSec, in.snapshotIntervalSec);

        QCOMPARE(out.autoInsertPairs, in.autoInsertPairs);
        QCOMPARE(out.wordAutoComplete, in.wordAutoComplete);
        QCOMPARE(out.acThreshold, in.acThreshold);
        QCOMPARE(out.showCallTips, in.showCallTips);

        QCOMPARE(out.largeFileMB, in.largeFileMB);
        QCOMPARE(out.disableAutoCompleteOverMB, in.disableAutoCompleteOverMB);

        QCOMPARE(out.searchEngineUrl, in.searchEngineUrl);
        QCOMPARE(out.keepFindDialogOpen, in.keepFindDialogOpen);
        QCOMPARE(out.confirmReplaceAll, in.confirmReplaceAll);
        QCOMPARE(out.findInSelectionThreshold, in.findInSelectionThreshold);

        QCOMPARE(out.smartHighlight, in.smartHighlight);
        QCOMPARE(out.highlightMatchingTags, in.highlightMatchingTags);
        QCOMPARE(out.edgeColumn, in.edgeColumn);
        QCOMPARE(out.multiEdgeEnabled, in.multiEdgeEnabled);
        QCOMPARE(out.showWrapSymbol, in.showWrapSymbol);
        QCOMPARE(out.showEol, in.showEol);

        QCOMPARE(out.showToolbar, in.showToolbar);
        QCOMPARE(out.showStatusBar, in.showStatusBar);
        QCOMPARE(out.showTabBar, in.showTabBar);
        QCOMPARE(out.caretBlinkRate, in.caretBlinkRate);
        QCOMPARE(out.toolbarIconSize, in.toolbarIconSize);

        QCOMPARE(out.tabBarMultiLine, in.tabBarMultiLine);
        QCOMPARE(out.tabBarVertical, in.tabBarVertical);
        QCOMPARE(out.tabBarShowCloseButton, in.tabBarShowCloseButton);
        QCOMPARE(out.tabBarDoubleClickCloses, in.tabBarDoubleClickCloses);
        QCOMPARE(out.tabBarLabelMaxLength, in.tabBarLabelMaxLength);
        QCOMPARE(out.tabBarUntitledNameFromFirstLine, in.tabBarUntitledNameFromFirstLine);

        QCOMPARE(out.edgeMode, in.edgeMode);
        QCOMPARE(out.foldMarginStyle, in.foldMarginStyle);
        QCOMPARE(out.lineNumberMargin, in.lineNumberMargin);

        QCOMPARE(out.defaultDirPolicy, in.defaultDirPolicy);
        QCOMPARE(out.defaultDirFixedPath, in.defaultDirFixedPath);

        QCOMPARE(out.recentFilesMaxEntries, in.recentFilesMaxEntries);
        QCOMPARE(out.recentFilesShowFullPath, in.recentFilesShowFullPath);
        QCOMPARE(out.recentFilesInSubmenu, in.recentFilesInSubmenu);

        QCOMPARE(out.disabledLanguages, in.disabledLanguages);
        QCOMPARE(out.perLangTabWidth, in.perLangTabWidth);

        QCOMPARE(out.multiInstanceMode, in.multiInstanceMode);
        QCOMPARE(out.dateFormat, in.dateFormat);
        QCOMPARE(out.customDateFormat, in.customDateFormat);

        QCOMPARE(out.delimiterChars, in.delimiterChars);
        QCOMPARE(out.ctrlDoubleClickWholeWord, in.ctrlDoubleClickWholeWord);

        QCOMPARE(out.docSwitcherEnabled, in.docSwitcherEnabled);
        QCOMPARE(out.docPeekerEnabled, in.docPeekerEnabled);
        QCOMPARE(out.fileStatusAutoDetect, in.fileStatusAutoDetect);
        QCOMPARE(out.autoUpdater, in.autoUpdater);
        QCOMPARE(out.enableSound, in.enableSound);
    }

    // 損毀/手改的設定檔不得讓 UI 顯示出範圍外的值：色彩模式與邊界需夾限。
    void printPageClampsOutOfRangeValues()
    {
        {
            Settings s;
            s.printColourMode = 99;   // 超出 SC_PRINT_* 的 0..3
            s.printMarginMm = 999;    // 超出 0..50
            macpad::ui::PreferencesDialog dlg(s);
            QCOMPARE(dlg.result().printColourMode, 3);
            QCOMPARE(dlg.result().printMarginMm, 50);
        }
        {
            Settings s;
            s.printColourMode = -5;
            s.printMarginMm = -1;
            macpad::ui::PreferencesDialog dlg(s);
            QCOMPARE(dlg.result().printColourMode, 0);
            QCOMPARE(dlg.result().printMarginMm, 0);
        }
    }

    // General 頁與 Dark Mode 頁是同一份 ThemeMode 的兩個入口，任一邊改動另一邊都必須跟上；
    // 否則 result() 取 Dark Mode 那顆的值時，使用者在 General 頁選的主題會被默默丟掉。
    void themeCombosStaySynchronised()
    {
        Settings s;
        s.theme = ThemeMode::System;
        macpad::ui::PreferencesDialog dlg(s);

        auto *general = prefWidget<QComboBox *>(&dlg, QStringLiteral("General"), 0);
        auto *darkMode = prefWidget<QComboBox *>(&dlg, QStringLiteral("Dark Mode"), 0);
        QVERIFY(general && darkMode);
        QCOMPARE(general->currentIndex(), 0);
        QCOMPARE(darkMode->currentIndex(), 0);

        general->setCurrentIndex(2);                       // General → Dark
        QCOMPARE(darkMode->currentIndex(), 2);
        QCOMPARE(dlg.result().theme, ThemeMode::Dark);

        darkMode->setCurrentIndex(1);                      // Dark Mode → Light（反向）
        QCOMPARE(general->currentIndex(), 1);
        QCOMPARE(dlg.result().theme, ThemeMode::Light);
    }

    // 工具列按鈕清單的語意是「勾選＝顯示」，result() 收集的卻是「隱藏」清單——
    // 這個反向對映一旦寫反，使用者會發現自己勾了什麼就不見什麼。
    void toolbarButtonListInvertsCheckStateIntoHiddenIds()
    {
        Settings s;
        s.hiddenToolbarButtons = {QStringLiteral("OPEN")};   // 大小寫不敏感比對
        macpad::ui::PreferencesDialog dlg(s);

        auto *list = prefWidget<QListWidget *>(&dlg, QStringLiteral("Toolbar"), 0);
        QVERIFY(list);
        QVERIFY(list->count() > 30);   // 對齊上游 toolBarIcons[] 的按鈕數

        QListWidgetItem *open = nullptr;
        QListWidgetItem *save = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            const QString id = list->item(i)->data(Qt::UserRole).toString();
            if (id == QLatin1String("open"))
                open = list->item(i);
            if (id == QLatin1String("save"))
                save = list->item(i);
        }
        QVERIFY(open && save);
        QCOMPARE(open->checkState(), Qt::Unchecked);   // 已隱藏 → 未勾選
        QCOMPARE(save->checkState(), Qt::Checked);

        // 顯示 open、隱藏 save
        open->setCheckState(Qt::Checked);
        save->setCheckState(Qt::Unchecked);
        const QStringList hidden = dlg.result().hiddenToolbarButtons;
        QVERIFY(!hidden.contains(QStringLiteral("open")));
        QCOMPARE(hidden, QStringList{QStringLiteral("save")});
    }

    // Language 頁是自由文字輸入，使用者一定會打出各種畸形內容；
    // 解析必須挑掉壞的而不是產生垃圾鍵值（垃圾鍵值會讓 applyPerLangTabWidth 永遠對不上）。
    void languagePageParsesAndRejectsMalformedInput()
    {
        Settings s;
        macpad::ui::PreferencesDialog dlg(s);

        auto *disabled = prefWidget<QLineEdit *>(&dlg, QStringLiteral("Language"), 0);
        auto *perLang = prefWidget<QLineEdit *>(&dlg, QStringLiteral("Language"), 1);
        QVERIFY(disabled && perLang);

        disabled->setText(QStringLiteral("  Python ,JSON ,"));
        perLang->setText(QStringLiteral("python=2, C++ = 4, missing_eq, =9, zero=0, bad=abc, neg=-3"));

        const Settings out = dlg.result();
        QCOMPARE(out.disabledLanguages,
                 (QStringList{QStringLiteral("Python"), QStringLiteral("JSON")}));

        QMap<QString, int> expected;
        expected.insert(QStringLiteral("python"), 2);
        expected.insert(QStringLiteral("C++"), 4);
        QCOMPARE(out.perLangTabWidth, expected);   // 無 '='、空語言名、非數字、<=0 一律剔除
    }

    // File Association 寫的是作業系統層設定，平台不支援時必須明說原因，
    // 而不是給一個按了沒反應的清單（靜默無效比缺功能更糟）。
    void fileAssociationPageMatchesPlatformSupport()
    {
        using macpad::platform::FileAssociation;
        Settings s;
        macpad::ui::PreferencesDialog dlg(s);
        QWidget *page = prefPage(&dlg, QStringLiteral("File Association"));
        QVERIFY(page);

        auto *list = page->findChild<QListWidget *>();
        if (!FileAssociation::isSupported()) {
            QVERIFY2(!list, "不支援的平台仍列出了副檔名清單（按了不會有作用）");
            auto *label = page->findChild<QLabel *>();
            QVERIFY(label);
            QCOMPARE(label->text(), FileAssociation::unsupportedReason());
            return;
        }
        // 支援的平台（Windows）：清單須涵蓋常見副檔名，且勾選狀態反映實際關聯
        QVERIFY(list);
        QCOMPARE(list->count(), FileAssociation::commonExtensions().size());
        for (int i = 0; i < list->count(); ++i) {
            const QString ext = list->item(i)->data(Qt::UserRole).toString();
            QVERIFY(!ext.isEmpty());
            QCOMPARE(list->item(i)->checkState(),
                     FileAssociation::isAssociated(ext) ? Qt::Checked : Qt::Unchecked);
        }
    }

    // ===================================================================
    // MainWindow：Preferences… → 儲存 → 立即套用
    // ===================================================================

    // 整條主路徑：從選單開啟偏好設定 → 改一堆欄位 → 按 OK。
    // 斷言分三層：settings.json 真的寫入、編輯器真的改變、視窗外觀真的改變。
    void preferencesDialogSavesAndAppliesEverything()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            // --- General ---
            auto *theme = prefWidget<QComboBox *>(dlg, QStringLiteral("General"), 0);
            auto *tabWidth = prefWidget<QSpinBox *>(dlg, QStringLiteral("General"), 0);
            QVERIFY(theme && tabWidth);
            theme->setCurrentIndex(int(ThemeMode::Light));
            tabWidth->setValue(7);

            // --- Editing ---
            const QString editing = QStringLiteral("Editing");
            prefWidget<QCheckBox *>(dlg, editing, 0)->setChecked(true);   // 顯示行號
            prefWidget<QSpinBox *>(dlg, editing, 0)->setValue(3);         // 插入點寬度
            prefWidget<QCheckBox *>(dlg, editing, 4)->setChecked(false);  // 目前行高亮
            prefWidget<QCheckBox *>(dlg, editing, 5)->setChecked(true);   // 虛擬空白
            prefWidget<QCheckBox *>(dlg, editing, 7)->setChecked(true);   // 欄選轉多游標
            prefWidget<QCheckBox *>(dlg, editing, 8)->setChecked(true);   // Undo 納入選取歷史
            prefWidget<QCheckBox *>(dlg, editing, 9)->setChecked(false);  // 停用拖放選取
            prefWidget<QCheckBox *>(dlg, editing, 12)->setChecked(false); // 進階自動縮排

            // --- Auto-Completion ---
            const QString ac = QStringLiteral("Auto-Completion");
            prefWidget<QCheckBox *>(dlg, ac, 0)->setChecked(false);   // 自動配對
            prefWidget<QCheckBox *>(dlg, ac, 1)->setChecked(false);   // 文字自動完成
            prefWidget<QSpinBox *>(dlg, ac, 0)->setValue(5);          // 觸發字元數
            prefWidget<QCheckBox *>(dlg, ac, 2)->setChecked(false);   // 函式提示

            // --- Highlighting ---
            const QString hl = QStringLiteral("Highlighting");
            prefWidget<QCheckBox *>(dlg, hl, 1)->setChecked(true);    // 標示相符標籤
            prefWidget<QSpinBox *>(dlg, hl, 0)->setValue(77);         // 邊界線欄位
            prefWidget<QCheckBox *>(dlg, hl, 2)->setChecked(true);    // 多重邊界線

            // --- Margins/Border/Edge ---
            const QString mg = QStringLiteral("Margins/Border/Edge");
            prefWidget<QComboBox *>(dlg, mg, 0)->setCurrentIndex(int(EdgeMode::Line));
            prefWidget<QComboBox *>(dlg, mg, 1)->setCurrentIndex(int(FoldMarginStyle::Circle));
            prefWidget<QCheckBox *>(dlg, mg, 0)->setChecked(true);    // 行號邊界

            // --- Dark Mode（外觀）---
            const QString dm = QStringLiteral("Dark Mode");
            prefWidget<QCheckBox *>(dlg, dm, 0)->setChecked(false);   // 隱藏工具列
            prefWidget<QCheckBox *>(dlg, dm, 1)->setChecked(false);   // 隱藏狀態列
            prefWidget<QCheckBox *>(dlg, dm, 2)->setChecked(false);   // 隱藏分頁列
            prefWidget<QSpinBox *>(dlg, dm, 0)->setValue(0);          // 插入點不閃爍

            // --- Toolbar / Tab Bar ---
            prefWidget<QComboBox *>(dlg, QStringLiteral("Toolbar"), 0)
                ->setCurrentIndex(int(ToolbarIconSize::Large));
            const QString tb = QStringLiteral("Tab Bar");
            prefWidget<QCheckBox *>(dlg, tb, 1)->setChecked(true);    // 垂直排列
            prefWidget<QCheckBox *>(dlg, tb, 2)->setChecked(false);   // 不顯示關閉鈕

            // --- Delimiter ---
            const QString dl = QStringLiteral("Delimiter");
            prefWidget<QLineEdit *>(dlg, dl, 0)->setText(QStringLiteral("."));
            prefWidget<QCheckBox *>(dlg, dl, 0)->setChecked(false);

            dlg->accept();
        }, &fired);

        QAction *prefs = findAction(w, QStringLiteral("Preferences…"));
        QVERIFY2(prefs, "找不到 Preferences… 動作");
        prefs->trigger();
        QVERIFY2(fired, "Preferences… 沒有開出對話框");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("偏好設定已儲存"));

        // (1) 真的寫進 settings.json
        const Settings saved = SettingsStore::load();
        QCOMPARE(saved.theme, ThemeMode::Light);
        QCOMPARE(saved.tabWidth, 7);
        QCOMPARE(saved.caretWidth, 3);
        QCOMPARE(saved.acThreshold, 5);
        QCOMPARE(saved.edgeColumn, 77);
        QCOMPARE(saved.edgeMode, EdgeMode::Line);
        QCOMPARE(saved.foldMarginStyle, FoldMarginStyle::Circle);
        QCOMPARE(saved.caretBlinkRate, 0);
        QCOMPARE(saved.toolbarIconSize, ToolbarIconSize::Large);
        QVERIFY(!saved.showToolbar);
        QVERIFY(!saved.showStatusBar);
        QVERIFY(!saved.showTabBar);
        QVERIFY(saved.tabBarVertical);
        QVERIFY(!saved.tabBarShowCloseButton);
        QCOMPARE(saved.delimiterChars, QStringLiteral("."));

        // (2) 真的套用到編輯器（applyEditorPrefs）
        QCOMPARE(e->tabWidth(), 7);
        QVERIFY(e->showLineNumbers());
        QCOMPARE(e->caretWidth(), 3);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETCARETPERIOD)), 0);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGEMODE)),
                 int(QsciScintillaBase::EDGE_LINE));
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGECOLUMN)), 77);
        QCOMPARE(e->folding(), QsciScintilla::CircledTreeFoldStyle);
        QVERIFY(e->highlightMatchingTags());
        QVERIFY(!e->ctrlDoubleClickWholeWord());
        QVERIFY(!e->autoClose());
        QVERIFY(!e->wordCompletionEnabled());
        QVERIFY(!e->callTipsEnabled());
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETCARETLINEVISIBLE)), 0);
        QVERIFY(e->virtualSpace());
        QVERIFY(e->columnSelectionToMultiEdit());
        QVERIFY(e->undoSelectionHistory());
        QVERIFY(!e->selectionDragDropEnabled());
        QVERIFY(!e->advancedAutoIndent());
        // delimiterChars="." → 雙擊選字在 '.' 處斷開（applyDelimiters）
        e->setText(QStringLiteral("foo.bar"));
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION,
                                      static_cast<unsigned long>(0), 1L)), 3);

        // (3) 真的套用到視窗外觀（applyWindowPrefs）
        QToolBar *tb = mainToolbar(w);
        QVERIFY(tb);
        QVERIFY2(!tb->isVisibleTo(&w), "showToolbar=false 但工具列仍會顯示");
        QCOMPARE(tb->iconSize(), QSize(32, 32));
        QVERIFY(!w.statusBar()->isVisibleTo(&w));
        const auto tabs = viewTabs(w);
        QCOMPARE(tabs.size(), 2);
        for (QTabWidget *t : tabs) {
            QVERIFY(!t->tabBar()->isVisibleTo(t));
            QCOMPARE(t->tabPosition(), QTabWidget::West);
            QVERIFY(!t->tabsClosable());
        }
    }

    // 取消對話框：一個位元都不能落地（IL-2 的精神——沒按 OK 就沒有變更）
    void preferencesDialogCancelChangesNothing()
    {
        Settings before;
        before.tabWidth = 6;
        before.caretWidth = 2;
        QVERIFY(SettingsStore::save(before));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->tabWidth(), 6);

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *tabWidth = prefWidget<QSpinBox *>(dlg, QStringLiteral("General"), 0);
            QVERIFY(tabWidth);
            tabWidth->setValue(16);
            dlg->reject();
        }, &fired);
        QAction *prefs = findAction(w, QStringLiteral("Preferences…"));
        QVERIFY(prefs);
        prefs->trigger();
        QVERIFY(fired);

        QCOMPARE(SettingsStore::load().tabWidth, 6);
        QCOMPARE(e->tabWidth(), 6);
    }

    // ===================================================================
    // applyEditorPrefs：啟動時載入的偏好必須套到（新開的）每一個分頁
    // ===================================================================

    void newTabInheritsSavedEditorPrefs()
    {
        Settings s;
        s.tabWidth = 5;
        s.showLineNumbers = true;
        s.lineNumberMargin = false;   // 兩者需皆為真才顯示行號（調和邏輯）
        s.caretWidth = 3;
        s.caretBlinkRate = 800;
        s.edgeMode = EdgeMode::Background;
        s.edgeColumn = 60;
        s.multiEdgeEnabled = true;
        s.foldMarginStyle = FoldMarginStyle::None;
        s.autoInsertPairs = false;
        s.wordAutoComplete = false;
        s.showCallTips = false;
        s.currentLineHighlight = false;
        s.enableVirtualSpace = true;
        s.highlightMatchingTags = true;
        s.ctrlDoubleClickWholeWord = false;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        // 新分頁（File ▸ New）同樣要吃到偏好，而不是只有啟動時那一個
        QAction *newAct = findAction(w, QStringLiteral("New"));
        QVERIFY(newAct);
        newAct->trigger();

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->tabWidth(), 5);
        QVERIFY2(!e->showLineNumbers(),
                 "lineNumberMargin=false 時仍顯示行號（Editing 與 Margins 兩頁未調和）");
        QCOMPARE(e->caretWidth(), 3);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETCARETPERIOD)), 800);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGEMODE)),
                 int(QsciScintillaBase::EDGE_BACKGROUND));
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGECOLUMN)), 60);
        QCOMPARE(e->folding(), QsciScintilla::NoFoldStyle);
        QVERIFY(!e->autoClose());
        QVERIFY(!e->wordCompletionEnabled());
        QVERIFY(!e->callTipsEnabled());
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETCARETLINEVISIBLE)), 0);
        QVERIFY(e->virtualSpace());
        QVERIFY(e->highlightMatchingTags());
        QVERIFY(!e->ctrlDoubleClickWholeWord());
    }

    // caretBlinkRate 為負（手改設定檔）時不得傳負值給 Scintilla
    void negativeCaretBlinkRateIsClampedToZero()
    {
        Settings s;
        s.caretBlinkRate = -100;
        QVERIFY(SettingsStore::save(s));
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETCARETPERIOD)), 0);
    }

    // edgeColumn=0（關閉）時不得改動 Scintilla 的邊界欄位——0 欄在畫面上會壓在最左邊
    void zeroEdgeColumnLeavesScintillaDefault()
    {
        Settings s;
        s.edgeMode = EdgeMode::None;
        s.edgeColumn = 0;
        QVERIFY(SettingsStore::save(s));
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGEMODE)),
                 int(QsciScintillaBase::EDGE_NONE));
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETEDGECOLUMN)), 0);
    }

    // ===================================================================
    // applyPerLangTabWidth：依語言覆寫 Tab 寬度
    // ===================================================================

    // 走使用者真正會走的路徑：先開好檔案（此時 lexer 已就位），再於 Preferences ▸ Language
    // 填入覆寫並按 OK。同一次套用中，Python 分頁縮到 2、純文字分頁維持全域 8。
    void perLangTabWidthOverridesGlobalOnlyForListedLanguage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString py = writeFile(dir.filePath(QStringLiteral("a.py")), "x = 1\n");
        const QString txt = writeFile(dir.filePath(QStringLiteral("b.txt")), "plain\n");
        QVERIFY(!py.isEmpty() && !txt.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(py);
        EditorWidget *pyEditor = w.activeEditor();
        QVERIFY(pyEditor);
        QVERIFY2(pyEditor->lexer(), ".py 未套用 lexer，無從判定語言");
        w.openFile(txt);
        EditorWidget *txtEditor = w.activeEditor();
        QVERIFY(txtEditor && txtEditor != pyEditor);

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *tabWidth = prefWidget<QSpinBox *>(dlg, QStringLiteral("General"), 0);
            auto *perLang = prefWidget<QLineEdit *>(dlg, QStringLiteral("Language"), 1);
            QVERIFY(tabWidth && perLang);
            tabWidth->setValue(8);                                   // 全域
            perLang->setText(QStringLiteral("python=2"));            // 僅 Python 覆寫
            dlg->accept();
        }, &fired);
        QAction *prefs = findAction(w, QStringLiteral("Preferences…"));
        QVERIFY(prefs);
        prefs->trigger();
        QVERIFY(fired);

        QCOMPARE(SettingsStore::load().perLangTabWidth.value(QStringLiteral("python")), 2);
        QCOMPARE(pyEditor->tabWidth(), 2);
        QCOMPARE(txtEditor->tabWidth(), 8);   // 未列出的語言沿用全域值
    }

    // 沒有任何覆寫時必須早退，全部沿用全域 tabWidth
    void emptyPerLangMapLeavesGlobalTabWidth()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString py = writeFile(dir.filePath(QStringLiteral("c.py")), "y = 2\n");
        QVERIFY(!py.isEmpty());

        Settings s;
        s.tabWidth = 3;
        QVERIFY(s.perLangTabWidth.isEmpty());
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(py);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->tabWidth(), 3);
    }

    // ===================================================================
    // applyDelimiters：分隔字元 → 雙擊選字邊界
    // ===================================================================

    // 斷言用 Scintilla 的字詞邊界查詢，而不是「有沒有呼叫 SCI_SETWORDCHARS」——
    // 前者才是使用者雙擊時真正感受到的行為。
    void delimiterCharsChangeWordBoundaries()
    {
        Settings s;
        s.delimiterChars = QStringLiteral(".");   // 只有 '.' 是邊界
        QVERIFY(SettingsStore::save(s));
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e);
            e->setText(QStringLiteral("foo.bar-baz"));
            // 從位置 0 往後找字尾：'.' 斷開 → 3；'-' 不是邊界故不斷開
            QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION,
                                          static_cast<unsigned long>(0), 1L)), 3);
            QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION,
                                          static_cast<unsigned long>(4), 1L)), 11);
        }

        s.delimiterChars = QStringLiteral(".-");   // 加入 '-' 為邊界
        QVERIFY(SettingsStore::save(s));
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e);
            e->setText(QStringLiteral("foo.bar-baz"));
            QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION,
                                          static_cast<unsigned long>(4), 1L)), 7);
        }
    }

    // ===================================================================
    // applyViewPrefs：View 選單的檢視開關
    // ===================================================================

    void viewMenuTogglesApplyToAllEditors()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        QAction *wrap = findAction(w, QStringLiteral("Word Wrap"));
        QAction *ws = findAction(w, QStringLiteral("Show Whitespace"));
        QAction *eol = findAction(w, QStringLiteral("Show End of Line"));
        QAction *ig = findAction(w, QStringLiteral("Show Indent Guide"));
        QAction *wrapSym = findAction(w, QStringLiteral("Show Wrap Symbol"));
        QVERIFY(wrap && ws && eol && ig && wrapSym);

        wrap->setChecked(true);
        QCOMPARE(e->wrapMode(), QsciScintilla::WrapWord);
        ws->setChecked(true);
        QCOMPARE(e->whitespaceVisibility(), QsciScintilla::WsVisible);
        eol->setChecked(true);
        QVERIFY(e->eolVisibility());
        wrapSym->setChecked(true);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETWRAPVISUALFLAGS)),
                 int(QsciScintilla::WrapFlagByText));
        ig->setChecked(false);
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETINDENTATIONGUIDES)), 0);

        // 全部關回去：另一半分支同樣要走到
        wrap->setChecked(false);
        ws->setChecked(false);
        eol->setChecked(false);
        wrapSym->setChecked(false);
        ig->setChecked(true);
        QCOMPARE(e->wrapMode(), QsciScintilla::WrapNone);
        QCOMPARE(e->whitespaceVisibility(), QsciScintilla::WsInvisible);
        QVERIFY(!e->eolVisibility());
        QCOMPARE(int(e->SendScintilla(QsciScintillaBase::SCI_GETWRAPVISUALFLAGS)),
                 int(QsciScintilla::WrapFlagNone));
        QVERIFY(int(e->SendScintilla(QsciScintillaBase::SCI_GETINDENTATIONGUIDES)) != 0);
    }

    // ===================================================================
    // applyWindowPrefs：工具列 / 狀態列 / 分頁列
    // ===================================================================

    void windowPrefsApplyOnConstruction_data()
    {
        QTest::addColumn<int>("iconSize");
        QTest::addColumn<int>("expectedPx");
        QTest::newRow("small") << int(ToolbarIconSize::Small) << 16;
        QTest::newRow("standard") << int(ToolbarIconSize::Standard) << 24;
        QTest::newRow("large") << int(ToolbarIconSize::Large) << 32;
    }

    void windowPrefsApplyOnConstruction()
    {
        QFETCH(int, iconSize);
        QFETCH(int, expectedPx);

        Settings s;
        s.toolbarIconSize = ToolbarIconSize(iconSize);
        s.showToolbar = true;
        s.showStatusBar = true;
        s.showTabBar = true;
        s.tabBarVertical = false;
        s.tabBarShowCloseButton = true;
        s.tabBarMultiLine = true;   // 水平時多列生效
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QToolBar *tb = mainToolbar(w);
        QVERIFY(tb);
        QCOMPARE(tb->iconSize(), QSize(expectedPx, expectedPx));
        QVERIFY(tb->isVisibleTo(&w));
        QVERIFY(w.statusBar()->isVisibleTo(&w));

        const auto tabs = viewTabs(w);
        QCOMPARE(tabs.size(), 2);
        for (QTabWidget *t : tabs) {
            QVERIFY(t->tabBar()->isVisibleTo(t));
            QCOMPARE(t->tabPosition(), QTabWidget::North);
            QVERIFY(t->tabsClosable());
            auto *bar = qobject_cast<macpad::ui::MultiRowTabBar *>(t->tabBar());
            QVERIFY2(bar, "分頁列不是 MultiRowTabBar，多列模式將無從啟用");
            QVERIFY(bar->isMultiRow());
        }
    }

    // 垂直排列時分頁本來就縱向堆疊，多列無意義，必須被關掉（否則版面會亂）
    void multiLineTabBarIsDisabledWhenVertical()
    {
        Settings s;
        s.tabBarMultiLine = true;
        s.tabBarVertical = true;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const auto tabs = viewTabs(w);
        QCOMPARE(tabs.size(), 2);
        for (QTabWidget *t : tabs) {
            QCOMPARE(t->tabPosition(), QTabWidget::West);
            auto *bar = qobject_cast<macpad::ui::MultiRowTabBar *>(t->tabBar());
            QVERIFY(bar);
            QVERIFY2(!bar->isMultiRow(), "垂直排列時仍啟用了多列模式");
        }
    }

    // ===================================================================
    // startDirForDialog：開檔對話框的起始目錄策略
    // ===================================================================

    // 三種策略各走一條分支。以真正開出 QFileDialog 並讀其 directory() 為斷言，
    // 而不是呼叫私有函式——使用者感受到的就是「對話框開在哪個資料夾」。
    void startDirForDialogFollowsPolicy()
    {
        QTemporaryDir fixedDir;
        QTemporaryDir docDir;
        QVERIFY(fixedDir.isValid() && docDir.isValid());
        const QString doc = writeFile(docDir.filePath(QStringLiteral("doc.txt")), "hi\n");
        QVERIFY(!doc.isEmpty());

        // --- (1) FixedPath：固定目錄 ---
        {
            Settings s;
            s.defaultDirPolicy = DefaultDirPolicy::FixedPath;
            s.defaultDirFixedPath = fixedDir.path();
            QVERIFY(SettingsStore::save(s));

            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            QString seen;
            bool fired = false;
            driveNextModal([&seen](QDialog *dlg) {
                if (auto *fd = qobject_cast<QFileDialog *>(dlg))
                    seen = fd->directory().absolutePath();
                dlg->reject();
            }, &fired);
            QMetaObject::invokeMethod(&w, "openFileDialog");
            QVERIFY2(fired, "openFileDialog 沒有開出對話框（原生對話框未被停用？）");
            QCOMPARE(seen, QFileInfo(fixedDir.path()).absoluteFilePath());
        }

        // --- (2) FixedPath 但路徑為空 → 退回目前文件所在資料夾 ---
        {
            Settings s;
            s.defaultDirPolicy = DefaultDirPolicy::FixedPath;
            s.defaultDirFixedPath.clear();
            QVERIFY(SettingsStore::save(s));

            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.openFile(doc);
            QString seen;
            bool fired = false;
            driveNextModal([&seen](QDialog *dlg) {
                if (auto *fd = qobject_cast<QFileDialog *>(dlg))
                    seen = fd->directory().absolutePath();
                dlg->reject();
            }, &fired);
            QMetaObject::invokeMethod(&w, "openFileDialog");
            QVERIFY(fired);
            QCOMPARE(seen, QFileInfo(doc).absolutePath());
        }

        // --- (3) FollowCurrentDoc 且目前為未命名文件 → 無起始目錄（不臆測） ---
        {
            Settings s;
            s.defaultDirPolicy = DefaultDirPolicy::FollowCurrentDoc;
            QVERIFY(SettingsStore::save(s));

            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            QVERIFY(w.activeEditor());
            QVERIFY(w.activeEditor()->isUntitled());
            QString seen;
            bool fired = false;
            driveNextModal([&seen](QDialog *dlg) {
                if (auto *fd = qobject_cast<QFileDialog *>(dlg))
                    seen = fd->directory().absolutePath();
                dlg->reject();
            }, &fired);
            QMetaObject::invokeMethod(&w, "openFileDialog");
            QVERIFY(fired);
            // 未指定起始目錄時 Qt 會用目前工作目錄；重點是不得指向任何測試資料夾
            QVERIFY(!seen.isEmpty());
            QVERIFY(seen != QFileInfo(fixedDir.path()).absoluteFilePath());
        }

        // --- (4) RememberLast：先在對話框中真的選一個檔，再確認下次開在該目錄 ---
        {
            Settings s;
            s.defaultDirPolicy = DefaultDirPolicy::RememberLast;
            QVERIFY(SettingsStore::save(s));

            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            bool fired = false;
            driveNextModal([&doc](QDialog *dlg) {
                auto *fd = qobject_cast<QFileDialog *>(dlg);
                QVERIFY(fd);
                fd->setDirectory(QFileInfo(doc).absolutePath());
                // 直接寫入檔名輸入框而非用 selectFile()：後者在對話框已顯示且輸入框
                // 取得焦點時會略過設定文字，會讓這段測試時好時壞。
                auto *edit = fd->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
                QVERIFY2(edit, "找不到 QFileDialog 的檔名輸入框（非 Qt 內建對話框？）");
                edit->setText(doc);
                QMetaObject::invokeMethod(fd, "accept");
            }, &fired);
            QMetaObject::invokeMethod(&w, "openFileDialog");
            QVERIFY(fired);
            QVERIFY2(w.activeEditor() && !w.activeEditor()->isUntitled(),
                     "對話框選檔後沒有開起來，無法驗證 RememberLast");

            // 切到未命名分頁：此時 FollowCurrentDoc 會給空值，只有 m_lastDir 能給出答案
            QAction *newAct = findAction(w, QStringLiteral("New"));
            QVERIFY(newAct);
            newAct->trigger();
            QVERIFY(w.activeEditor()->isUntitled());

            QString seen;
            fired = false;
            driveNextModal([&seen](QDialog *dlg) {
                if (auto *fd = qobject_cast<QFileDialog *>(dlg))
                    seen = fd->directory().absolutePath();
                dlg->reject();
            }, &fired);
            QMetaObject::invokeMethod(&w, "openFileDialog");
            QVERIFY(fired);
            QCOMPARE(seen, QFileInfo(doc).absolutePath());
        }
    }

    // ===================================================================
    // 命令列視窗選項
    // ===================================================================

    void cliWindowOptionsSetTopMostAndTitleSuffix()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(!w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        // 建構後尚未設過標題 → 實作以 "macpad++" 為基底，不可拼出前導的 " - "
        QVERIFY(w.windowTitle().isEmpty());
        w.applyCliWindowOptions(/*alwaysOnTop=*/true, QStringLiteral("Session A"));
        QVERIFY(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(w.windowTitle(), QStringLiteral("macpad++ - Session A"));

        // 已有標題時附加於其後（另一條分支）
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        w2.setWindowTitle(QStringLiteral("doc.txt"));
        w2.applyCliWindowOptions(/*alwaysOnTop=*/false, QStringLiteral("Extra"));
        QVERIFY2(!w2.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                 "未指定 -alwaysOnTop 卻設了置頂旗標");
        QCOMPARE(w2.windowTitle(), QStringLiteral("doc.txt - Extra"));

        // 兩個選項都不給時視窗完全不受影響（早退分支）
        MainWindow w3(nullptr, /*restoreSessionOnLaunch=*/false);
        w3.setWindowTitle(QStringLiteral("keep"));
        w3.applyCliWindowOptions(/*alwaysOnTop=*/false, QString());
        QVERIFY(!w3.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(w3.windowTitle(), QStringLiteral("keep"));
    }

    // -notabbar：兩個檢視的分頁列都要一起隱藏（只藏一邊等於沒藏）
    void setTabBarVisibleAffectsBothViews()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const auto tabs = viewTabs(w);
        QCOMPARE(tabs.size(), 2);

        w.setTabBarVisible(false);
        for (QTabWidget *t : tabs)
            QVERIFY(!t->tabBar()->isVisibleTo(t));

        w.setTabBarVisible(true);
        for (QTabWidget *t : tabs)
            QVERIFY(t->tabBar()->isVisibleTo(t));
    }

    // -fullReadOnly：政策鎖定，且必須涵蓋所有分頁而不只是作用中那一個
    void fullReadOnlyLocksEveryOpenDocument()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString f1 = writeFile(dir.filePath(QStringLiteral("r1.txt")), "one\n");
        const QString f2 = writeFile(dir.filePath(QStringLiteral("r2.txt")), "two\n");
        QVERIFY(!f1.isEmpty() && !f2.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(f1);
        w.openFile(f2);

        const auto editors = w.findChildren<EditorWidget *>();
        QVERIFY2(editors.size() >= 2, "測試前置條件：至少要有兩個已開啟文件");

        w.setFullReadOnly(true);
        for (EditorWidget *e : editors) {
            QVERIFY(e->isPolicyReadOnly());
            QVERIFY(e->isReadOnly());
        }

        w.setFullReadOnly(false);
        for (EditorWidget *e : editors)
            QVERIFY(!e->isPolicyReadOnly());
    }

    // -monitor：已存檔的分頁進入 tail -f 唯讀；未命名分頁不得被納入（無檔可監控）
    void monitoringLocksSavedFilesOnly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString f = writeFile(dir.filePath(QStringLiteral("m.txt")), "watch me\n");
        QVERIFY(!f.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        // 空的未命名分頁會被開檔沿用，故先開檔、再另建一個未命名分頁
        w.openFile(f);
        EditorWidget *saved = w.activeEditor();
        QVERIFY(saved && !saved->isUntitled());
        QAction *newAct = findAction(w, QStringLiteral("New"));
        QVERIFY(newAct);
        newAct->trigger();
        EditorWidget *untitled = w.activeEditor();
        QVERIFY(untitled && untitled != saved && untitled->isUntitled());

        w.enableMonitoringForOpenFiles();
        QVERIFY2(saved->isPolicyReadOnly(), "監控中的檔案未被設為唯讀");
        QVERIFY2(!untitled->isPolicyReadOnly(), "未命名分頁不應被納入監控");

        // 重複呼叫必須冪等（已在 m_monitored 中的路徑不再重複處理）
        saved->setPolicyReadOnly(false);
        w.enableMonitoringForOpenFiles();
        QVERIFY(!saved->isPolicyReadOnly());
    }

    // -openFoldersAsWorkspace：加入工作區並顯示面板；空路徑須為 no-op
    void addWorkspaceFolderShowsDock()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
        QVERIFY(!writeFile(dir.filePath(QStringLiteral("sub/inside.txt")), "x\n").isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto *dock = w.findChild<macpad::ui::WorkspaceDock *>();
        QVERIFY(dock);
        QVERIFY(!dock->isVisibleTo(&w));

        w.addWorkspaceFolder(QString());   // 空路徑：不得顯示面板
        QVERIFY(!dock->isVisibleTo(&w));

        w.addWorkspaceFolder(dir.path());
        QVERIFY2(dock->isVisibleTo(&w), "加入工作區資料夾後面板沒有顯示出來");
    }

    // ===================================================================
    // -udl=<name>
    // ===================================================================

    void applyUdlByNameSetsLexerOnActiveEditor()
    {
        // 先在（已被導向暫存目錄的）設定目錄中種一份 UDL，MainWindow 建構時會載入
        macpad::features::UdlDefinition def;
        def.name = QStringLiteral("MyLang");
        def.extensions = {QStringLiteral("mylang")};
        def.keywords = {QStringLiteral("alpha"), QStringLiteral("beta")};
        def.lineComment = QStringLiteral("#");
        macpad::features::UdlManager mgr;
        QVERIFY(mgr.save(def));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // 名稱為空 / 查無此 UDL → 完全不動 lexer
        QsciLexer *before = e->lexer();
        w.applyUdlByName(QString());
        QCOMPARE(e->lexer(), before);
        w.applyUdlByName(QStringLiteral("NoSuchLanguage"));
        QCOMPARE(e->lexer(), before);

        w.applyUdlByName(QStringLiteral("MyLang"));
        auto *udl = qobject_cast<macpad::features::UdlLexer *>(e->lexer());
        QVERIFY2(udl, "-udl=MyLang 沒有把 UDL lexer 套到作用中編輯器");
        QCOMPARE(QString::fromLatin1(udl->language()), QStringLiteral("MyLang"));

        QFile::remove(AppPaths::filePath(QStringLiteral("udl/MyLang.json")));
    }

    // ===================================================================
    // -systemtray
    // ===================================================================

    // 系統匣在無頭/offscreen 環境不可用是常態，必須安全回報 false 而不是崩潰或半殘。
    // 可用時則驗證選單與冪等性。
    void systemTrayIsOptionalAndIdempotent()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const bool available = QSystemTrayIcon::isSystemTrayAvailable();
        const bool ok = w.enableSystemTray();
        QCOMPARE(ok, available);

        if (!available) {
            QVERIFY2(!w.findChild<QSystemTrayIcon *>(),
                     "系統不支援系統匣時不應建立圖示物件");
            return;
        }

        auto *tray = w.findChild<QSystemTrayIcon *>();
        QVERIFY(tray);
        QVERIFY(!tray->icon().isNull());   // 空圖示等同功能失效
        QVERIFY(tray->contextMenu());
        QCOMPARE(tray->contextMenu()->actions().size(), 4);   // Show / Hide / 分隔線 / Quit

        QVERIFY2(w.enableSystemTray(), "重複啟用應為冪等");
        QCOMPARE(w.findChildren<QSystemTrayIcon *>().size(), 1);

        const auto acts = tray->contextMenu()->actions();
        acts.at(1)->trigger();                    // Hide Window
        QVERIFY(w.isHidden());
        acts.at(0)->trigger();                    // Show Window
        QVERIFY(!w.isHidden());

        // 點擊圖示切換顯示/隱藏；非點擊原因（如中鍵）不得改變狀態
        emit tray->activated(QSystemTrayIcon::Trigger);
        QVERIFY(w.isHidden());
        emit tray->activated(QSystemTrayIcon::Trigger);
        QVERIFY(!w.isHidden());
        emit tray->activated(QSystemTrayIcon::MiddleClick);
        QVERIFY(!w.isHidden());
    }
};

QTEST_MAIN(TestMainWindowPrefs)
#include "test_mainwindow_prefs.moc"
