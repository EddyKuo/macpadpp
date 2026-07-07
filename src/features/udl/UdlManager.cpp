#include "features/udl/UdlManager.h"

#include "persistence/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

namespace macpad::features {

static QString udlDir()
{
    const QString dir = macpad::persistence::AppPaths::filePath(QStringLiteral("udl"));
    QDir().mkpath(dir);
    return dir;
}

void UdlManager::loadAll()
{
    m_defs.clear();
    QDir dir(udlDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            UdlDefinition d = UdlDefinition::fromJson(doc.object());
            if (d.isValid())
                m_defs.push_back(d);
        }
    }
}

bool UdlManager::save(const UdlDefinition &def)
{
    if (!def.isValid())
        return false;
    QString safe = def.name;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    QFile file(udlDir() + QLatin1Char('/') + safe + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(def.toJson()).toJson(QJsonDocument::Indented));
    file.close();
    // 更新記憶體
    for (auto &d : m_defs)
        if (d.name == def.name) { d = def; return true; }
    m_defs.push_back(def);
    return true;
}

bool UdlManager::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const UdlDefinition d = UdlDefinition::fromJson(doc.object());
    if (!d.isValid())
        return false;
    return save(d);
}

const UdlDefinition *UdlManager::findForExtension(const QString &suffix) const
{
    const QString s = suffix.toLower();
    for (const auto &d : m_defs)
        if (d.extensions.contains(s))
            return &d;
    return nullptr;
}

}  // namespace macpad::features
