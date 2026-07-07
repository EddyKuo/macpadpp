#pragma once

// TextOps — 文字轉換操作（Notepad++ Edit 選單對等）
// 全部為純函式，輸入輸出皆 QString，可完整單元測試。
// 大小寫、行操作（排序/去重/去空行/移動/複製/合併/反轉）、空白操作、註解切換。

#include <QString>

namespace macpad::features {

class TextOps {
public:
    // --- 大小寫 ---
    static QString toUpper(const QString &s);
    static QString toLower(const QString &s);
    static QString toTitleCase(const QString &s);     // 每個單字首字母大寫
    static QString toSentenceCase(const QString &s);  // 每句首字母大寫
    static QString invertCase(const QString &s);      // 大小寫互換

    // --- 行操作 ---
    static QString sortLinesAscending(const QString &s, bool caseSensitive = true);
    static QString sortLinesDescending(const QString &s, bool caseSensitive = true);
    static QString sortLinesNumeric(const QString &s, bool ascending = true);
    static QString removeDuplicateLines(const QString &s);
    static QString removeEmptyLines(const QString &s, bool blankMeansWhitespace = true);
    static QString reverseLines(const QString &s);
    static QString joinLines(const QString &s, const QString &sep = QString());
    static QString duplicateLines(const QString &s);  // 每行重複一次

    // 移動指定（0-based）行區間上/下一行；回傳新文字與新的選取行範圍
    static QString moveLinesUp(const QString &s, int firstLine, int lastLine, int *newFirst);
    static QString moveLinesDown(const QString &s, int firstLine, int lastLine, int *newFirst);

    // --- 空白 ---
    static QString trimTrailing(const QString &s);
    static QString trimLeading(const QString &s);
    static QString tabsToSpaces(const QString &s, int tabWidth);
    static QString spacesToTabs(const QString &s, int tabWidth);

    // --- 註解切換（行註解）---
    // 若區塊所有非空行皆已註解 → 取消；否則加註解。marker 如 "//"、"#"。
    static QString toggleLineComment(const QString &s, const QString &marker);
};

}  // namespace macpad::features
