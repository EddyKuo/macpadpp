#include "features/udl/UdlLexer.h"

#include <algorithm>

#include <QColor>
#include <QList>
#include <QVector>

#include <Qsci/qsciscintilla.h>

namespace macpad::features {

UdlLexer::UdlLexer(const UdlDefinition &def, QObject *parent)
    : QsciLexerCustom(parent), m_def(def)
{
    m_langName = def.name.toUtf8();
}

int UdlLexer::styleForKeywordGroup(int groupIdx)
{
    if (groupIdx <= 0)
        return Keyword;
    // 1..7 → Keyword2..Keyword8
    return Keyword2 + (groupIdx - 1);
}

QString UdlLexer::description(int style) const
{
    switch (style) {
    case Default:   return QStringLiteral("Default");
    case Keyword:   return QStringLiteral("Keyword");
    case Comment:   return QStringLiteral("Comment");
    case String:    return QStringLiteral("String");
    case Number:    return QStringLiteral("Number");
    case Keyword2:  return QStringLiteral("Keyword 2");
    case Keyword3:  return QStringLiteral("Keyword 3");
    case Keyword4:  return QStringLiteral("Keyword 4");
    case Keyword5:  return QStringLiteral("Keyword 5");
    case Keyword6:  return QStringLiteral("Keyword 6");
    case Keyword7:  return QStringLiteral("Keyword 7");
    case Keyword8:  return QStringLiteral("Keyword 8");
    case Operator:  return QStringLiteral("Operator");
    case Delimiter: return QStringLiteral("Delimiter");
    }
    return QString();
}

QColor UdlLexer::defaultColor(int style) const
{
    switch (style) {
    case Keyword:   return QColor(0, 0, 255);
    case Comment:   return QColor(0, 128, 0);
    case String:    return QColor(163, 21, 21);
    case Number:    return QColor(128, 0, 128);
    case Keyword2:  return QColor(0, 128, 128);
    case Keyword3:  return QColor(128, 64, 0);
    case Keyword4:  return QColor(0, 100, 0);
    case Keyword5:  return QColor(160, 32, 240);
    case Keyword6:  return QColor(200, 100, 50);
    case Keyword7:  return QColor(70, 70, 200);
    case Keyword8:  return QColor(150, 0, 80);
    case Operator:  return QColor(90, 90, 90);
    case Delimiter: return QColor(200, 0, 0);
    default:        return QColor(0, 0, 0);
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

    // 分隔符（成對區塊，含可選跳脫字元）的 UTF-8 快取
    struct DelimBytes { QByteArray open, escape, close; };
    QVector<DelimBytes> delims;
    for (const auto &d : m_def.delimiters) {
        if (d.open.isEmpty() || d.close.isEmpty())
            continue;
        delims.push_back({d.open.toUtf8(), d.escape.toUtf8(), d.close.toUtf8()});
    }

    // 運算子依長度遞減排序，確保最長匹配優先（如 "==" 優先於 "="）
    QList<QByteArray> ops;
    for (const QString &op : m_def.operators)
        if (!op.isEmpty())
            ops << op.toUtf8();
    std::sort(ops.begin(), ops.end(), [](const QByteArray &a, const QByteArray &b) {
        return a.size() > b.size();
    });

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
        // 自訂分隔符區塊（FR-059）
        {
            bool matched = false;
            for (const auto &d : delims) {
                if (utf8.mid(i, d.open.size()) != d.open)
                    continue;
                int j = i + d.open.size();
                while (j < utf8.size() && utf8.mid(j, d.close.size()) != d.close) {
                    if (!d.escape.isEmpty() && utf8.mid(j, d.escape.size()) == d.escape)
                        j += d.escape.size();
                    ++j;
                }
                j = qMin(end, j + d.close.size());
                setStyling(j - i, Delimiter);
                i = j;
                matched = true;
                break;
            }
            if (matched)
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
        // 識別字/關鍵字（依 8 組關鍵字分別著色）
        if (isWordChar(c)) {
            int j = i;
            while (j < end && j < utf8.size() && isWordChar(utf8.at(j))) ++j;
            QString word = QString::fromUtf8(utf8.mid(i, j - i));

            int style = Default;
            const int groupCount = m_def.keywordGroups.isEmpty()
                ? 1
                : std::min(static_cast<int>(m_def.keywordGroups.size()), kUdlMaxKeywordGroups);
            for (int g = 0; g < groupCount; ++g) {
                const QSet<QString> &group = m_def.keywordGroup(g);
                bool kw = m_def.caseSensitive
                    ? group.contains(word)
                    : group.contains(word.toLower()) || group.contains(word);
                if (!m_def.caseSensitive && !kw) {
                    for (const QString &k : group)
                        if (k.compare(word, Qt::CaseInsensitive) == 0) { kw = true; break; }
                }
                if (kw) {
                    style = styleForKeywordGroup(g);
                    break;
                }
            }
            setStyling(j - i, style);
            i = j;
            continue;
        }
        // 運算子（FR-059）
        {
            bool matched = false;
            for (const QByteArray &op : ops) {
                if (utf8.mid(i, op.size()) == op) {
                    setStyling(op.size(), Operator);
                    i += op.size();
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;
        }
        setStyling(1, Default);
        ++i;
    }

    applyFolding(text);
}

void UdlLexer::applyFolding(const QString &text)
{
    if (m_def.folderTokens.isEmpty())
        return;

    const QStringList lines = text.split(QLatin1Char('\n'));
    int depth = 0;
    for (int ln = 0; ln < lines.size(); ++ln) {
        const QString &line = lines.at(ln);
        const int opens = m_def.folderTokens.open.isEmpty()
            ? 0 : line.count(m_def.folderTokens.open);
        const int closes = m_def.folderTokens.close.isEmpty()
            ? 0 : line.count(m_def.folderTokens.close);

        int level = QsciScintillaBase::SC_FOLDLEVELBASE + depth;
        if (opens > closes)
            level |= QsciScintillaBase::SC_FOLDLEVELHEADERFLAG;
        editor()->SendScintilla(QsciScintillaBase::SCI_SETFOLDLEVEL,
                                 static_cast<unsigned long>(ln),
                                 static_cast<long>(level));

        depth += (opens - closes);
        if (depth < 0)
            depth = 0;
    }
}

}  // namespace macpad::features
