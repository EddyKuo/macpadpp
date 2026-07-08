#pragma once

// ApiDatabase — 各語言自動完成資料庫（FR-055）
// 純靜態工具類，提供關鍵字/常用函式清單與呼叫提示（call-tip），無 Qt widget 依賴，可完整單元測試。
// 語言代碼（langKey）與 core/LexerFactory 使用的一致："cpp","python","javascript",
// "css","html","sql","bash","json" 等；未知語言一律回傳空結果。

#include <QString>
#include <QStringList>

namespace macpad::features {

class ApiDatabase {
public:
    // 依語言代碼回傳自動完成清單（關鍵字 + 常用函式/內建名稱）；未知語言回傳空清單。
    static QStringList entriesFor(const QString &langKey);

    // 依詞彙與語言代碼回傳呼叫提示（函式簽名/用法說明）；未知則回傳空字串。
    static QString callTipFor(const QString &word, const QString &langKey);

    // 依路徑片段（可能包含目錄部分）回傳檔案系統路徑自動完成候選清單。
    // fragment 的目錄部分決定搜尋目錄，檔名部分作為前綴過濾。
    static QStringList completePath(const QString &fragment);
};

}  // namespace macpad::features
