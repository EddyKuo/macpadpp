#pragma once

// UdlManager — UDL 定義的載入/儲存/查找（FR-032, DR-005）
// 從設定目錄 udl/ 載入 *.json；依副檔名查找。

#include <QVector>

#include "features/udl/UdlDefinition.h"

namespace macpad::features {

class UdlManager {
public:
    void loadAll();                              // 從設定 udl/ 目錄載入全部
    bool save(const UdlDefinition &def);         // 存至 udl/{name}.json 並加入記憶體
    bool importFromFile(const QString &path);    // 匯入外部 JSON UDL 檔並儲存
    bool exportToFile(const QString &name, const QString &path);  // 匯出已存在的 UDL 至外部檔（FR-059）
    bool rename(const QString &oldName, const QString &newName);  // 重新命名已存在的 UDL（FR-059 擴充）
    bool remove(const QString &name);                             // 移除已存在的 UDL（FR-059 擴充）
    const UdlDefinition *findForExtension(const QString &suffix) const;

    const QVector<UdlDefinition> &definitions() const { return m_defs; }

private:
    QVector<UdlDefinition> m_defs;
};

}  // namespace macpad::features
