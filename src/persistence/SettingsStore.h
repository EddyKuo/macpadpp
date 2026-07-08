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

    // === New Document（FR-053）===
    macpad::core::Eol defaultEol = macpad::core::Eol::Lf;
    macpad::core::Encoding defaultEncoding = macpad::core::Encoding::Utf8;

    // === Backup（FR-053）===
    BackupMode backupMode = BackupMode::None;
    QString backupDir;
    bool autosaveOnFocusLoss = false;

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
};

class SettingsStore {
public:
    static Settings load();
    static bool save(const Settings &s);
};

}  // namespace macpad::persistence
