#include "persistence/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace macpad::persistence {

QString AppPaths::configDir()
{
    // profile §1：~/Library/Application Support/macpad++/（macOS = AppDataLocation）
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.macpad++");
    QDir().mkpath(base);
    return base;
}

QString AppPaths::filePath(const QString &name)
{
    return configDir() + QLatin1Char('/') + name;
}

}  // namespace macpad::persistence
