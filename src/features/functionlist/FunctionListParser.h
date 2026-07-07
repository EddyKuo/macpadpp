#pragma once

// FunctionListParser — 由原始碼解析函式/類別符號（FR-029 Function List）
// 純邏輯、可單元測試。以語言別的正則啟發式擷取符號名與行號。

#include <QString>
#include <QVector>

namespace macpad::features {

struct Symbol {
    QString name;
    int line;   // 1-based
};

class FunctionListParser {
public:
    // language：由副檔名或 lexer 名推得的小寫語言鍵（"python","cpp","javascript"…）
    static QVector<Symbol> parse(const QString &content, const QString &language);

    // 由副檔名推語言鍵
    static QString languageForSuffix(const QString &suffix);
};

}  // namespace macpad::features
