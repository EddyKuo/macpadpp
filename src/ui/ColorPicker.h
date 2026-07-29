#pragma once

// ColorPicker — 記住使用者自訂色的取色器（複刻 Notepad++ v8.9.7
// 「Enhance color picker to remember custom colors across sessions」）
//
// QColorDialog 的「自訂顏色」格是行程層級的全域狀態，程式重啟後就消失。
// 本類別在每次取色後把 16 格自訂色寫回設定檔，並於啟動時填回，使其跨 session 保留。
// 所有取色點（分頁標色、Style Configurator、UDL Styler）一律走此處，行為才一致。

#include <QColor>
#include <QString>

class QWidget;

namespace macpad::ui {

class ColorPicker {
public:
    // 啟動時呼叫一次：把設定檔中的自訂色填回 QColorDialog 的自訂色格
    static void restoreCustomColors();

    // 取代 QColorDialog::getColor：行為相同，但關閉後會保存自訂色格內容。
    // 使用者取消時回傳無效 QColor（與 QColorDialog::getColor 一致）。
    static QColor getColor(const QColor &initial, QWidget *parent, const QString &title);

    // 把目前 QColorDialog 的自訂色格寫回設定（getColor 內部會呼叫；公開以利測試）
    static void persistCustomColors();
};

}  // namespace macpad::ui
