#include "features/udl/UdlXmlIo.h"

#include "features/udl/UdlDefinition.h"

#include <QColor>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace macpad::features {

namespace {

// 解析「代碼前綴」編碼字串（Notepad++ Comments/Delimiters 關鍵字清單格式）：
// 依序尋找兩位數代碼（00 ~ maxCode-1），代碼後直到下一個代碼（或字串結尾）前的文字
// 即為該代碼對應的值。無分隔符、寬容解析（若值本身含相鄰兩位數字可能誤判，屬已知限制）。
QMap<int, QString> decodeCodedKeywords(const QString &text, int maxCode)
{
    QMap<int, QString> out;
    int curCode = -1;
    int valueStart = -1;
    int i = 0;
    const int n = text.size();
    auto codeAt = [&](int pos) -> int {
        if (pos + 1 >= n)
            return -1;
        const QChar c0 = text.at(pos);
        const QChar c1 = text.at(pos + 1);
        if (!c0.isDigit() || !c1.isDigit())
            return -1;
        const int v = c0.digitValue() * 10 + c1.digitValue();
        if (v >= maxCode)
            return -1;
        return v;
    };
    while (i < n) {
        const int code = codeAt(i);
        if (code >= 0) {
            if (curCode >= 0)
                out.insert(curCode, text.mid(valueStart, i - valueStart));
            curCode = code;
            i += 2;
            valueStart = i;
        } else {
            ++i;
        }
    }
    if (curCode >= 0)
        out.insert(curCode, text.mid(valueStart));
    return out;
}

// 反向編碼：依代碼由小到大串接「兩位數代碼 + 值」，空值的代碼略過。
QString encodeCodedKeywords(const QMap<int, QString> &values)
{
    QString out;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        out += QStringLiteral("%1").arg(it.key(), 2, 10, QLatin1Char('0'));
        out += it.value();
    }
    return out;
}

// Notepad++ 色碼寬容解析：純數字視為十進位 COLORREF（0x00BBGGRR，低位元組為 R），
// 否則視為十六進位 "RRGGBB"（可含前導 '#'）。皆失敗回傳無效 QColor。
QColor parseNppColor(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return QColor();

    bool allDigits = true;
    for (const QChar &c : s) {
        if (!c.isDigit()) { allDigits = false; break; }
    }
    if (allDigits) {
        bool ok = false;
        const quint32 v = s.toUInt(&ok);
        if (!ok)
            return QColor();
        const int r = v & 0xFF;
        const int g = (v >> 8) & 0xFF;
        const int b = (v >> 16) & 0xFF;
        return QColor(r, g, b);
    }

    QString hex = s;
    if (hex.startsWith(QLatin1Char('#')))
        hex.remove(0, 1);
    if (hex.size() != 6)
        return QColor();
    bool ok = false;
    const quint32 v = hex.toUInt(&ok, 16);
    if (!ok)
        return QColor();
    const int r = (v >> 16) & 0xFF;
    const int g = (v >> 8) & 0xFF;
    const int b = v & 0xFF;
    return QColor(r, g, b);
}

QString writeNppColorHex(const QColor &c)
{
    if (!c.isValid())
        return QString();
    return QStringLiteral("%1%2%3")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

constexpr int kFontStyleBold = 1;
constexpr int kFontStyleItalic = 2;
constexpr int kFontStyleUnderline = 4;

// 最多 8 組分隔符，每組佔 3 個依序代碼（open/escape/close），對齊 UdlDefinition::delimiters。
constexpr int kMaxDelimiterSets = 8;

}  // namespace

UdlDefinition UdlXmlIo::importFromXml(const QString &path, const QString &langName)
{
    UdlDefinition result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;

    QXmlStreamReader xml(&file);
    UdlDefinition def;
    bool inTargetUserLang = false;
    bool found = false;
    QString currentKeywordsName;
    QMap<QString, QString> keywordLists;   // name -> 內文
    QMap<int, UdlStyle> styleMap;

    while (!xml.atEnd() && !found) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString elem = xml.name().toString();
            if (elem == QLatin1String("UserLang")) {
                const QString name = xml.attributes().value(QStringLiteral("name")).toString();
                if (langName.isEmpty() || name == langName) {
                    inTargetUserLang = true;
                    def.name = name;
                    const QString ext = xml.attributes().value(QStringLiteral("ext")).toString();
                    for (const QString &e : ext.split(QRegularExpression(QStringLiteral("\\s+")),
                                                        Qt::SkipEmptyParts))
                        def.extensions << e.toLower();
                } else {
                    inTargetUserLang = false;
                }
            } else if (inTargetUserLang && elem == QLatin1String("Global")) {
                const QString caseIgnored =
                    xml.attributes().value(QStringLiteral("caseIgnored")).toString();
                def.caseSensitive = (caseIgnored.compare(QStringLiteral("yes"), Qt::CaseInsensitive) != 0);
            } else if (inTargetUserLang && elem == QLatin1String("Keywords")) {
                currentKeywordsName = xml.attributes().value(QStringLiteral("name")).toString();
                keywordLists.insert(currentKeywordsName, xml.readElementText());
            } else if (inTargetUserLang && elem == QLatin1String("WordsStyle")) {
                bool ok = false;
                const int styleId = xml.attributes().value(QStringLiteral("styleID")).toInt(&ok);
                if (ok) {
                    UdlStyle st;
                    const QColor fg = parseNppColor(
                        xml.attributes().value(QStringLiteral("fgColor")).toString());
                    const QColor bg = parseNppColor(
                        xml.attributes().value(QStringLiteral("bgColor")).toString());
                    st.fg = fg.isValid() ? fg.name() : QString();
                    st.bg = bg.isValid() ? bg.name() : QString();
                    const int fontStyle =
                        xml.attributes().value(QStringLiteral("fontStyle")).toInt();
                    st.bold = (fontStyle & kFontStyleBold) != 0;
                    st.italic = (fontStyle & kFontStyleItalic) != 0;
                    st.underline = (fontStyle & kFontStyleUnderline) != 0;
                    styleMap.insert(styleId, st);
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("UserLang") && inTargetUserLang) {
                found = true;  // 找到目標 UserLang 並讀完，結束
            }
        }
    }
    if (xml.hasError() && !found)
        return result;
    if (def.name.isEmpty())
        return result;

    static const QRegularExpression sep(QStringLiteral("\\s+"));

    // 8 組關鍵字（Keywords1..8）
    def.keywordGroups.resize(kUdlMaxKeywordGroups);
    def.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
    for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
        const QString txt = keywordLists.value(QStringLiteral("Keywords%1").arg(i + 1));
        for (const QString &kw : txt.split(sep, Qt::SkipEmptyParts))
            def.keywordGroups[i].insert(kw);
    }
    def.keywords = def.keywordGroups.at(0);

    // 運算子（Operators1）
    const QString opsTxt = keywordLists.value(QStringLiteral("Operators1"));
    for (const QString &op : opsTxt.split(sep, Qt::SkipEmptyParts))
        def.operators.insert(op);

    // 註解：Comments 清單，代碼 00=行註解 01=區塊起 02=區塊結尾
    if (keywordLists.contains(QStringLiteral("Comments"))) {
        const auto coded = decodeCodedKeywords(keywordLists.value(QStringLiteral("Comments")), 3);
        def.lineComment = coded.value(0).trimmed();
        def.blockCommentStart = coded.value(1).trimmed();
        def.blockCommentEnd = coded.value(2).trimmed();
    }

    // 摺疊符（Folders in code1, open/middle/close）
    def.folderTokens.open = keywordLists.value(QStringLiteral("Folders in code1, open")).trimmed();
    def.folderTokens.middle =
        keywordLists.value(QStringLiteral("Folders in code1, middle")).trimmed();
    def.folderTokens.close = keywordLists.value(QStringLiteral("Folders in code1, close")).trimmed();

    // 分隔符：Delimiters 清單，每組 3 個依序代碼（open/escape/close）
    if (keywordLists.contains(QStringLiteral("Delimiters"))) {
        const auto coded = decodeCodedKeywords(keywordLists.value(QStringLiteral("Delimiters")),
                                                kMaxDelimiterSets * 3);
        for (int i = 0; i < kMaxDelimiterSets; ++i) {
            const QString open = coded.value(i * 3);
            const QString escape = coded.value(i * 3 + 1);
            const QString close = coded.value(i * 3 + 2);
            if (open.isEmpty() && escape.isEmpty() && close.isEmpty())
                continue;
            UdlDelimiter del;
            del.open = open;
            del.escape = escape;
            del.close = close;
            def.delimiters.push_back(del);
        }
    }

    def.styles = styleMap;
    return def;
}

bool UdlXmlIo::exportToXml(const UdlDefinition &def, const QString &path)
{
    if (!def.isValid())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("NotepadPlus"));
    xml.writeStartElement(QStringLiteral("UserLang"));
    xml.writeAttribute(QStringLiteral("name"), def.name);
    xml.writeAttribute(QStringLiteral("ext"), def.extensions.join(QLatin1Char(' ')));
    xml.writeAttribute(QStringLiteral("udlVersion"), QStringLiteral("2.1"));

    xml.writeStartElement(QStringLiteral("Settings"));
    xml.writeStartElement(QStringLiteral("Global"));
    xml.writeAttribute(QStringLiteral("caseIgnored"), def.caseSensitive
                        ? QStringLiteral("no") : QStringLiteral("yes"));
    xml.writeAttribute(QStringLiteral("allowFoldOfComments"), QStringLiteral("no"));
    xml.writeAttribute(QStringLiteral("foldCompact"), QStringLiteral("no"));
    xml.writeEndElement();  // Global
    xml.writeStartElement(QStringLiteral("Prefix"));
    for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
        xml.writeAttribute(QStringLiteral("words%1").arg(i + 1),
                            def.keywordGroupPrefix(i) ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    xml.writeEndElement();  // Prefix
    xml.writeEndElement();  // Settings

    xml.writeStartElement(QStringLiteral("KeywordLists"));

    auto writeKeywords = [&](const QString &name, const QString &text) {
        xml.writeStartElement(QStringLiteral("Keywords"));
        xml.writeAttribute(QStringLiteral("name"), name);
        xml.writeCharacters(text);
        xml.writeEndElement();
    };

    // Comments：代碼 00=行註解 01=區塊起 02=區塊結尾
    QMap<int, QString> commentCoded;
    commentCoded.insert(0, def.lineComment);
    commentCoded.insert(1, def.blockCommentStart);
    commentCoded.insert(2, def.blockCommentEnd);
    writeKeywords(QStringLiteral("Comments"), encodeCodedKeywords(commentCoded));

    for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
        const QStringList kws(def.keywordGroup(i).begin(), def.keywordGroup(i).end());
        writeKeywords(QStringLiteral("Keywords%1").arg(i + 1), kws.join(QLatin1Char(' ')));
    }

    const QStringList ops(def.operators.begin(), def.operators.end());
    writeKeywords(QStringLiteral("Operators1"), ops.join(QLatin1Char(' ')));
    writeKeywords(QStringLiteral("Operators2"), QString());

    writeKeywords(QStringLiteral("Folders in code1, open"), def.folderTokens.open);
    writeKeywords(QStringLiteral("Folders in code1, middle"), def.folderTokens.middle);
    writeKeywords(QStringLiteral("Folders in code1, close"), def.folderTokens.close);

    QMap<int, QString> delimCoded;
    for (int i = 0; i < kMaxDelimiterSets && i < def.delimiters.size(); ++i) {
        const UdlDelimiter &del = def.delimiters.at(i);
        delimCoded.insert(i * 3, del.open);
        delimCoded.insert(i * 3 + 1, del.escape);
        delimCoded.insert(i * 3 + 2, del.close);
    }
    writeKeywords(QStringLiteral("Delimiters"), encodeCodedKeywords(delimCoded));

    xml.writeEndElement();  // KeywordLists

    xml.writeStartElement(QStringLiteral("Styles"));
    for (auto it = def.styles.constBegin(); it != def.styles.constEnd(); ++it) {
        xml.writeStartElement(QStringLiteral("WordsStyle"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("STYLE%1").arg(it.key()));
        xml.writeAttribute(QStringLiteral("styleID"), QString::number(it.key()));
        const QString fgHex = writeNppColorHex(QColor(it.value().fg));
        const QString bgHex = writeNppColorHex(QColor(it.value().bg));
        xml.writeAttribute(QStringLiteral("fgColor"), fgHex);
        xml.writeAttribute(QStringLiteral("bgColor"), bgHex);
        int fontStyle = 0;
        if (it.value().bold) fontStyle |= kFontStyleBold;
        if (it.value().italic) fontStyle |= kFontStyleItalic;
        if (it.value().underline) fontStyle |= kFontStyleUnderline;
        xml.writeAttribute(QStringLiteral("fontStyle"), QString::number(fontStyle));
        xml.writeEndElement();  // WordsStyle
    }
    xml.writeEndElement();  // Styles

    xml.writeEndElement();  // UserLang
    xml.writeEndElement();  // NotepadPlus
    xml.writeEndDocument();

    return !xml.hasError();
}

}  // namespace macpad::features
