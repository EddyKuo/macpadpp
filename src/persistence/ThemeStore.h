#pragma once

// ThemeStore — themes/*.json：具名、可分享的主題（FR-056）。
// 每個主題是一份獨立 JSON（放在設定目錄下 themes/ 子目錄），內容為 name + dark + StyleSettings
// （沿用 StyleStore 的欄位語意：全域字型 + 每語言每 style 覆寫）。
// ThemeManager::applyNamedTheme() 讀取具名主題並套用到編輯器。

#include "persistence/StyleStore.h"

#include <QString>
#include <QStringList>

namespace macpad::persistence {

struct Theme {
    QString name;
    bool dark = false;
    StyleSettings styles;
};

class ThemeStore {
public:
    // 已儲存主題名稱清單（依檔名排序）
    static QStringList listThemes();
    // 讀取指定名稱的主題；不存在或解析失敗回傳空白 Theme（name 為空）
    static Theme load(const QString &name);
    // 儲存（以 theme.name 為檔名，同名覆蓋）；name 為空則失敗
    static bool save(const Theme &theme);
    // 從外部 JSON 檔匯入一份主題（存入本地 themes/ 目錄）
    static bool importFromFile(const QString &path);
    // 將指定名稱的主題匯出成獨立 JSON 檔
    static bool exportToFile(const QString &name, const QString &path);
    // 刪除指定名稱的主題
    static bool remove(const QString &name);

    // 把打包的內建主題（Qt 資源 :/themes/*.json，複刻各大廠 IDE 配色）植入使用者主題目錄。
    // 僅在同名主題「尚未存在」時寫入——不覆蓋使用者的修改或刪除（刪掉的內建主題不會被還原）。
    // 應在啟動時呼叫一次；回傳本次新植入的主題數。
    static int seedBundledThemes();
};

}  // namespace macpad::persistence
