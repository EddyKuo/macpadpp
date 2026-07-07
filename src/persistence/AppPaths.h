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
};

}  // namespace macpad::persistence
