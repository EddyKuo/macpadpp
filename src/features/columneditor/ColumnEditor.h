#pragma once

// ColumnEditor — 欄位編輯：於多行同一欄位插入遞增數列（Notepad++ Column Editor 對等）
// 純邏輯、可單元測試。

#include <QString>

namespace macpad::features {

struct NumberSeqSpec {
    int start = 1;
    int increment = 1;
    int base = 10;        // 10 / 16 / 8 / 2
    int width = 0;        // 補零至寬度（0 = 不補）
    bool upperHex = true;
    int repeat = 1;       // 每個數值重複幾行後才遞增（1 = 每行都遞增；Notepad++ Repeat）
};

class ColumnEditor {
public:
    // 依規格格式化單一數值
    static QString formatNumber(int value, const NumberSeqSpec &spec);

    // 於 text 的 [firstLine..lastLine]（0-based）每行第 col 欄插入遞增數列。
    // 行長不足 col 時以空白補齊至 col。回傳新文字。
    static QString insertNumberColumn(const QString &text, int firstLine, int lastLine,
                                      int col, const NumberSeqSpec &spec);

    // 於多行同欄插入固定文字（非數列）
    static QString insertTextColumn(const QString &text, int firstLine, int lastLine,
                                    int col, const QString &insertText);
};

}  // namespace macpad::features
