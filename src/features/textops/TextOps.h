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
    // Blend 變體：僅將目標位置字母轉大寫，其餘字母大小寫「保留原狀」（不像 toTitleCase/toSentenceCase 會將其餘字母轉小寫）
    static QString properCaseBlend(const QString &s);     // 每個單字首字母大寫，其餘不變
    static QString sentenceCaseBlend(const QString &s);   // 每句首字母大寫，其餘不變

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

    // 隨機大小寫（每個字母獨立隨機大寫/小寫）；seed < 0 表示使用亂數種子
    static QString toRandomCase(const QString &s, qint64 seed = -1);
    // 只合併「相鄰」重複行（與 removeDuplicateLines 的全域去重不同）
    static QString removeConsecutiveDuplicateLines(const QString &s);
    // 隨機打亂行順序；seed < 0 表示使用亂數種子
    static QString shuffleLines(const QString &s, qint64 seed = -1);
    // 地區化（locale-aware）排序，使用 QCollator
    static QString sortLinesLocale(const QString &s, bool ascending = true, bool caseSensitive = true);
    // 依行長度排序（穩定排序）
    static QString sortLinesByLength(const QString &s, bool ascending = true);
    // 將每行視為十進位數字排序；commaDecimal 表示以 ',' 作為小數點（會先正規化為 '.'）
    static QString sortLinesAsDecimals(const QString &s, bool ascending = true, bool commaDecimal = false);

    // --- 空白 ---
    static QString trimTrailing(const QString &s);
    static QString trimLeading(const QString &s);
    static QString tabsToSpaces(const QString &s, int tabWidth);
    static QString spacesToTabs(const QString &s, int tabWidth);
    static QString trimBoth(const QString &s);              // 每行同時去除頭尾空白
    static QString eolToSpace(const QString &s);            // 以單一空白取代換行，合併為一行
    // 只將「行首」連續空白（縮排）轉為 tab，行內其餘空白不動
    static QString spacesToTabsLeading(const QString &s, int tabWidth);
    // 組合操作：先對每行同時去除頭尾空白（同 trimBoth），再將行間換行轉為單一空白（同 eolToSpace）合併成一行
    static QString trimBothAndEolToSpace(const QString &s);

    // --- 註解切換（行註解）---
    // 若區塊所有非空行皆已註解 → 取消；否則加註解。marker 如 "//"、"#"。
    static QString toggleLineComment(const QString &s, const QString &marker);
};

}  // namespace macpad::features
