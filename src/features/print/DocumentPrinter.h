#pragma once

// DocumentPrinter — 帶頁首/頁尾與邊界的列印器（複刻 Notepad++ Print 偏好）
// QsciPrinter 本身只印內文；覆寫 formatPage() 才能在每頁上下緣繪製頁首/頁尾，
// 並把內文可用區域縮小，避免文字疊到頁首頁尾上。
//
// 頁首/頁尾內容由 features/print/PrintFormatter 展開變數（該處為純函式、可單元測試）；
// 本類別只負責「畫」與「量」，不含字串邏輯。

#include <Qsci/qsciprinter.h>

#include <QString>

namespace macpad::features {

class DocumentPrinter : public QsciPrinter {
public:
    DocumentPrinter() = default;

    // 目前文件路徑（供 $(FILE_NAME) 等變數展開；空 = 未命名）
    void setFilePath(const QString &path) { m_filePath = path; }
    void setHeaderTemplate(const QString &tmpl) { m_header = tmpl; }
    void setFooterTemplate(const QString &tmpl) { m_footer = tmpl; }

    // 每頁繪製頁首/頁尾並縮減內文區域。drawing=false 時 Qt 只是在量測版面，
    // 此時不可實際繪圖，但仍必須做出「相同的區域縮減」，否則量測與繪製不一致，
    // 內文會與頁首頁尾重疊。
    void formatPage(QPainter &painter, bool drawing, QRect &area, int pagenr) override;

private:
    QString m_filePath;
    QString m_header;
    QString m_footer;
};

}  // namespace macpad::features
