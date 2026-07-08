#pragma once

// SettingsStore — settings.json：應用設定（FR-015/021, DR-001）
// 缺欄位以預設值回填（dba-engine-file-config）。

#include <QString>

#include "core/FileEncoding.h"

namespace macpad::persistence {

enum class ThemeMode { System, Light, Dark };

// 備份模式（FR-053）：None=不備份；Simple=同目錄備份；Verbose=備份至指定目錄並保留時間戳
enum class BackupMode { None, Simple, Verbose };

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
};

class SettingsStore {
public:
    static Settings load();
    static bool save(const Settings &s);
};

}  // namespace macpad::persistence
