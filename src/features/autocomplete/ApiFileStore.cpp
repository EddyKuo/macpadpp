#include "features/autocomplete/ApiFileStore.h"

#include "persistence/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QXmlStreamReader>

namespace macpad::features {

namespace {

// 語言別快取：同一語言的 API 檔只解析一次。
// 值為「已解析結果」，即使檔案不存在也會存入空 vector，避免每次按鍵都去敲檔案系統。
QHash<QString, QVector<ApiEntry>> &cache()
{
    static QHash<QString, QVector<ApiEntry>> c;
    return c;
}

// 依 Notepad++ 的慣例組出單一多載的簽名字串：
//   retVal name(param1, param2)  descr
// 缺項一律略過，不補假資料。
QString buildOverload(const QString &name, const QString &retVal,
                      const QStringList &params, const QString &descr)
{
    QString sig;
    if (!retVal.isEmpty())
        sig += retVal + QLatin1Char(' ');
    sig += name + QLatin1Char('(') + params.join(QStringLiteral(", ")) + QLatin1Char(')');
    if (!descr.isEmpty())
        sig += QStringLiteral("\n") + descr;
    return sig;
}

}  // namespace

QVector<ApiEntry> ApiFileStore::parseXml(const QByteArray &xml)
{
    QVector<ApiEntry> out;
    QXmlStreamReader reader(xml);

    ApiEntry current;
    bool inKeyword = false;
    // 目前正在收集的多載
    QString retVal;
    QString descr;
    QStringList params;
    bool inOverload = false;

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView elem = reader.name();
            if (elem == QLatin1String("KeyWord")) {
                current = ApiEntry();
                current.name = reader.attributes().value(QStringLiteral("name")).toString();
                current.isFunction =
                    reader.attributes().value(QStringLiteral("func")).toString()
                        .compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
                inKeyword = !current.name.isEmpty();
            } else if (inKeyword && elem == QLatin1String("Overload")) {
                inOverload = true;
                retVal = reader.attributes().value(QStringLiteral("retVal")).toString();
                descr = reader.attributes().value(QStringLiteral("descr")).toString();
                params.clear();
            } else if (inOverload && elem == QLatin1String("Param")) {
                const QString p = reader.attributes().value(QStringLiteral("name")).toString();
                if (!p.isEmpty())
                    params << p;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView elem = reader.name();
            if (inOverload && elem == QLatin1String("Overload")) {
                current.overloads << buildOverload(current.name, retVal, params, descr);
                inOverload = false;
            } else if (inKeyword && elem == QLatin1String("KeyWord")) {
                out.push_back(current);
                inKeyword = false;
            }
        }
    }

    if (reader.hasError())
        return {};   // 格式壞掉就整份放棄，不回傳半套資料（IL-4：失敗要明確）
    return out;
}

QVector<ApiEntry> ApiFileStore::parseFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseXml(f.readAll());
}

QString ApiFileStore::filePathFor(const QString &langKey)
{
    if (langKey.isEmpty())
        return QString();
    return QDir(macpad::persistence::AppPaths::configDir())
        .filePath(QStringLiteral("apis/%1.xml").arg(langKey));
}

QVector<ApiEntry> ApiFileStore::entriesFor(const QString &langKey)
{
    if (langKey.isEmpty())
        return {};

    auto &c = cache();
    const auto it = c.constFind(langKey);
    if (it != c.constEnd())
        return it.value();

    // 檔案不存在是正常情況（使用者未提供 API 檔），一樣存入空結果避免重複探測
    const QVector<ApiEntry> parsed = parseFile(filePathFor(langKey));
    c.insert(langKey, parsed);
    return parsed;
}

void ApiFileStore::clearCache()
{
    cache().clear();
}

}  // namespace macpad::features
