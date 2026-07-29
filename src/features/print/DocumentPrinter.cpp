#include "features/print/DocumentPrinter.h"

#include "features/print/PrintFormatter.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>

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
    // 總頁數：QsciPrinter 逐頁輸出時無法預先得知，維持 0 讓 $(NB_PAGES) 展開為空，
    // 不假造數字（見 PrintFormatter 的說明）。
    ctx.pageCount = 0;

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
    if (breakAfterLine.isEmpty())
        return printRange(sci);   // 沒有 \f → 與原本行為完全相同

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

    m_pageOffset = 0;
    return ok;
}

}  // namespace macpad::features
