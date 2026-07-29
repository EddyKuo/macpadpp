#include "persistence/PluginStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>

namespace macpad::persistence {

static QString pluginsPath() { return AppPaths::filePath(QStringLiteral("plugins.json")); }

static constexpr auto kDisabledKey = "disabled";

QSet<QString> PluginStore::disabledIds()
{
    const QJsonObject root = JsonFile::load(pluginsPath());
    QSet<QString> ids;
    const QJsonArray arr = root.value(QLatin1String(kDisabledKey)).toArray();
    for (const QJsonValue &v : arr) {
        const QString id = v.toString().trimmed();
        if (!id.isEmpty())
            ids.insert(id);
    }
    return ids;
}

bool PluginStore::setDisabledIds(const QSet<QString> &ids)
{
    // 排序後寫出：讓檔案內容穩定、diff 友善（QSet 的迭代順序不保證）
    QStringList sorted(ids.cbegin(), ids.cend());
    sorted.sort();

    QJsonArray arr;
    for (const QString &id : std::as_const(sorted))
        arr.append(id);

    QJsonObject root;
    root.insert(QLatin1String(kDisabledKey), arr);
    return JsonFile::save(pluginsPath(), root);
}

}  // namespace macpad::persistence
