#include "features/udl/UdlLexer.h"

#include <QColor>

#include <Qsci/qsciscintilla.h>

namespace macpad::features {

UdlLexer::UdlLexer(const UdlDefinition &def, QObject *parent)
    : QsciLexerCustom(parent), m_def(def)
{
    m_langName = def.name.toUtf8();
}

QString UdlLexer::description(int style) const
{
    switch (style) {
    case Default: return QStringLiteral("Default");
    case Keyword: return QStringLiteral("Keyword");
    case Comment: return QStringLiteral("Comment");
    case String:  return QStringLiteral("String");
    case Number:  return QStringLiteral("Number");
    }
    return QString();
}

QColor UdlLexer::defaultColor(int style) const
{
    switch (style) {
    case Keyword: return QColor(0, 0, 255);
    case Comment: return QColor(0, 128, 0);
    case String:  return QColor(163, 21, 21);
    case Number:  return QColor(128, 0, 128);
    default:      return QColor(0, 0, 0);
    }
}

void UdlLexer::styleText(int start, int end)
{
    if (!editor())
        return;

    QString text = editor()->text();
    // 以位元組計 QScintilla；此處以 UTF-8 索引近似。
    // 區塊註解等具有跨行狀態，QScintilla 傳入的 start..end 僅涵蓋編輯處，
    // 無法得知 start 之前是否仍處於未結束的區塊註解中，故一律自文件開頭重新掃描。
    const QByteArray utf8 = text.toUtf8();
    start = 0;
    end = utf8.size();
    int i = start;
    startStyling(start);

    auto isWordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };

    const QByteArray lc = m_def.lineComment.toUtf8();
    const QByteArray bcs = m_def.blockCommentStart.toUtf8();
    const QByteArray bce = m_def.blockCommentEnd.toUtf8();

    while (i < end && i < utf8.size()) {
        const char c = utf8.at(i);

        // 行註解
        if (!lc.isEmpty() && utf8.mid(i, lc.size()) == lc) {
            int j = i;
            while (j < end && j < utf8.size() && utf8.at(j) != '\n') ++j;
            setStyling(j - i, Comment);
            i = j;
            continue;
        }
        // 區塊註解
        if (!bcs.isEmpty() && utf8.mid(i, bcs.size()) == bcs) {
            int j = i + bcs.size();
            while (j < utf8.size() && utf8.mid(j, bce.size()) != bce) ++j;
            j = qMin(end, j + bce.size());
            setStyling(j - i, Comment);
            i = j;
            continue;
        }
        // 字串
        if (c == '"' || c == '\'') {
            const char q = c;
            int j = i + 1;
            while (j < end && j < utf8.size() && utf8.at(j) != q) {
                // 跳脫字元：\" 等不視為字串結尾
                if (utf8.at(j) == '\\' && j + 1 < utf8.size()) ++j;
                ++j;
            }
            j = qMin(end, j + 1);
            setStyling(j - i, String);
            i = j;
            continue;
        }
        // 數字
        if (c >= '0' && c <= '9') {
            int j = i;
            while (j < end && j < utf8.size() &&
                   ((utf8.at(j) >= '0' && utf8.at(j) <= '9') || utf8.at(j) == '.')) ++j;
            setStyling(j - i, Number);
            i = j;
            continue;
        }
        // 識別字/關鍵字
        if (isWordChar(c)) {
            int j = i;
            while (j < end && j < utf8.size() && isWordChar(utf8.at(j))) ++j;
            QString word = QString::fromUtf8(utf8.mid(i, j - i));
            bool kw = m_def.caseSensitive
                ? m_def.keywords.contains(word)
                : m_def.keywords.contains(word.toLower()) || m_def.keywords.contains(word);
            if (!m_def.caseSensitive && !kw) {
                for (const QString &k : m_def.keywords)
                    if (k.compare(word, Qt::CaseInsensitive) == 0) { kw = true; break; }
            }
            setStyling(j - i, kw ? Keyword : Default);
            i = j;
            continue;
        }
        setStyling(1, Default);
        ++i;
    }
}

}  // namespace macpad::features
