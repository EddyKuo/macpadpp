#pragma once

// RtfExporter — 把目前文件連同語法高亮輸出為 RTF（複刻 Notepad++ Export as RTF）
//
// 與 HtmlExporter 相同的作法：依 Scintilla 的逐字元樣式切成連續色段，
// 再以 RTF 的顏色表（\colortbl）與 \cfN 控制詞著色。
// 純函式、不依賴 GUI 事件迴圈，可單元測試。

#include <QString>

namespace macpad::core { class EditorWidget; }

namespace macpad::features {

class RtfExporter {
public:
    // 產生完整 RTF 文件字串（含 header、字型表、顏色表）。editor 為 nullptr 時回傳空字串。
    static QString toRtf(macpad::core::EditorWidget *editor);

    // 將純文字轉為 RTF 內文：跳脫 \ { }，換行轉 \par，非 ASCII 轉 \uN? 形式。
    static QString rtfEscape(const QString &text);
};

}  // namespace macpad::features
