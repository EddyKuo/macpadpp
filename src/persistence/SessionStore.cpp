#include "persistence/SessionStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QCryptographicHash>
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

// 由 session 名稱推導檔名：sanitize 後再附加原始名稱的雜湊，
// 避免僅在非法字元上不同的名稱（如 "a/b" 與 "a?b"）映射到同一檔名而互相覆蓋。
// 相同名稱恆得相同檔名（可正常覆蓋 / 讀回）。
static QString sessionFileName(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    return safe + QLatin1Char('-') + hash + QStringLiteral(".json");
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
        o.insert(QStringLiteral("selection"), t.selection);
        QJsonArray bookmarks;
        for (int b : t.bookmarks)
            bookmarks.append(b);
        o.insert(QStringLiteral("bookmarks"), bookmarks);
        o.insert(QStringLiteral("language_override"), t.languageOverride);
        o.insert(QStringLiteral("view"), t.view);
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
    const int rawActive = root.value(QStringLiteral("active_index")).toInt(0);
    const QJsonArray tabs = root.value(QStringLiteral("tabs")).toArray();
    int skippedBeforeActive = 0;  // 記錄作用分頁之前被略過的空 path 項目數，用於重新映射 activeIndex
    for (int i = 0; i < tabs.size(); ++i) {
        const QJsonObject o = tabs.at(i).toObject();
        const QString path = o.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            if (i < rawActive)
                ++skippedBeforeActive;
            continue;
        }
        TabState t;
        t.path = path;
        t.line = o.value(QStringLiteral("line")).toInt(0);
        t.index = o.value(QStringLiteral("index")).toInt(0);
        t.firstVisibleLine = o.value(QStringLiteral("first_visible_line")).toInt(0);
        t.selection = o.value(QStringLiteral("selection")).toString();
        const QJsonArray bookmarks = o.value(QStringLiteral("bookmarks")).toArray();
        for (const QJsonValue &bv : bookmarks)
            t.bookmarks.append(bv.toInt(0));
        t.languageOverride = o.value(QStringLiteral("language_override")).toString();
        t.view = o.value(QStringLiteral("view")).toInt(0);
        state.tabs.push_back(t);
    }
    state.activeIndex = rawActive - skippedBeforeActive;  // 依過濾後的陣列重新映射
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
    QJsonObject root = stateToJson(state);
    root.insert(QStringLiteral("name"), name);  // 保留原始顯示名（檔名經 sanitize + 雜湊）
    return JsonFile::save(sessionsDir() + QLatin1Char('/') + sessionFileName(name), root);
}

SessionState SessionStore::loadNamed(const QString &name)
{
    return jsonToState(JsonFile::load(sessionsDir() + QLatin1Char('/') + sessionFileName(name)));
}

SessionState SessionStore::loadFromPath(const QString &path)
{
    return jsonToState(JsonFile::load(path));
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
