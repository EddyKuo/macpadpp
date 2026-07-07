#pragma once

// UdlDefinition — 使用者自訂語言定義（FR-032, DR-005）
// 由 JSON 載入；純資料 + 解析，可單元測試。

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace macpad::features {

struct UdlDefinition {
    QString name;
    QStringList extensions;      // 不含點
    QSet<QString> keywords;
    QString lineComment;         // 如 "#"、"//"
    QString blockCommentStart;   // 如 "/*"
    QString blockCommentEnd;     // 如 "*/"
    bool caseSensitive = true;

    bool isValid() const { return !name.isEmpty(); }

    static UdlDefinition fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

}  // namespace macpad::features
