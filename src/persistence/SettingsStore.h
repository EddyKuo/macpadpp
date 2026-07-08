#pragma once

// SettingsStore — settings.json：應用設定（FR-015/021, DR-001）
// 缺欄位以預設值回填（dba-engine-file-config）。

#include <QMap>
#include <QString>
#include <QStringList>

#include "core/FileEncoding.h"

namespace macpad::persistence {

enum class ThemeMode { System, Light, Dark };

// 備份模式（FR-053）：None=不備份；Simple=同目錄備份；Verbose=備份至指定目錄並保留時間戳
enum class BackupMode { None, Simple, Verbose };

// 工具列圖示大小
enum class ToolbarIconSize { Small, Standard, Large };

// 邊界線模式：None=不顯示；Line=垂直線；Background=超過欄位背景變色
enum class EdgeMode { None, Line, Background };

// 摺疊邊界樣式（對應 Notepad++ Fold Margin Style）
enum class FoldMarginStyle { None, Simple, Arrow, Circle, Box };

// 預設開啟/儲存目錄策略
enum class DefaultDirPolicy { FollowCurrentDoc, RememberLast, FixedPath };

// 多重執行個體模式（取代舊有單純 bool singleInstance 的語意，向下相容保留該欄位）
enum class MultiInstanceMode { MonoInstance, MultiInstOnSession, AlwaysMulti };

// 檔案外部變更偵測模式（MISC 頁；比 New Document 頁的 autoDetectFileStatus 更細緻）
enum class FileStatusAutoDetectMode { Disabled, Enabled, EnabledSilent };

struct Settings {
    int schemaVersion = 1;
    bool restoreOnLaunch = true;         // FR-016
    ThemeMode theme = ThemeMode::System; // FR-021
    bool autosaveEnabled = false;        // FR-015
    int autosaveIntervalSec = 60;        // FR-015
    int tabWidth = 4;                    // FR-009
    bool singleInstance = true;          // FR-034
    QString language;                    // 介面語言:""=系統;"zh_TW"/"zh_CN"/"ja"/"en"

    // === Editing（FR-053）===
    bool showLineNumbers = true;
    bool showIndentGuides = true;
    bool wordWrap = false;
    bool showWhitespace = false;
    int caretWidth = 1;
    bool currentLineHighlight = true;      // 高亮目前所在行
    bool enableVirtualSpace = false;       // 允許插入點移至行尾之後的虛擬空白
    bool copyLineWithoutSelection = true;  // 無選取時複製/剪下整行（Notepad++ 慣例）
    bool columnSelectionToMultiEdit = false;  // 欄選（矩形選取）結束時轉為每行一個多游標

    // === New Document（FR-053）===
    macpad::core::Eol defaultEol = macpad::core::Eol::Lf;
    macpad::core::Encoding defaultEncoding = macpad::core::Encoding::Utf8;
    bool autoDetectFileStatus = true;  // 偵測檔案於外部被異動/刪除並提示
    QString sessionFileExt;            // 自動以 session 開啟的副檔名（空=停用）

    // === Backup（FR-053）===
    BackupMode backupMode = BackupMode::None;
    QString backupDir;
    bool autosaveOnFocusLoss = false;
    bool enableSessionSnapshot = true;  // 啟用當機還原快照計時器
    int snapshotIntervalSec = 30;       // 快照間隔（秒）

    // === Auto-Completion（FR-053）===
    bool autoInsertPairs = true;
    bool wordAutoComplete = true;
    int acThreshold = 3;
    bool showCallTips = true;

    // === Performance（FR-053）===
    int largeFileMB = 200;
    int disableAutoCompleteOverMB = 50;

    // === Search（FR-053）===
    QString searchEngineUrl;  // 「在網路上搜尋」使用的搜尋引擎網址模板
    bool keepFindDialogOpen = true;      // 執行搜尋後保持「尋找」對話框開啟
    bool confirmReplaceAll = true;       // 「全部取代」前顯示確認
    int findInSelectionThreshold = 0;    // 選取範圍字元數超過此值時預設啟用「在選取範圍內尋找」（0=關閉）

    // === Highlighting（新增）===
    bool showWrapSymbol = false;       // 換行處顯示折行符號
    bool showEol = false;              // 顯示換行字元
    bool smartHighlight = true;        // 智慧高亮：自動標示與選取字詞相同的字串
    bool highlightMatchingTags = false;// 標示相符的 HTML/XML 標籤
    int edgeColumn = 0;                // 垂直邊界線欄位（0=關閉）
    bool multiEdgeEnabled = false;     // 啟用多重邊界線

    // === Dark Mode / Appearance（新增；沿用既有 ThemeMode）===
    bool showToolbar = true;   // 顯示工具列
    bool showStatusBar = true; // 顯示狀態列
    bool showTabBar = true;    // 顯示分頁列
    int caretBlinkRate = 500;  // 插入點閃爍週期（毫秒，0=不閃爍）

    // === Toolbar（新增）===
    ToolbarIconSize toolbarIconSize = ToolbarIconSize::Standard;

    // === Tab Bar（新增）===
    bool tabBarMultiLine = false;          // 多行顯示分頁（放不下時換行，而非捲動）
    bool tabBarVertical = false;           // 分頁列垂直排列
    bool tabBarShowCloseButton = true;     // 分頁上顯示關閉按鈕
    bool tabBarDoubleClickCloses = false;  // 雙擊分頁關閉

    // === Margins / Border / Edge（新增；edgeColumn 沿用既有 Highlighting 欄位）===
    EdgeMode edgeMode = EdgeMode::None;
    FoldMarginStyle foldMarginStyle = FoldMarginStyle::Simple;
    bool lineNumberMargin = true;  // 行號邊界顯示（Margins 頁獨立開關，UI 與 showLineNumbers 呼應）

    // === Default Directory（新增）===
    DefaultDirPolicy defaultDirPolicy = DefaultDirPolicy::FollowCurrentDoc;
    QString defaultDirFixedPath;  // defaultDirPolicy == FixedPath 時使用

    // === Recent Files History（新增）===
    int recentFilesMaxEntries = 10;
    bool recentFilesShowFullPath = false;
    bool recentFilesInSubmenu = false;  // false=直接列於 File 選單；true=收於子選單

    // === Language menu / per-language indentation（新增）===
    QStringList disabledLanguages;       // 停用的語言（語法高亮/Language 選單隱藏）
    QMap<QString, int> perLangTabWidth;  // 語言名稱 -> Tab 寬度覆寫（未列出者使用全域 tabWidth）

    // === Multi-Instance & Date（新增；singleInstance 保留供舊版相容）===
    MultiInstanceMode multiInstanceMode = MultiInstanceMode::MultiInstOnSession;
    QString dateFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");  // Date/Time 插入格式
    QString customDateFormat;  // 使用者自訂格式（dateFormat == "custom" 時採用此欄位）

    // === Delimiter（新增）===
    QString delimiterChars = QStringLiteral("+-/\\*!?=()[]{}<>\"'.,;:");  // 雙擊選字的邊界字元
    bool ctrlDoubleClickWholeWord = true;  // Ctrl+雙擊選整個字（含被 delimiter 排除的部分）

    // === MISC（新增）===
    bool docSwitcherEnabled = true;      // 啟用文件切換器（Ctrl+Tab 清單）
    bool docPeekerEnabled = true;        // 切換時預覽文件內容
    FileStatusAutoDetectMode fileStatusAutoDetect = FileStatusAutoDetectMode::Enabled;
    bool autoUpdater = false;            // macOS 上僅儲存使用者偏好，不執行實際更新檢查
    bool enableSound = false;            // 動作提示音（如尋找到底/取代完成）
};

class SettingsStore {
public:
    static Settings load();
    static bool save(const Settings &s);
};

}  // namespace macpad::persistence
