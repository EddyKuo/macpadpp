#include "core/FileEncoding.h"

#include <QStringConverter>
#include <QTextCodec>   // Qt6Core5Compat：傳統/區域編碼支援

namespace macpad::core {

DetectResult FileEncoding::detect(const QByteArray &head)
{
    DetectResult r;

    // 1) BOM 偵測
    const auto n = head.size();
    const auto u = reinterpret_cast<const unsigned char *>(head.constData());
    if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        r.encoding = Encoding::Utf8Bom;
        r.hasBom = true;
    } else if (n >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
        r.encoding = Encoding::Utf16LE;
        r.hasBom = true;
    } else if (n >= 2 && u[0] == 0xFE && u[1] == 0xFF) {
        r.encoding = Encoding::Utf16BE;
        r.hasBom = true;
    } else {
        // 2) 無 BOM：以 UTF-8 嚴格解碼測試有效性
        QStringDecoder dec(QStringConverter::Utf8,
                           QStringConverter::Flag::ConvertInvalidToNull);
        const QString probe = dec.decode(head);
        r.encoding = dec.hasError() ? Encoding::Latin1 : Encoding::Utf8;
        Q_UNUSED(probe);
    }

    // 3) EOL 偵測（掃描開頭，先出現者為準）
    for (int i = 0; i < n; ++i) {
        if (u[i] == '\r') {
            r.eol = (i + 1 < n && u[i + 1] == '\n') ? Eol::CrLf : Eol::Cr;
            break;
        }
        if (u[i] == '\n') {
            r.eol = Eol::Lf;
            break;
        }
    }
    return r;
}

QString FileEncoding::decode(const QByteArray &raw, Encoding enc)
{
    switch (enc) {
    case Encoding::Utf8:
    case Encoding::Utf8Bom: {
        QByteArray body = raw;
        if (enc == Encoding::Utf8Bom && body.startsWith("\xEF\xBB\xBF"))
            body.remove(0, 3);
        return QString::fromUtf8(body);
    }
    case Encoding::Utf16LE: {
        QStringDecoder dec(QStringConverter::Utf16LE);
        QString s = dec.decode(raw);
        if (s.startsWith(QChar(0xFEFF)))  // 去除前導 BOM（顯式 LE 不自動剝除）
            s.remove(0, 1);
        return s;
    }
    case Encoding::Utf16BE: {
        QStringDecoder dec(QStringConverter::Utf16BE);
        QString s = dec.decode(raw);
        if (s.startsWith(QChar(0xFEFF)))
            s.remove(0, 1);
        return s;
    }
    case Encoding::Latin1:
        return QString::fromLatin1(raw);
    }
    return QString::fromUtf8(raw);
}

QByteArray FileEncoding::encode(const QString &text, Encoding enc)
{
    switch (enc) {
    case Encoding::Utf8:
        return text.toUtf8();
    case Encoding::Utf8Bom:
        return QByteArray("\xEF\xBB\xBF") + text.toUtf8();
    case Encoding::Utf16LE: {
        QStringEncoder e(QStringConverter::Utf16LE);  // 顯式 LE 不含 BOM → 手動加，供偵測
        return QByteArray("\xFF\xFE", 2) + e.encode(text);
    }
    case Encoding::Utf16BE: {
        QStringEncoder e(QStringConverter::Utf16BE);
        return QByteArray("\xFE\xFF", 2) + e.encode(text);
    }
    case Encoding::Latin1:
        return text.toLatin1();
    }
    return text.toUtf8();
}

QString FileEncoding::encodingName(Encoding enc)
{
    switch (enc) {
    case Encoding::Utf8:     return QStringLiteral("UTF-8");
    case Encoding::Utf8Bom:  return QStringLiteral("UTF-8 BOM");
    case Encoding::Utf16LE:  return QStringLiteral("UTF-16 LE");
    case Encoding::Utf16BE:  return QStringLiteral("UTF-16 BE");
    case Encoding::Latin1:   return QStringLiteral("ANSI");
    }
    return QStringLiteral("UTF-8");
}

QString FileEncoding::eolName(Eol eol)
{
    switch (eol) {
    case Eol::Lf:   return QStringLiteral("LF");
    case Eol::CrLf: return QStringLiteral("CRLF");
    case Eol::Cr:   return QStringLiteral("CR");
    }
    return QStringLiteral("LF");
}

QString FileEncoding::decodeWithCodec(const QByteArray &raw, const QString &codecName)
{
    QTextCodec *c = QTextCodec::codecForName(codecName.toLatin1());
    if (!c)
        return QString::fromUtf8(raw);   // 回退
    return c->toUnicode(raw);
}

QByteArray FileEncoding::encodeWithCodec(const QString &text, const QString &codecName)
{
    QTextCodec *c = QTextCodec::codecForName(codecName.toLatin1());
    if (!c)
        return text.toUtf8();
    return c->fromUnicode(text);
}

QVector<FileEncoding::CharsetGroup> FileEncoding::characterSets()
{
    // 複刻 Notepad++「Encoding ▸ Character sets」的地區分組。codec 名採 QTextCodec 可辨識者。
    return {
        {QStringLiteral("Western European"), {
            {QStringLiteral("ISO 8859-1 (Latin-1)"), QStringLiteral("ISO-8859-1")},
            {QStringLiteral("ISO 8859-15 (Latin-9)"), QStringLiteral("ISO-8859-15")},
            {QStringLiteral("Windows-1252"), QStringLiteral("windows-1252")},
        }},
        {QStringLiteral("Central European"), {
            {QStringLiteral("ISO 8859-2"), QStringLiteral("ISO-8859-2")},
            {QStringLiteral("Windows-1250"), QStringLiteral("windows-1250")},
        }},
        {QStringLiteral("Chinese"), {
            {QStringLiteral("Big5 (Traditional)"), QStringLiteral("Big5")},
            {QStringLiteral("GB2312 (Simplified)"), QStringLiteral("GB2312")},
            {QStringLiteral("GBK (Simplified)"), QStringLiteral("GBK")},
            {QStringLiteral("GB18030 (Simplified)"), QStringLiteral("GB18030")},
        }},
        {QStringLiteral("Japanese"), {
            {QStringLiteral("Shift-JIS"), QStringLiteral("Shift_JIS")},
            {QStringLiteral("EUC-JP"), QStringLiteral("EUC-JP")},
            {QStringLiteral("ISO-2022-JP"), QStringLiteral("ISO-2022-JP")},
        }},
        {QStringLiteral("Korean"), {
            {QStringLiteral("EUC-KR"), QStringLiteral("EUC-KR")},
            {QStringLiteral("Windows-949"), QStringLiteral("windows-949")},
        }},
        {QStringLiteral("Cyrillic"), {
            {QStringLiteral("Windows-1251"), QStringLiteral("windows-1251")},
            {QStringLiteral("ISO 8859-5"), QStringLiteral("ISO-8859-5")},
            {QStringLiteral("KOI8-R"), QStringLiteral("KOI8-R")},
            {QStringLiteral("KOI8-U"), QStringLiteral("KOI8-U")},
        }},
        {QStringLiteral("Greek"), {
            {QStringLiteral("ISO 8859-7"), QStringLiteral("ISO-8859-7")},
            {QStringLiteral("Windows-1253"), QStringLiteral("windows-1253")},
        }},
        {QStringLiteral("Turkish"), {
            {QStringLiteral("ISO 8859-9"), QStringLiteral("ISO-8859-9")},
            {QStringLiteral("Windows-1254"), QStringLiteral("windows-1254")},
        }},
        {QStringLiteral("Hebrew"), {
            {QStringLiteral("ISO 8859-8"), QStringLiteral("ISO-8859-8")},
            {QStringLiteral("Windows-1255"), QStringLiteral("windows-1255")},
        }},
        {QStringLiteral("Arabic"), {
            {QStringLiteral("ISO 8859-6"), QStringLiteral("ISO-8859-6")},
            {QStringLiteral("Windows-1256"), QStringLiteral("windows-1256")},
        }},
        {QStringLiteral("Baltic"), {
            {QStringLiteral("ISO 8859-13"), QStringLiteral("ISO-8859-13")},
            {QStringLiteral("Windows-1257"), QStringLiteral("windows-1257")},
        }},
        {QStringLiteral("Thai"), {
            {QStringLiteral("TIS-620"), QStringLiteral("TIS-620")},
            {QStringLiteral("Windows-874"), QStringLiteral("windows-874")},
        }},
        {QStringLiteral("Vietnamese"), {
            {QStringLiteral("Windows-1258"), QStringLiteral("windows-1258")},
        }},
    };
}

}  // namespace macpad::core
