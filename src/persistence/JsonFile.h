#pragma once

// JsonFile — JSON 檔案讀寫工具（dba-engine-file-config）
// 原子寫入（QSaveFile：temp + fsync + rename）；讀取 corruption-safe（解析失敗回空物件，
// 不崩潰）——FR-016 AC3、ADR-5。

#include <QJsonObject>
#include <QString>

namespace macpad::persistence {

class JsonFile {
public:
    // 讀取；檔案不存在或解析失敗回傳空 QJsonObject（不視為錯誤）
    static QJsonObject load(const QString &path);
    // 原子寫入；成功回傳 true
    static bool save(const QString &path, const QJsonObject &obj);
};

}  // namespace macpad::persistence
