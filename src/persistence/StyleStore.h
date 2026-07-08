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
};

}  // namespace macpad::persistence
