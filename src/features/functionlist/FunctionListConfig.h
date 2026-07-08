#pragma once

// FunctionListConfig — Function List 解析規則的外部可配置化（FR-029 擴充）
// 對齊 Notepad++ functionList/*.xml + overrideMap.xml 的概念：
// 每個語言鍵對應一組規則（函式擷取正則 + 可選的類別/範疇擷取正則），
// 使用者可在設定目錄 functionlist/ 下放置 JSON 檔覆寫或新增語言規則，
// 未提供使用者檔時退回內建預設值（維持既有行為不變）。
//
// 純邏輯、可單元測試；不依賴 QScintilla。

#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::features {

// 單一語言的 Function List 解析規則集
struct FunctionListRule {
    // 函式/符號擷取正則（依序嘗試，取第一個命中者）；每個正則第 1 個捕獲群組為符號名稱
    QStringList functionExprs;
    // 類別/命名空間（範疇）擷取正則；第 1 個捕獲群組為範疇名稱。空字串代表不支援範疇追蹤。
    QString classExpr;
    // 是否追蹤所屬範疇（trackScope）：true 時才會依大括號深度堆疊 classExpr 命中的範疇
    bool trackScope = false;
    // 排除的控制流關鍵字誤判清單（如 if/for/while…）；為空時使用內建預設清單
    QStringList keywordExclusions;

    bool isValid() const { return !functionExprs.isEmpty(); }
};

class FunctionListConfig {
public:
    // 取得指定語言鍵的規則集：使用者檔（functionlist/<language>.json）優先於內建預設值；
    // 兩者皆無則回傳 isValid()==false 的空規則（呼叫端應退回無符號輸出，行為與過去不支援語言一致）。
    static FunctionListRule ruleForLanguage(const QString &language);

    // overrideMap.json（functionlist/overrideMap.json）：副檔名 -> 語言鍵 的額外對照，
    // 優先於 FunctionListParser 內建的 suffix 對照表。查無則回傳空字串。
    static QString languageOverrideForSuffix(const QString &suffix);

    // 內建預設規則（language 查無使用者檔時的 fallback；亦供測試直接驗證預設行為不變）。
    // 查無對應內建語言則回傳 isValid()==false。
    static FunctionListRule builtinRule(const QString &language);

    // 強制清除記憶體快取，下次呼叫重新讀取磁碟（使用者於執行期間手動編輯設定檔後可呼叫；
    // 主要供測試使用，一般執行流程不需呼叫）。
    static void reloadCache();

    // 設定目錄下的 functionlist 子目錄路徑（不存在則建立）；供測試準備使用者覆寫檔。
    static QString configDirPath();
};

}  // namespace macpad::features
