#include "features/print/DocumentPrinter.h"

#include "features/print/PrintFormatter.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>

namespace macpad::features {

void DocumentPrinter::formatPage(QPainter &painter, bool drawing, QRect &area, int pagenr)
{
    if (m_header.isEmpty() && m_footer.isEmpty())
        return;   // 沒設定就完全不介入版面，維持原本行為

    PrintContext ctx;
    ctx.filePath = m_filePath;
    const QDateTime now = QDateTime::currentDateTime();
    ctx.dateText = now.toString(Qt::ISODate).left(10);
    ctx.timeText = now.toString(QStringLiteral("HH:mm"));
    ctx.pageNumber = pagenr;
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

}  // namespace macpad::features
