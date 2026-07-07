#pragma once

// HtmlExporter — 匯出為保留語法高亮的 HTML（FR-036）
// htmlEscape 為純函式可單元測試；toHtml 讀取編輯器樣式產生著色 HTML。

#include <QString>

namespace macpad::core { class EditorWidget; }

namespace macpad::features {

class HtmlExporter {
public:
    static QString htmlEscape(const QString &text);
    static QString toHtml(macpad::core::EditorWidget *editor);
};

}  // namespace macpad::features
