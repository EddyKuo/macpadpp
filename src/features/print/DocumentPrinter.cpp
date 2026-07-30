#include "features/print/DocumentPrinter.h"

#include "features/print/PrintFormatter.h"

#include <QDateTime>
#include <QDir>
#include <QFontMetrics>
#include <QPainter>
#include <QTemporaryFile>

namespace macpad::features {

void DocumentPrinter::formatPage(QPainter &painter, bool drawing, QRect &area, int pagenr)
{
    // 記錄本段用掉的頁數（printWithFormFeeds 用以維持連續頁碼）。
    // 必須在 header/footer 的早期 return 之前，否則沒設頁首頁尾時就統計不到。
    if (drawing && pagenr > m_pagesInSegment)
        m_pagesInSegment = pagenr;

    if (m_header.isEmpty() && m_footer.isEmpty())
        return;   // 沒設定就完全不介入版面，維持原本行為

    PrintContext ctx;
    ctx.filePath = m_filePath;
    const QDateTime now = QDateTime::currentDateTime();
    ctx.dateText = now.toString(Qt::ISODate).left(10);
    ctx.timeText = now.toString(QStringLiteral("HH:mm"));
    ctx.pageNumber = pagenr + m_pageOffset;   // 逐段列印時累加為整份文件的連續頁碼
    // 總頁數由 printDocument() 事前試排求得；未試排（或試排失敗）時維持 0，
    // 讓 $(NB_PAGES) 展開為空字串而非假造數字（見 PrintFormatter 的說明）。
    ctx.pageCount = m_pageCount;

    const QFontMetrics fm(painter.font());
    const int lineHeight = fm.height();
    const int gap = lineHeight / 2;   // 頁首/頁尾與內文之間的留白

    if (!m_header.isEmpty()) {
        const QString text = PrintFormatter::expand(m_header, ctx);
        if (drawing) {
            painter.drawText(area.left(), area.top() + fm.ascent(), text);
            // 分隔線讓頁首與內文在視覺上分開
            const int y = area.top() + lineHeight;
            painter.drawLine(area.left(), y, area.right(), y);
        }
        // 量測與繪製都必須縮減區域，否則兩趟版面不一致、內文會壓到頁首上
        area.setTop(area.top() + lineHeight + gap);
    }

    if (!m_footer.isEmpty()) {
        const QString text = PrintFormatter::expand(m_footer, ctx);
        if (drawing) {
            const int y = area.bottom() - lineHeight;
            painter.drawLine(area.left(), y, area.right(), y);
            painter.drawText(area.left(), area.bottom() - fm.descent(), text);
        }
        area.setBottom(area.bottom() - lineHeight - gap);
    }
}


bool DocumentPrinter::needsPageCount() const
{
    static const QString kToken = QStringLiteral("$(NB_PAGES)");
    return m_header.contains(kToken) || m_footer.contains(kToken);
}


int DocumentPrinter::computePageCount(QsciScintillaBase *sci, bool formFeeds)
{
    if (!sci)
        return 0;

    // 輸出到暫存 PDF：不佔用實體印表機，也不會吐紙。
    // 注意必須 close()：QTemporaryFile 會持有檔案控制代碼，Windows 上未關閉時
    // QPrinter 的 PDF 引擎再開同一路徑會踩到共用衝突而失敗，導致頁數永遠算不出來。
    // 這裡保留物件本身只是為了佔住唯一檔名並在解構時自動刪除。
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("macpad-pagecount-XXXXXX.pdf")));
    tmp.setAutoRemove(true);
    if (!tmp.open())
        return 0;   // 求不到就回 0，$(NB_PAGES) 展開為空，不影響列印本身
    const QString tmpPath = tmp.fileName();
    tmp.close();

    // 試排的印表機必須與本體同樣式，否則分頁位置不同、算出的總頁數就不對。
    // 切換 outputFormat 會重設 resolution 為新引擎的預設 DPI，故務必先切格式、再套版面，
    // 否則試排會以不同 DPI 分頁，算出的頁數與實際列印不符。
    DocumentPrinter dry;
    dry.m_counting = true;
    dry.m_filePath = m_filePath;
    dry.m_header = m_header;
    dry.m_footer = m_footer;
    dry.setOutputFormat(QPrinter::PdfFormat);
    dry.setOutputFileName(tmpPath);
    dry.setMagnification(magnification());
    dry.setWrapMode(wrapMode());
    dry.setResolution(resolution());
    dry.setPageLayout(pageLayout());

    // 走 printDocument 讓 m_counting 守衛真正生效（試排中不得再觸發試排）
    const int ok = dry.printDocument(sci, formFeeds);
    return ok == 0 ? 0 : dry.m_totalPages;
}


int DocumentPrinter::printDocument(QsciScintillaBase *sci, bool formFeeds)
{
    if (!sci)
        return 0;

    // 只有頁首/頁尾真的引用 $(NB_PAGES) 才付出試排成本（整份文件會被多算繪一次）；
    // 試排本身 m_counting 為 true，故不會再次觸發試排而遞迴。
    m_pageCount = (!m_counting && needsPageCount()) ? computePageCount(sci, formFeeds) : 0;

    if (formFeeds)
        return printWithFormFeeds(sci);   // 內部會把各段頁數加總寫入 m_totalPages

    m_pagesInSegment = 0;
    const int ok = printRange(sci);
    m_totalPages = m_pagesInSegment;      // 單段列印：formatPage 記下的最大頁碼即總頁數
    return ok;
}


int DocumentPrinter::printWithFormFeeds(QsciScintillaBase *sci)
{
    if (!sci)
        return 0;

    // 找出所有含 FormFeed 的行；每個 \f 之後的內容從新的一頁開始。
    const int lineCount =
        static_cast<int>(sci->SendScintilla(QsciScintillaBase::SCI_GETLINECOUNT));
    QVector<int> breakAfterLine;   // 這些行印完之後換頁
    for (int line = 0; line < lineCount; ++line) {
        const long start = sci->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
        const long end = sci->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line);
        bool hasFf = false;
        for (long p = start; p < end; ++p) {
            if (static_cast<char>(sci->SendScintilla(QsciScintillaBase::SCI_GETCHARAT,
                                                     static_cast<unsigned long>(p))) == '\f') {
                hasFf = true;
                break;
            }
        }
        if (hasFf)
            breakAfterLine.push_back(line);
    }

    m_pageOffset = 0;
    if (breakAfterLine.isEmpty()) {
        m_pagesInSegment = 0;
        const int single = printRange(sci);   // 沒有 \f → 與原本行為完全相同
        m_totalPages = m_pagesInSegment;
        return single;
    }

    // 共用同一個 QPainter 逐段列印：QsciPrinter::printRange 的每一段都從自己的第 1 頁開始，
    // 不會自動換頁，因此段與段之間由此處手動 newPage()。
    QPainter painter(this);
    int fromLine = 0;
    bool first = true;
    int ok = 1;
    // 每段列印完，把該段用掉的頁數累加到偏移量，讓 $(CURRENT_PAGE) 連續。
    // printRange 不回報頁數，故由 formatPage 記錄本段出現過的最大頁碼。
    auto printSegment = [&](int from, int to) {
        if (!first && !newPage()) {
            ok = 0;
            return false;
        }
        first = false;
        m_pagesInSegment = 0;
        ok = printRange(sci, painter, from, to);
        m_pageOffset += m_pagesInSegment;
        return ok != 0;
    };

    for (int b : breakAfterLine) {
        if (!printSegment(fromLine, b))
            return 0;
        fromLine = b + 1;
    }
    if (fromLine < lineCount && !printSegment(fromLine, lineCount - 1))
        return 0;

    m_totalPages = m_pageOffset;   // 各段頁數累加後即整份文件總頁數（歸零前先留存）
    m_pageOffset = 0;
    return ok;
}

}  // namespace macpad::features
