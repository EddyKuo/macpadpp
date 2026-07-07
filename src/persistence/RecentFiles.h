#pragma once

// RecentFiles — recent.json：最近開啟檔案（FR-017, DR-003）
// 時間倒序、去重、上限（預設 20）。

#include <QStringList>

namespace macpad::persistence {

class RecentFiles {
public:
    static QStringList load();
    static bool add(const QString &path);   // 去重、置頂、裁切至上限後存檔；回傳寫檔是否成功
    static bool clear();                     // 回傳寫檔是否成功

    static constexpr int kMaxItems = 20;
    static constexpr int kSchemaVersion = 1;
};

}  // namespace macpad::persistence
