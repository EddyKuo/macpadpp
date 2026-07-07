#pragma once

// SettingsStore — settings.json：應用設定（FR-015/021, DR-001）
// 缺欄位以預設值回填（dba-engine-file-config）。

#include <QString>

namespace macpad::persistence {

enum class ThemeMode { System, Light, Dark };

struct Settings {
    int schemaVersion = 1;
    bool restoreOnLaunch = true;         // FR-016
    ThemeMode theme = ThemeMode::System; // FR-021
    bool autosaveEnabled = false;        // FR-015
    int autosaveIntervalSec = 60;        // FR-015
    int tabWidth = 4;                    // FR-009
    bool singleInstance = true;          // FR-034
};

class SettingsStore {
public:
    static Settings load();
    static bool save(const Settings &s);
};

}  // namespace macpad::persistence
