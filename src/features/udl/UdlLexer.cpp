#include "features/udl/UdlLexer.h"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QList>
#include <QVector>

#include <Qsci/qsciscintilla.h>

namespace macpad::features {

UdlLexer::UdlLexer(const UdlDefinition &def, QObject *parent)
    : QsciLexerCustom(parent), m_def(def)
{
    m_langName = def.name.toUtf8();
    applyUserStyles();
}

void UdlLexer::applyUserStyles()
{
    // 僅對 m_def.styles 中有設定的樣式呼叫 setColor/setPaper/setFont；
    // 未設定者不覆寫，QsciLexer 會自動回退至 defaultColor()（向後相容）。
    for (auto it = m_def.styles.constBegin(); it != m_def.styles.constEnd(); ++it) {
        const int styleId = it.key();
        const UdlStyle &st = it.value();
        if (!st.fg.isEmpty())
            setColor(QColor(st.fg), styleId);
        if (!st.bg.isEmpty())
            setPaper(QColor(st.bg), styleId);
        if (st.bold || st.italic || st.underline) {
            QFont f = font(styleId);
            f.setBold(st.bold);
            f.setItalic(st.italic);
            f.setUnderline(st.underline);
            setFont(f, styleId);
        }
    }
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

// 掃描一段有明確結束標記的區塊。開頭/結尾標記固定以 bodyStyle 上色；
// 中間內容若 nesting 非 0 就遞迴 scanToken（讓字串內的數字、註解內的關鍵字等能被辨識），
// 否則整段以 bodyStyle 上色——與加入 nesting 之前的行為完全相同。
int UdlLexer::scanRegion(const QByteArray &utf8, int i, int end, const ScanTables &t,
                         const QByteArray &openTok, const QByteArray &closeTok,
                         const QByteArray &escapeTok, int nesting, int bodyStyle)
{
    const int startPos = i;
    setStyling(openTok.size(), bodyStyle);
    int j = i + openTok.size();

    while (j < utf8.size()) {
        // 跳脫字元優先：\" 之類不得被誤判為結尾
        if (!escapeTok.isEmpty() && utf8.mid(j, escapeTok.size()) == escapeTok) {
            const int skip = qMin(escapeTok.size() + 1, utf8.size() - j);
            setStyling(skip, bodyStyle);
            j += skip;
            continue;
        }
        if (!closeTok.isEmpty() && utf8.mid(j, closeTok.size()) == closeTok)
            break;
        if (nesting != 0) {
            j += scanToken(utf8, j, end, t, nesting, bodyStyle);
        } else {
            setStyling(1, bodyStyle);
            ++j;
        }
    }

    // 結尾標記；未閉合時（掃到檔尾）就只到檔尾為止
    if (j < utf8.size() && !closeTok.isEmpty() && utf8.mid(j, closeTok.size()) == closeTok) {
        const int n = qMin(closeTok.size(), utf8.size() - j);
        setStyling(n, bodyStyle);
        j += n;
    }
    return j - startPos;
}


int UdlLexer::scanToken(const QByteArray &utf8, int i, int end, const ScanTables &t,
                        int mask, int fallbackStyle)
{
    auto isWordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    };
    const char c = utf8.at(i);

    // 行註解：延伸至行尾（結尾標記為換行，不吃掉換行本身）
    if ((mask & UdlNest::LineComment) && !t.lineComment.isEmpty()
        && utf8.mid(i, t.lineComment.size()) == t.lineComment) {
        const int nest = m_def.lineCommentNesting;
        setStyling(t.lineComment.size(), Comment);
        int j = i + t.lineComment.size();
        while (j < utf8.size() && utf8.at(j) != '\n') {
            if (nest != 0) {
                j += scanToken(utf8, j, end, t, nest, Comment);
            } else {
                setStyling(1, Comment);
                ++j;
            }
        }
        return j - i;
    }
    // 區塊註解
    if ((mask & UdlNest::Comment) && !t.blockOpen.isEmpty()
        && utf8.mid(i, t.blockOpen.size()) == t.blockOpen) {
        return scanRegion(utf8, i, end, t, t.blockOpen, t.blockClose, QByteArray(),
                          m_def.blockCommentNesting, Comment);
    }
    // 自訂分隔符區塊（FR-059）；逐組檢查 nesting 遮罩是否允許該組
    for (int d = 0; d < t.delims.size(); ++d) {
        if (!(mask & UdlNest::delimiterBit(qMin(d, 7))))
            continue;
        const auto &dl = t.delims.at(d);
        if (utf8.mid(i, dl.open.size()) != dl.open)
            continue;
        return scanRegion(utf8, i, end, t, dl.open, dl.close, dl.escape, dl.nesting, Delimiter);
    }
    // 內建引號字串
    if ((mask & UdlNest::String) && (c == '"' || c == '\'')) {
        const QByteArray q(1, c);
        return scanRegion(utf8, i, end, t, q, q, QByteArrayLiteral("\\"),
                          m_def.stringNesting, String);
    }
    // 數字
    if ((mask & UdlNest::Number) && c >= '0' && c <= '9') {
        int j = i;
        while (j < utf8.size() &&
               ((utf8.at(j) >= '0' && utf8.at(j) <= '9') || utf8.at(j) == '.')) ++j;
        setStyling(j - i, Number);
        return j - i;
    }
    // 識別字/關鍵字（依 8 組關鍵字分別著色）
    if (isWordChar(c)) {
        int j = i;
        while (j < utf8.size() && isWordChar(utf8.at(j))) ++j;
        const QString word = QString::fromUtf8(utf8.mid(i, j - i));

        int style = fallbackStyle;
        const int groupCount = m_def.keywordGroups.isEmpty()
            ? 1
            : std::min(static_cast<int>(m_def.keywordGroups.size()), kUdlMaxKeywordGroups);
        for (int g = 0; g < groupCount; ++g) {
            if (!(mask & UdlNest::keywordBit(g)))
                continue;   // 此巢狀層級不允許這組關鍵字
            const QSet<QString> &group = m_def.keywordGroup(g);
            bool kw = false;
            if (m_def.keywordGroupPrefix(g)) {
                // 前綴模式（FR-059 擴充）：token 以任一關鍵字為前綴即視為命中
                for (const QString &k : group) {
                    if (k.isEmpty())
                        continue;
                    if (word.startsWith(k, m_def.caseSensitive ? Qt::CaseSensitive
                                                               : Qt::CaseInsensitive)) {
                        kw = true;
                        break;
                    }
                }
            } else {
                kw = m_def.caseSensitive
                    ? group.contains(word)
                    : group.contains(word.toLower()) || group.contains(word);
                if (!m_def.caseSensitive && !kw) {
                    for (const QString &k : group)
                        if (k.compare(word, Qt::CaseInsensitive) == 0) { kw = true; break; }
                }
            }
            if (kw) {
                style = styleForKeywordGroup(g);
                break;
            }
        }
        setStyling(j - i, style);
        return j - i;
    }
    // 運算子（FR-059）
    if (mask & UdlNest::Operator) {
        for (const QByteArray &op : t.operators) {
            if (utf8.mid(i, op.size()) == op) {
                setStyling(op.size(), Operator);
                return op.size();
            }
        }
    }

    setStyling(1, fallbackStyle);
    return 1;
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

    ScanTables tables;
    tables.lineComment = m_def.lineComment.toUtf8();
    tables.blockOpen = m_def.blockCommentStart.toUtf8();
    tables.blockClose = m_def.blockCommentEnd.toUtf8();
    for (const auto &d : m_def.delimiters) {
        if (d.open.isEmpty() || d.close.isEmpty())
            continue;
        tables.delims.push_back({d.open.toUtf8(), d.escape.toUtf8(), d.close.toUtf8(), d.nesting});
    }
    // 運算子依長度遞減排序，確保最長匹配優先（如 "==" 優先於 "="）
    for (const QString &op : m_def.operators)
        if (!op.isEmpty())
            tables.operators << op.toUtf8();
    std::sort(tables.operators.begin(), tables.operators.end(),
              [](const QByteArray &a, const QByteArray &b) { return a.size() > b.size(); });

    // 頂層：所有類別皆可辨識；區塊內部由 scanRegion 依各自的 nesting 遮罩遞迴。
    while (i < end && i < utf8.size())
        i += scanToken(utf8, i, end, tables, UdlNest::All, Default);

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
        const int mids = m_def.folderTokens.middle.isEmpty()
            ? 0 : line.count(m_def.folderTokens.middle);

        // middle（如 else/elseif）視為「先關閉再重新開啟」同一層級：該行本身顯示於
        // 外層層級（depth-1），並標記為 header（其後內容摺疊至此），但淨深度不變。
        int lineDepth = depth;
        if (mids > 0 && depth > 0)
            lineDepth = depth - 1;

        int level = QsciScintillaBase::SC_FOLDLEVELBASE + lineDepth;
        if (opens > closes || mids > 0)
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
