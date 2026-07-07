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
};

struct StyleSettings {
    QString fontFamily;                                   // 空 = 用預設等寬字型
    int fontSize = 0;                                     // 0 = 用預設
    QHash<QString, QVector<StyleOverride>> byLang;        // 語言鍵（LexerFactory key）→ 覆寫清單
};

class StyleStore {
public:
    static StyleSettings load();
    static bool save(const StyleSettings &s);
};

}  // namespace macpad::persistence
