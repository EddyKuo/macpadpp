// MimeTools 實作 — Base64 / URL 編碼解碼（文字視為 UTF-8）
#include "features/mime/MimeTools.h"

#include <QByteArray>
#include <QUrl>

namespace macpad::features {

QString MimeTools::base64Encode(const QString &s)
{
    return QString::fromLatin1(s.toUtf8().toBase64());
}

QString MimeTools::base64Decode(const QString &s)
{
    const QByteArray decoded = QByteArray::fromBase64(s.toUtf8());
    return QString::fromUtf8(decoded);
}

QString MimeTools::urlEncode(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

QString MimeTools::urlDecode(const QString &s)
{
    return QUrl::fromPercentEncoding(s.toUtf8());
}

}  // namespace macpad::features
