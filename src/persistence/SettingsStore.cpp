#include "persistence/SettingsStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonObject>

namespace macpad::persistence {

static QString settingsPath() { return AppPaths::filePath(QStringLiteral("settings.json")); }

static ThemeMode themeFromString(const QString &s)
{
    if (s == QLatin1String("light")) return ThemeMode::Light;
    if (s == QLatin1String("dark"))  return ThemeMode::Dark;
    return ThemeMode::System;
}

static QString themeToString(ThemeMode m)
{
    switch (m) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark:  return QStringLiteral("dark");
    case ThemeMode::System:return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

Settings SettingsStore::load()
{
    Settings s;  // 預設值
    const QJsonObject o = JsonFile::load(settingsPath());
    if (o.isEmpty())
        return s;  // 首次或損毀 → 全預設（缺欄位回填）

    s.restoreOnLaunch = o.value(QStringLiteral("restore_on_launch")).toBool(s.restoreOnLaunch);
    s.theme = themeFromString(o.value(QStringLiteral("theme")).toString(themeToString(s.theme)));
    s.autosaveEnabled = o.value(QStringLiteral("autosave_enabled")).toBool(s.autosaveEnabled);
    s.autosaveIntervalSec = o.value(QStringLiteral("autosave_interval_sec")).toInt(s.autosaveIntervalSec);
    s.tabWidth = o.value(QStringLiteral("tab_width")).toInt(s.tabWidth);
    s.singleInstance = o.value(QStringLiteral("single_instance")).toBool(s.singleInstance);
    s.language = o.value(QStringLiteral("language")).toString(s.language);
    return s;
}

bool SettingsStore::save(const Settings &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), s.schemaVersion);
    o.insert(QStringLiteral("restore_on_launch"), s.restoreOnLaunch);
    o.insert(QStringLiteral("theme"), themeToString(s.theme));
    o.insert(QStringLiteral("autosave_enabled"), s.autosaveEnabled);
    o.insert(QStringLiteral("autosave_interval_sec"), s.autosaveIntervalSec);
    o.insert(QStringLiteral("tab_width"), s.tabWidth);
    o.insert(QStringLiteral("single_instance"), s.singleInstance);
    o.insert(QStringLiteral("language"), s.language);
    return JsonFile::save(settingsPath(), o);
}

}  // namespace macpad::persistence
