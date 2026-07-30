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

    // 把 FormFeed（\f, U+000C）當成分頁符列印（複刻 Notepad++ v8.9.7）。
    // QsciPrinter::printRange 對整份文件分頁，遇到 \f 不會換頁；此處改為依 \f 切段，
    // 共用同一個 QPainter 逐段列印，段與段之間手動 newPage()。
    // 回傳值語意同 QsciPrinter::printRange（非 0 表示成功）。
    // 文件中沒有 \f 時等同單段列印，輸出與原本一致。
    int printWithFormFeeds(QsciScintillaBase *sci);

    // 列印整份文件的統一入口：需要時先跑一趟「試排」求出總頁數（供 $(NB_PAGES) 展開），
    // 再依 formFeeds 決定走 printWithFormFeeds 或 printRange。
    // 只有在頁首/頁尾真的用到 $(NB_PAGES) 時才會試排，否則不付出這個成本。
    int printDocument(QsciScintillaBase *sci, bool formFeeds);

    // 頁首/頁尾樣板是否用到 $(NB_PAGES)（決定要不要試排）。公開以利單元測試。
    bool needsPageCount() const;

private:
    // 試排：把同樣的版面設定輸出到暫存 PDF，藉 formatPage 記錄最大頁碼求得總頁數。
    // 以相同的 resolution 與 pageLayout 進行，讓分頁結果與實際列印一致。
    int computePageCount(QsciScintillaBase *sci, bool formFeeds);

    QString m_filePath;
    QString m_header;
    QString m_footer;
    // 逐段列印時的頁碼偏移：printRange 每次呼叫都從第 1 頁起算，
    // 加上偏移後 $(CURRENT_PAGE) 才是整份文件的連續頁碼。
    int m_pageOffset = 0;
    int m_pagesInSegment = 0;   // 本段已輸出的頁數（由 formatPage 更新）
    int m_totalPages = 0;       // 上一次列印實際輸出的總頁數（供試排取用）
    int m_pageCount = 0;        // 整份文件總頁數；0 = 未知（$(NB_PAGES) 展開為空）
    bool m_counting = false;    // 目前是否為試排（避免試排中再次試排而遞迴）
};

}  // namespace macpad::features
