#include "persistence/SessionStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace macpad::persistence {

static QString sessionPath() { return AppPaths::filePath(QStringLiteral("session.json")); }

static QString sessionsDir()
{
    const QString d = AppPaths::filePath(QStringLiteral("sessions"));
    QDir().mkpath(d);
    return d;
}

static QJsonObject stateToJson(const SessionState &state)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), SessionStore::kSchemaVersion);
    root.insert(QStringLiteral("active_index"), state.activeIndex);
    QJsonArray tabs;
    for (const TabState &t : state.tabs) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), t.path);
        o.insert(QStringLiteral("line"), t.line);
        o.insert(QStringLiteral("index"), t.index);
        o.insert(QStringLiteral("first_visible_line"), t.firstVisibleLine);
        tabs.append(o);
    }
    root.insert(QStringLiteral("tabs"), tabs);
    return root;
}

static SessionState jsonToState(const QJsonObject &root)
{
    SessionState state;
    if (root.isEmpty())
        return state;
    state.activeIndex = root.value(QStringLiteral("active_index")).toInt(0);
    const QJsonArray tabs = root.value(QStringLiteral("tabs")).toArray();
    for (const QJsonValue &v : tabs) {
        const QJsonObject o = v.toObject();
        const QString path = o.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        TabState t;
        t.path = path;
        t.line = o.value(QStringLiteral("line")).toInt(0);
        t.index = o.value(QStringLiteral("index")).toInt(0);
        t.firstVisibleLine = o.value(QStringLiteral("first_visible_line")).toInt(0);
        state.tabs.push_back(t);
    }
    if (state.activeIndex < 0 || state.activeIndex >= state.tabs.size())
        state.activeIndex = 0;
    return state;
}

SessionState SessionStore::load()
{
    return jsonToState(JsonFile::load(sessionPath()));
}

bool SessionStore::save(const SessionState &state)
{
    return JsonFile::save(sessionPath(), stateToJson(state));
}

bool SessionStore::saveNamed(const QString &name, const SessionState &state)
{
    if (name.isEmpty())
        return false;
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    QJsonObject root = stateToJson(state);
    root.insert(QStringLiteral("name"), name);  // 保留原始顯示名（檔名經 sanitize）
    return JsonFile::save(sessionsDir() + QLatin1Char('/') + safe + QStringLiteral(".json"), root);
}

SessionState SessionStore::loadNamed(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    return jsonToState(JsonFile::load(sessionsDir() + QLatin1Char('/') + safe + QStringLiteral(".json")));
}

QStringList SessionStore::listNames()
{
    QStringList names;
    QDir dir(sessionsDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files) {
        const QJsonObject o = JsonFile::load(dir.filePath(f));
        const QString name = o.value(QStringLiteral("name")).toString();
        names << (name.isEmpty() ? QFileInfo(f).completeBaseName() : name);
    }
    return names;
}

}  // namespace macpad::persistence
