#pragma once

// MainWindow — 主視窗（app 層）
// v1 起始：分頁式編輯（FR-001 部分）、選單列（Mac 慣例 keymap，FR-024/025）、
// 檔案開啟/儲存/另存（FR-014）、狀態列行/列（FR-022 部分）。
// 完整分頁拖曳/標色/鎖定、分割視窗、session 等後續加入。

#include <QMainWindow>

#include "extension/IExtension.h"
#include "features/udl/UdlManager.h"

#include <memory>
#include <QStringList>
#include <QSet>
#include <QVector>
#include <QPair>

class QTabWidget;
class QSplitter;
class QLabel;
class QMenu;
class QToolBar;
class QFileSystemWatcher;
class QTimer;
class QsciMacro;
class QsciLexer;
class QAction;
class QLineEdit;
class QDockWidget;
class QShortcut;

namespace macpad::core { class EditorWidget; }
namespace macpad::features { class FindReplaceDialog; }
namespace macpad::ui { class EditorPane; class DocumentListDock;
    class FunctionListDock; class ClipboardHistoryDock; class DocumentMapDock; class WorkspaceDock;
    class CharacterPanel; class ProjectPanelDock; }
namespace macpad::extension { class ExtensionRegistry; }
namespace macpad::features { class FindInFilesDock; class RunDock; class FindAllDock; }
namespace macpad::persistence { struct SessionState; struct Settings; }

class MainWindow : public QMainWindow, public macpad::extension::IHostServices {
    Q_OBJECT
    // 單元測試存取私有成員（分頁容器、右鍵選單建構、關閉側邊分頁等）——Qt 慣用測試模式
    friend class TestContextMenu;
public:
    // restoreSessionOnLaunch=false 供命令列 -nosession（FR-051）略過本次啟動的 session 還原
    explicit MainWindow(QWidget *parent = nullptr, bool restoreSessionOnLaunch = true);
    ~MainWindow() override;

    // IHostServices（extension protocol，FR-035）
    macpad::core::EditorWidget *activeEditor() override;
    void addMenuAction(const QString &menuTitle, const QString &text,
                       std::function<void()> callback) override;
    void showStatusMessage(const QString &message, int timeoutMs = 3000) override;
    QWidget *hostWindow() override { return this; }

    // 命令列視窗選項（FR-051）：-alwaysOnTop 置頂、-title:/-titleAdd: 附加標題（供 main.cpp 於開檔後套用）
    void applyCliWindowOptions(bool alwaysOnTop, const QString &titleAdd);

    // 命令列啟動選項（供 main.cpp 於開檔後套用）
    void openSessionFile(const QString &path);          // -openSession <file>
    void addWorkspaceFolder(const QString &path);       // -openFoldersAsWorkspace <folder>
    void setTabBarVisible(bool visible);                // -notabbar（隱藏分頁列）
    void setFullReadOnly(bool on);                      // -fullReadOnly（全域唯讀）
    void enableMonitoringForOpenFiles();                // -monitor（監控所有已開啟檔案）
    void applyUdlByName(const QString &name);           // -udl=<name>（對作用中編輯器套用 UDL）

public slots:
    // 開啟指定檔案（已開啟則聚焦既有分頁——FR-001 邊界）
    void openFile(const QString &path);
    // 開啟並跳至指定行/欄（1-based）——供 Find in Files 跳轉
    void openFileAtLine(const QString &path, int line, int column);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newFile();
    void openFileDialog();
    bool saveCurrent();
    bool saveCurrentAs();
    void closeTab(int index);
    void createStatusCells();
    void updateStatusBar();
    void updateTabTitle();
    void showFind();
    void showReplace();
    void toggleSplit();
    // Dual-View（雙檢視）：把目前分頁搬到 / 複製到另一個檢視容器（複刻 Notepad++ 兩欄檢視）
    void moveToOtherView();
    void cloneToOtherView();
    void applyTheme();
    void themeEditor(macpad::core::EditorWidget *editor);  // 依設定為單一編輯器上主題色
    void applyViewPrefs(macpad::core::EditorWidget *editor);
    void applyEditorPrefs(macpad::core::EditorWidget *editor, const macpad::persistence::Settings &s);
    void applySnapshotTimerSettings(const macpad::persistence::Settings &s);  // 依偏好啟停/調整快照計時器
    void onFileChangedOnDisk(const QString &path);
    void startMacroRecording();
    void stopMacroRecording();
    void playMacro();

    // === Notepad++ 對等：檔案操作 ===
    void reloadFromDisk();
    void saveAll();
    void saveCopyAs();
    void renameCurrentFile();
    void closeAllTabs();
    void closeAllButCurrent();
    void moveCurrentToTrash();
    void restoreClosedTab();
    void revealInFinder();
    void openInDefaultApp();
    // === Notepad++ 對等：搜尋 ===
    void findNextDir(bool forward);
    void selectAndFind(bool forward);
    void markSelectionOccurrences();
    void clearAllMarks();
    // === Notepad++ 對等：檢視 / 分頁 / 視窗 ===
    void toggleAlwaysOnTop(bool on);
    void activateTabRelative(int delta);
    void moveCurrentTab(int delta);
    void toggleMonitoring();
    void buildWindowMenu();
    void setDistractionFree(bool on);
    void setPostIt(bool on);
    void showIncrementalSearch();
    void viewCurrentFileInBrowser(const QString &appName);
    // 編輯區右鍵選單（複刻 Notepad++ 編輯區右鍵）：由 EditorWidget::contextMenuRequested 觸發，
    // sender() 即被右鍵的編輯器；先將其設為作用中分頁再建構選單，使選單各項作用於正確文件。
    void showEditorContextMenu(const QPoint &globalPos);
    // 建構編輯區右鍵選單（複刻 Notepad++ contextMenu.xml），各項作用於 ed 本身；
    // 回傳的 QMenu 以 parent 為擁有者（傳 nullptr 時由呼叫端負責釋放）。抽出以利單元測試斷言。
    QMenu *buildEditorContextMenu(macpad::core::EditorWidget *ed, QWidget *parent);
    // 分頁右鍵的檔案操作輔助（Close to Left/Right 需指定檢視與基準索引）
    void closeTabsToOneSide(QTabWidget *w, int pivot, bool toLeft);

private:
    void createMenus();
    // createMenus() 的分解：各頂層選單的填充拆到獨立 helper（行為保持一致）
    void createFileMenu(QMenu *fileMenu);
    void createEditMenu(QMenu *editMenu);
    void createEncodingMenu(QMenu *formatMenu);
    void createLanguageMenu(QMenu *langMenu);
    void createSettingsMenu(QMenu *settingsMenu);
    void createToolsMenu(QMenu *toolsMenu);
    void createMacroMenu(QMenu *macroMenu);
    void createRunMenu(QMenu *runMenu);
    void createViewMenu(QMenu *viewMenu);
    void createPluginsMenu(QMenu *pluginMenu);
    // 依目前 lexer 反查 LexerFactory 語言鍵（FR-052）。跨多個 .cpp 使用，故為私有靜態成員。
    static QString languageKeyForLexer(QsciLexer *lex);
    // 依偏好套用「視窗層級」外觀（工具列/狀態列/分頁列可見性與圖示大小、分頁關閉鈕等）
    void applyWindowPrefs(const macpad::persistence::Settings &s);
    // 依 delimiterChars 設定編輯器的 word-char 集合（影響雙擊選字邊界）
    void applyDelimiters(macpad::core::EditorWidget *editor, const macpad::persistence::Settings &s);
    // 依 perLangTabWidth 對單一編輯器套用其語言的 Tab 寬度覆寫（否則用全域 tabWidth）
    void applyPerLangTabWidth(macpad::core::EditorWidget *editor, const macpad::persistence::Settings &s);
    // 依 macros.json + macro_shortcuts.json 重建每個具名巨集的全域快捷鍵
    void rebuildMacroShortcuts();
    // 依 run_commands.json 中各已儲存指令的 shortcut 欄位重建全域快捷鍵
    void rebuildRunShortcuts();
    void openMacroManager();                   // Macro ▸ Macro Manager…
    void runSavedCommand(const QString &command);  // 以目前文件變數上下文執行一條已儲存 Run 指令
    void showFindInProjects();                 // Search ▸ Find in Projects…
    void gotoLineOrOffset();                   // Go to…（行 / 字元位移雙模式）
    QString startDirForDialog() const;         // 依 defaultDirPolicy 決定開/存檔對話框起始目錄
    void buildToolbar();                       // Notepad++ 風格的圖示工具列
    void retintToolbar();                      // 依主題重新上色圖示
    void createSearchMenu(QMenu *searchMenu);  // Notepad++ Search 選單（填入預建的選單）
    void createEditMenuOps(QMenu *editMenu);   // Notepad++ 對等文字操作
    void applyTextOp(const std::function<QString(const QString &)> &op);
    void moveCurrentLines(bool up);
    void saveSession();
    void restoreSession();
    macpad::persistence::SessionState buildCurrentSession() const;
    void openSessionState(const macpad::persistence::SessionState &state);
    void rebuildRecentMenu();
    void refreshDocList();
    void refreshPanels();
    void updateDocMapRange();   // 以作用中編輯器可視範圍更新 Document Map 色帶（FR-029）
    void watchPath(const QString &path);
    macpad::ui::EditorPane *currentPane() const;
    macpad::ui::EditorPane *paneAt(int index) const;
    macpad::core::EditorWidget *currentEditor() const;
    macpad::core::EditorWidget *editorAt(int index) const;
    macpad::core::EditorWidget *addEditorTab();

    // === Dual-View 支援（兩個 QTabWidget 檢視容器）===
    // 目前作用中的檢視（最後取得焦點 / 切換分頁者）；預設 m_tabs
    QTabWidget *currentTabWidget() const;
    QTabWidget *otherTabWidget() const;                 // 相對於作用中檢視的另一個
    macpad::ui::EditorPane *paneIn(QTabWidget *w, int index) const;
    macpad::core::EditorWidget *editorIn(QTabWidget *w, int index) const;
    void setActiveTabWidget(QTabWidget *w);             // 切換作用中檢視並刷新面板/狀態列
    void wireTabWidget(QTabWidget *w);                  // 為某檢視接上關閉/右鍵/切換等訊號
    void wireEditorSignals(macpad::core::EditorWidget *editor);  // 編輯器→狀態列/標題連線（供分頁與 clone 共用）
    void closeTabIn(QTabWidget *w, int index);          // 關閉指定檢視的分頁（closeTab 的底層）
    void updateSecondViewVisibility();                  // 第二檢視空了就隱藏、有內容就顯示
    bool focusExistingPath(const QString &absPath);     // 兩檢視中找已開啟檔案並聚焦；回傳是否命中
    // 對兩檢視所有分頁的 primary 編輯器套用同一操作（跨 view 迭代）
    void forEachEditor(const std::function<void(macpad::core::EditorWidget *)> &fn) const;
    // 若分頁有未存變更，詢問存/不存/取消；回傳 false 表示使用者取消（FR-001 AC2）
    bool maybeSave(macpad::core::EditorWidget *editor);
    int indexOfPath(const QString &absPath) const;

    QTabWidget *m_tabs = nullptr;
    QTabWidget *m_tabs2 = nullptr;      // 第二檢視（Dual-View）；預設隱藏，移動/複製文件過去才顯示
    QSplitter *m_viewSplit = nullptr;   // 兩檢視並排的中央容器
    QTabWidget *m_activeTabs = nullptr; // 作用中檢視（currentTabWidget 依此回傳）
    // 狀態列六格（複刻 Notepad++）：文件類型 / 長度·行數 / 游標·選取 / EOL / 編碼 / INS·OVR
    QLabel *m_stDoc = nullptr;
    QLabel *m_stLenLines = nullptr;
    QLabel *m_stCaret = nullptr;
    QLabel *m_stEol = nullptr;
    QLabel *m_stEnc = nullptr;
    QLabel *m_stMode = nullptr;
    QToolBar *m_toolbar = nullptr;
    QVector<QPair<QAction *, QString>> m_tbIcons;  // 工具列動作→圖示名(供主題變色重上色)
    macpad::features::FindReplaceDialog *m_findDialog = nullptr;
    QMenu *m_recentMenu = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    std::unique_ptr<macpad::extension::ExtensionRegistry> m_extensions;
    macpad::features::FindInFilesDock *m_findInFiles = nullptr;
    macpad::features::RunDock *m_runDock = nullptr;
    macpad::features::FindAllDock *m_findAllDock = nullptr;  // Search ▸ Find All in Opened Documents…
    macpad::ui::DocumentListDock *m_docList = nullptr;
    QVector<QPair<QTabWidget *, int>> m_docListMap;  // 文件清單合併兩檢視：列索引→(檢視, 分頁索引)
    macpad::ui::FunctionListDock *m_funcList = nullptr;
    macpad::ui::ClipboardHistoryDock *m_clipHistory = nullptr;
    macpad::ui::DocumentMapDock *m_docMap = nullptr;
    macpad::ui::WorkspaceDock *m_workspace = nullptr;
    macpad::ui::CharacterPanel *m_charPanel = nullptr;
    macpad::ui::ProjectPanelDock *m_projectPanel = nullptr;  // Notepad++ Project Panel（FR-030 延伸）
    QMenu *m_fileMenu = nullptr;                 // 供 rebuildRecentMenu 於 File 選單直接列出最近檔案
    QList<QAction *> m_recentDirectActions;      // recentFilesInSubmenu=false 時直接掛在 File 選單的最近檔案動作
    QList<QShortcut *> m_macroShortcuts;         // 具名巨集的全域快捷鍵（依 macro_shortcuts.json 重建）
    QList<QShortcut *> m_runShortcuts;           // 已儲存 Run 指令的全域快捷鍵（依 run_commands.json 重建）
    QsciMacro *m_recordingMacro = nullptr;
    QTimer *m_snapshotTimer = nullptr;  // 當機復原快照計時器（可依偏好設定啟停/調整間隔）
    QMenu *m_windowMenu = nullptr;
    QStringList m_closedFiles;          // 最近關閉分頁堆疊（Restore Recent Closed File）
    QSet<QString> m_monitored;          // tail -f 監控中的檔案（絕對路徑）
    QString m_lastFindText;             // Find Next/Previous 的最後查詢
    QString m_lastDir;                   // defaultDirPolicy=RememberLast 用：上次開/存檔對話框目錄
    QToolBar *m_incBar = nullptr;       // 漸進式搜尋工具列
    QLineEdit *m_incSearch = nullptr;
    QList<QDockWidget *> m_dfHidden;    // Distraction Free 時暫時隱藏的面板
    QList<QDockWidget *> m_postItHidden;
    bool m_distractionFree = false;
    bool m_postIt = false;
    bool m_alwaysOnTop = false;
    bool m_showIndentGuide = true;
    bool m_showWrapSymbol = false;
    bool m_wordWrap = false;
    bool m_showWhitespace = false;
    bool m_showEol = false;
    macpad::features::UdlManager m_udl;
    QString m_savedMacro;
    QAction *m_recordAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_playAction = nullptr;
    // View 選單中的可勾選檢視動作——工具列同名按鈕透過它們切換，兩處 UI 狀態保持一致
    QAction *m_wrapAct = nullptr;
    QAction *m_wsAct = nullptr;
    QAction *m_eolAct = nullptr;
    QAction *m_smartHighlightAct = nullptr;  // View ▸ Smart Highlighting（新分頁依此套用）
};
