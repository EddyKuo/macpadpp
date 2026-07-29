#include "features/print/PrintFormatter.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace macpad::features {

QString PrintFormatter::expand(const QString &tmpl, const PrintContext &ctx)
{
    if (tmpl.isEmpty())
        return {};

    const QFileInfo info(ctx.filePath);
    const bool named = !ctx.filePath.isEmpty();

    const QHash<QString, QString> vars = {
        {QStringLiteral("FULL_CURRENT_PATH"), ctx.filePath},
        {QStringLiteral("CURRENT_DIRECTORY"), named ? info.absolutePath() : QString()},
        {QStringLiteral("FILE_NAME"),         named ? info.fileName() : QString()},
        {QStringLiteral("NAME_PART"),         named ? info.completeBaseName() : QString()},
        {QStringLiteral("EXT_PART"),          named ? info.suffix() : QString()},
        {QStringLiteral("CURRENT_DATE"),      ctx.dateText},
        {QStringLiteral("CURRENT_TIME"),      ctx.timeText},
        {QStringLiteral("CURRENT_PAGE"),      QString::number(ctx.pageNumber)},
        // 總頁數未知時展開為空字串，而非印出 0 或 "?"——寧可少一段文字，
        // 也不要在紙上留下一個看起來像真值的假數字。
        {QStringLiteral("NB_PAGES"),          ctx.pageCount > 0
                                                  ? QString::number(ctx.pageCount) : QString()},
    };

    // 一次掃描完成替換，避免「替換後的內容又被當成變數再替換」（例如檔名剛好含 $(...)）。
    static const QRegularExpression re(QStringLiteral(R"(\$\(([A-Z_]+)\))"));
    QString out;
    out.reserve(tmpl.size());
    int last = 0;
    auto it = re.globalMatch(tmpl);
    while (it.hasNext()) {
        const auto m = it.next();
        out += tmpl.mid(last, m.capturedStart() - last);
        const QString key = m.captured(1);
        // 未知變數原樣保留，讓使用者看得出是自己打錯，而非以為樣板生效了
        out += vars.contains(key) ? vars.value(key) : m.captured(0);
        last = m.capturedEnd();
    }
    out += tmpl.mid(last);
    return out;
}

}  // namespace macpad::features
