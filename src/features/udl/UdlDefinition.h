#pragma once

// UdlDefinition — 使用者自訂語言定義（FR-032, FR-059, DR-005）
// 由 JSON 載入；純資料 + 解析，可單元測試。

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::features {

// 最多支援的關鍵字群組數（對齊 Notepad++ 8 組 KEYWORDx，FR-059）
constexpr int kUdlMaxKeywordGroups = 8;

// 單一樣式外觀（前景/背景色 + 粗體/斜體/底線），用於 UDL Styler（③a）。
// fg/bg 為空字串代表「未設定，沿用內建預設色」，以維持向後相容。
// 色碼格式如 "#RRGGBB"。styleId 對應 UdlLexer::Style 列舉值（此處以 int 儲存，
// 避免 UdlDefinition 反向依賴 UdlLexer）。
struct UdlStyle {
    QString fg;
    QString bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

// 成對分隔符區塊（如自訂字串/區塊界定），可選跳脫字元（FR-059）
struct UdlDelimiter {
    QString open;
    QString escape;   // 可為空：無跳脫字元
    QString close;
};

// 摺疊符（開頭/中間/結尾字串），用於產生 fold points（FR-059）
struct UdlFolderTokens {
    QString open;
    QString middle;
    QString close;

    bool isEmpty() const { return open.isEmpty() && middle.isEmpty() && close.isEmpty(); }
};

struct UdlDefinition {
    QString name;
    QStringList extensions;      // 不含點
    QSet<QString> keywords;      // 向後相容欄位：未使用多組時等同第 0 組
    QVector<QSet<QString>> keywordGroups;  // 最多 8 組（FR-059）；為空時退回 keywords
    QSet<QString> operators;               // 運算子（FR-059）
    QVector<UdlDelimiter> delimiters;       // 自訂分隔符區塊（FR-059）
    UdlFolderTokens folderTokens;            // 摺疊符（FR-059）
    QString lineComment;         // 如 "#"、"//"
    QString blockCommentStart;   // 如 "/*"
    QString blockCommentEnd;     // 如 "*/"
    bool caseSensitive = true;
    // 每個樣式（鍵為 UdlLexer::Style 列舉值）的外觀設定；未設定的樣式回退至
    // UdlLexer::defaultColor() 內建預設色（向後相容，③a）。
    QMap<int, UdlStyle> styles;

    bool isValid() const { return !name.isEmpty(); }

    // 取得第 idx 組關鍵字（0-based）。未設定 keywordGroups 時，第 0 組回退至 keywords（向後相容）。
    // 越界回傳空集合的參照。
    const QSet<QString> &keywordGroup(int idx) const;

    static UdlDefinition fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

}  // namespace macpad::features
