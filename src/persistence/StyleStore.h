#pragma once

// StyleStore — styles.json：Style Configurator 的樣式覆寫（複刻 Notepad++ Style Configurator）。
// 儲存全域字型 + 每語言每 style 的前景/背景/粗體/斜體覆寫；ThemeManager 在套用主題後疊上這些覆寫。

#include <QHash>
#include <QString>
#include <QVector>

namespace macpad::persistence {

struct StyleOverride {
    int style = 0;
    QString fg;         // #RRGGBB，空 = 不覆寫
    QString bg;         // #RRGGBB，空 = 不覆寫
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // 使用者自訂關鍵字清單（覆寫內建 lexer 關鍵字集，空白分隔；空字串 = 不覆寫）。
    // 僅對支援關鍵字的 style（如 SCE_C_WORD）有意義；LexerFactory/ThemeManager 套用時查詢此欄位。
    QString keywords;
};

// GlobalStyles — 編輯器全域（非 lexer）樣式覆寫（複刻 Notepad++ Style Configurator 的
// 「Global Styles」分類）。每個欄位為 #RRGGBB，空字串 = 沿用主題預設色。
struct GlobalStyles {
    QString indentGuide;        // 縮排參考線顏色
    QString caretLineBg;        // 目前行反白背景色
    QString selectionBg;        // 選取範圍背景色
    QString whitespaceFg;       // 顯示空白字元時的顏色
    QString marginBg;           // 行號邊界背景色
    QString marginFg;           // 行號邊界前景色
    QString currentLineNumber;  // 目前行行號顏色
    QString edgeColor;          // Edge（欄位參考線）顏色
    QString bookmarkMargin;     // 書籤邊界顏色
    QString foldMargin;         // 摺疊邊界顏色
    QString caretColor;         // 插入點（游標）顏色，獨立於「目前行反白背景色」
    QString markColor;          // 標記（Find Mark / Bookmark 標記列）顏色
    QString badBrace;                    // 不匹配括號（Bad Brace）顏色
    QString foldActive;                  // 摺疊邊界「作用中」狀態顏色（滑鼠停留於摺疊三角形上）
    QString changeHistoryModifiedMargin; // Change History 邊界：已修改（未存檔）標記顏色
    QString changeHistorySavedMargin;    // Change History 邊界：已存檔標記顏色
    QString changeHistoryRevertedMargin; // Change History 邊界：已還原標記顏色
    QString urlHovered;                  // URL 連結偵測滑鼠停留時的顏色

    // Global override：開啟時以單一前景/背景色套用到所有語言的所有 style，
    // 疊在各語言個別覆寫之上（供快速套用一致配色，如全域深色前景）。
    bool enableGlobalFg = false;
    QString globalFg;
    bool enableGlobalBg = false;
    QString globalBg;
};

struct StyleSettings {
    QString fontFamily;                                   // 空 = 用預設等寬字型
    int fontSize = 0;                                     // 0 = 用預設
    QHash<QString, QVector<StyleOverride>> byLang;        // 語言鍵（LexerFactory key）→ 覆寫清單
    GlobalStyles global;                                  // 全域（非 lexer）樣式覆寫
    QHash<QString, QString> userExtensions;               // 語言（lexer language() 字串）→ 使用者自訂副檔名（以逗號/空白分隔）
};

class StyleStore {
public:
    static StyleSettings load();
    static bool save(const StyleSettings &s);

    // 查詢指定語言（LexerFactory language() 字串）、指定 style 的使用者自訂關鍵字覆寫；
    // 未設定則回傳空字串（呼叫端應維持使用該 lexer 的內建關鍵字集）。
    // 供 LexerFactory / ThemeManager 套用 lexer 時查詢是否有關鍵字覆寫。
    static QString userKeywordsFor(const StyleSettings &s, const QString &lang, int style);
};

}  // namespace macpad::persistence
