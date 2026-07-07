#include "features/textops/TextOps.h"

#include <QSet>
#include <QStringList>
#include <algorithm>

namespace macpad::features {

// 以 \n 切行但保留「行數」語意；操作後以 \n 重組（EOL 正規化由編輯器負責）
static QStringList splitLines(const QString &s) { return s.split(QLatin1Char('\n')); }
static QString joinBack(const QStringList &lines) { return lines.join(QLatin1Char('\n')); }

QString TextOps::toUpper(const QString &s) { return s.toUpper(); }
QString TextOps::toLower(const QString &s) { return s.toLower(); }

QString TextOps::toTitleCase(const QString &s)
{
    QString out = s;
    bool startOfWord = true;
    for (QChar &c : out) {
        if (c.isLetter()) {
            c = startOfWord ? c.toUpper() : c.toLower();
            startOfWord = false;
        } else {
            startOfWord = true;
        }
    }
    return out;
}

QString TextOps::toSentenceCase(const QString &s)
{
    QString out = s;
    bool startOfSentence = true;
    for (QChar &c : out) {
        if (c.isLetter()) {
            c = startOfSentence ? c.toUpper() : c.toLower();
            startOfSentence = false;
        } else if (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')) {
            startOfSentence = true;
        }
    }
    return out;
}

QString TextOps::invertCase(const QString &s)
{
    QString out = s;
    for (QChar &c : out)
        c = c.isUpper() ? c.toLower() : c.toUpper();
    return out;
}

QString TextOps::sortLinesAscending(const QString &s, bool caseSensitive)
{
    QStringList lines = splitLines(s);
    std::stable_sort(lines.begin(), lines.end(), [caseSensitive](const QString &a, const QString &b) {
        return a.compare(b, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) < 0;
    });
    return joinBack(lines);
}

QString TextOps::sortLinesDescending(const QString &s, bool caseSensitive)
{
    QStringList lines = splitLines(s);
    std::stable_sort(lines.begin(), lines.end(), [caseSensitive](const QString &a, const QString &b) {
        return a.compare(b, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) > 0;
    });
    return joinBack(lines);
}

QString TextOps::sortLinesNumeric(const QString &s, bool ascending)
{
    QStringList lines = splitLines(s);
    std::stable_sort(lines.begin(), lines.end(), [ascending](const QString &a, const QString &b) {
        const double da = a.trimmed().toDouble();
        const double db = b.trimmed().toDouble();
        return ascending ? da < db : da > db;
    });
    return joinBack(lines);
}

QString TextOps::removeDuplicateLines(const QString &s)
{
    QStringList lines = splitLines(s);
    QStringList out;
    QSet<QString> seen;
    for (const QString &l : lines) {
        if (!seen.contains(l)) {
            seen.insert(l);
            out << l;
        }
    }
    return joinBack(out);
}

QString TextOps::removeEmptyLines(const QString &s, bool blankMeansWhitespace)
{
    QStringList lines = splitLines(s);
    QStringList out;
    for (const QString &l : lines) {
        const bool empty = blankMeansWhitespace ? l.trimmed().isEmpty() : l.isEmpty();
        if (!empty)
            out << l;
    }
    return joinBack(out);
}

QString TextOps::reverseLines(const QString &s)
{
    QStringList lines = splitLines(s);
    std::reverse(lines.begin(), lines.end());
    return joinBack(lines);
}

QString TextOps::joinLines(const QString &s, const QString &sep)
{
    return splitLines(s).join(sep);
}

QString TextOps::duplicateLines(const QString &s)
{
    QStringList lines = splitLines(s);
    QStringList out;
    for (const QString &l : lines) {
        out << l;
        out << l;
    }
    return joinBack(out);
}

QString TextOps::moveLinesUp(const QString &s, int firstLine, int lastLine, int *newFirst)
{
    QStringList lines = splitLines(s);
    if (firstLine <= 0 || firstLine > lastLine || lastLine >= lines.size()) {
        if (newFirst) *newFirst = firstLine;
        return s;
    }
    const QString prev = lines.takeAt(firstLine - 1);
    lines.insert(lastLine, prev);  // 前一行移到區塊之後
    if (newFirst) *newFirst = firstLine - 1;
    return joinBack(lines);
}

QString TextOps::moveLinesDown(const QString &s, int firstLine, int lastLine, int *newFirst)
{
    QStringList lines = splitLines(s);
    if (firstLine < 0 || firstLine > lastLine || lastLine >= lines.size() - 1) {
        if (newFirst) *newFirst = firstLine;
        return s;
    }
    const QString next = lines.takeAt(lastLine + 1);
    lines.insert(firstLine, next);  // 後一行移到區塊之前
    if (newFirst) *newFirst = firstLine + 1;
    return joinBack(lines);
}

QString TextOps::trimTrailing(const QString &s)
{
    QStringList lines = splitLines(s);
    for (QString &l : lines) {
        int end = l.size();
        while (end > 0 && (l[end - 1] == QLatin1Char(' ') || l[end - 1] == QLatin1Char('\t')))
            --end;
        l.truncate(end);
    }
    return joinBack(lines);
}

QString TextOps::trimLeading(const QString &s)
{
    QStringList lines = splitLines(s);
    for (QString &l : lines) {
        int start = 0;
        while (start < l.size() && (l[start] == QLatin1Char(' ') || l[start] == QLatin1Char('\t')))
            ++start;
        l = l.mid(start);
    }
    return joinBack(lines);
}

QString TextOps::tabsToSpaces(const QString &s, int tabWidth)
{
    if (tabWidth < 1) tabWidth = 4;
    QString out;
    int col = 0;
    for (const QChar c : s) {
        if (c == QLatin1Char('\t')) {
            const int n = tabWidth - (col % tabWidth);
            out += QString(n, QLatin1Char(' '));
            col += n;
        } else if (c == QLatin1Char('\n')) {
            out += c;
            col = 0;
        } else {
            out += c;
            ++col;
        }
    }
    return out;
}

QString TextOps::spacesToTabs(const QString &s, int tabWidth)
{
    if (tabWidth < 1) tabWidth = 4;
    QStringList lines = splitLines(s);
    for (QString &line : lines) {
        QString out;
        int col = 0;
        int i = 0;
        while (i < line.size()) {
            const QChar c = line[i];
            if (c == QLatin1Char(' ')) {
                int j = i;
                while (j < line.size() && line[j] == QLatin1Char(' ')) ++j;
                const int runEnd = col + (j - i);
                int cur = col;
                while (cur < runEnd) {
                    const int nextStop = ((cur / tabWidth) + 1) * tabWidth;
                    if (nextStop <= runEnd) {   // 整段跨越 tab 停駐點 → 折成 tab
                        out += QLatin1Char('\t');
                        cur = nextStop;
                    } else {
                        out += QString(runEnd - cur, QLatin1Char(' '));
                        cur = runEnd;
                    }
                }
                col = runEnd;
                i = j;
            } else if (c == QLatin1Char('\t')) {
                out += QLatin1Char('\t');
                col = ((col / tabWidth) + 1) * tabWidth;
                ++i;
            } else {
                out += c;
                ++col;
                ++i;
            }
        }
        line = out;
    }
    return joinBack(lines);
}

QString TextOps::toggleLineComment(const QString &s, const QString &marker)
{
    if (marker.isEmpty())
        return s;
    QStringList lines = splitLines(s);

    // 判斷是否全部非空行皆已註解
    bool allCommented = true;
    bool anyContent = false;
    for (const QString &l : lines) {
        if (l.trimmed().isEmpty())
            continue;
        anyContent = true;
        if (!l.trimmed().startsWith(marker)) {
            allCommented = false;
            break;
        }
    }
    if (!anyContent)
        return s;

    for (QString &l : lines) {
        if (l.trimmed().isEmpty())
            continue;
        if (allCommented) {
            // 取消註解：移除第一個 marker（含其後一個空白，若有）
            const int idx = l.indexOf(marker);
            if (idx >= 0) {
                int removeLen = marker.size();
                if (idx + removeLen < l.size() && l[idx + removeLen] == QLatin1Char(' '))
                    ++removeLen;
                l.remove(idx, removeLen);
            }
        } else {
            // 加註解：於行首縮排後插入 "marker "
            int indent = 0;
            while (indent < l.size() &&
                   (l[indent] == QLatin1Char(' ') || l[indent] == QLatin1Char('\t')))
                ++indent;
            l.insert(indent, marker + QLatin1Char(' '));
        }
    }
    return joinBack(lines);
}

}  // namespace macpad::features
