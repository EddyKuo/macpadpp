#include "persistence/JsonFile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace macpad::persistence {

QJsonObject JsonFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};  // 不存在 → 空（首次啟動）

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};  // 損毀 → 空（corruption-safe，FR-016 AC3）

    return doc.object();
}

bool JsonFile::save(const QString &path, const QJsonObject &obj)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QJsonDocument doc(obj);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size())
        return false;

    return file.commit();  // 原子 rename
}

}  // namespace macpad::persistence
