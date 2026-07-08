#pragma once

// AppPaths — 本地持久化檔案位置（profile §1；CON-003 用 QStandardPaths）
// macOS: ~/Library/Application Support/macpad++/

#include <QString>

namespace macpad::persistence {

class AppPaths {
public:
    // 設定目錄（不存在則建立）；回傳絕對路徑
    static QString configDir();
    // 設定目錄下的檔案路徑
    static QString filePath(const QString &name);
    // 覆寫設定目錄（供命令列 -settingsDir <dir>）；須在任何設定讀取前呼叫。空字串還原為預設。
    static void setConfigDirOverride(const QString &dir);
};

}  // namespace macpad::persistence
