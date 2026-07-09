#include "persistence/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace macpad::persistence {

static QString g_configDirOverride;  // -settingsDir 覆寫；非空則優先

void AppPaths::setConfigDirOverride(const QString &dir)
{
    g_configDirOverride = dir;
}

QString AppPaths::configDir()
{
    // -settingsDir 覆寫（命令列自訂設定目錄）優先
    if (!g_configDirOverride.isEmpty()) {
        QDir().mkpath(g_configDirOverride);
        return g_configDirOverride;
    }
    // profile §1：AppDataLocation（macOS: ~/Library/Application Support/macpad++/；
    //                              Windows: %APPDATA%\macpad++\）
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
