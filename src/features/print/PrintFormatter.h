#pragma once

// PrintFormatter — 列印頁首/頁尾的變數展開（複刻 Notepad++ Print 偏好的 header/footer 變數）
// 純函式、不依賴 QPrinter 或 widget，可完整單元測試。
//
// 支援的變數（與 Notepad++ 同名，便於使用者沿用既有樣板）：
//   $(FULL_CURRENT_PATH) 完整路徑      $(CURRENT_DIRECTORY) 所在目錄
//   $(FILE_NAME)         檔名（含副檔名） $(NAME_PART)      主檔名（不含副檔名）
//   $(EXT_PART)          副檔名           $(CURRENT_DATE)   日期
//   $(CURRENT_TIME)      時間             $(CURRENT_PAGE)   頁碼
//   $(NB_PAGES)          總頁數（未知時展開為空字串，不假造數字）
//
// 未知變數原樣保留——靜默吞掉會讓使用者以為打錯字的樣板「生效了」。

#include <QString>

namespace macpad::features {

struct PrintContext {
    QString filePath;    // 空 = 未命名文件
    QString dateText;    // 已格式化的日期（呼叫端決定 locale/格式）
    QString timeText;    // 已格式化的時間
    int pageNumber = 1;  // 1-based
    int pageCount = 0;   // 0 = 未知（$(NB_PAGES) 展開為空）
};

class PrintFormatter {
public:
    // 展開樣板中的變數；樣板為空時回傳空字串。
    static QString expand(const QString &tmpl, const PrintContext &ctx);
};

}  // namespace macpad::features
