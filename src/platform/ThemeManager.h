#pragma once

// ThemeManager — 深色模式跟隨系統（FR-021, IR-002）+ 具名主題套用（FR-056）
// v1：偵測系統 light/dark，套用編輯器基礎配色（paper/text/caret/margin）。
// lexer 語法色 v1 沿用預設；完整主題引擎（Style Configurator）列後續。
// FR-056：applyNamedTheme() 套用 ThemeStore 中具名、可分享的主題（含 dark 旗標 + 樣式覆寫）。

#include <QString>

namespace macpad::core { class EditorWidget; }

namespace macpad::platform {

class ThemeManager {
public:
    // 系統目前是否為深色（Qt 6.5+ QStyleHints::colorScheme）
    static bool systemIsDark();

    // 套用基礎配色到編輯器（樣式覆寫來源：StyleStore，即 Style Configurator 設定）
    static void applyToEditor(macpad::core::EditorWidget *editor, bool dark);

    // 套用 ThemeStore 中名為 themeName 的具名主題（FR-056）：
    // 讀取該主題的 dark 旗標與樣式覆寫並套用，語意與 applyToEditor 一致。
    // 主題不存在時不做任何事（不 crash）。
    static void applyNamedTheme(macpad::core::EditorWidget *editor, const QString &themeName);
};

}  // namespace macpad::platform
