#pragma once

// ThemeManager — 深色模式跟隨系統（FR-021, IR-002）
// v1：偵測系統 light/dark，套用編輯器基礎配色（paper/text/caret/margin）。
// lexer 語法色 v1 沿用預設；完整主題引擎（Style Configurator）列後續。

namespace macpad::core { class EditorWidget; }

namespace macpad::platform {

class ThemeManager {
public:
    // 系統目前是否為深色（Qt 6.5+ QStyleHints::colorScheme）
    static bool systemIsDark();

    // 套用基礎配色到編輯器
    static void applyToEditor(macpad::core::EditorWidget *editor, bool dark);
};

}  // namespace macpad::platform
