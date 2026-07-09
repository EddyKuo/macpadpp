#include "persistence/ThemeStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace macpad::persistence {

// themes/ 子目錄（不存在則建立）；回傳絕對路徑
static QString themesDir()
{
    const QString dir = AppPaths::configDir() + QStringLiteral("/themes");
    QDir().mkpath(dir);
    return dir;
}

static QString themeFilePath(const QString &name)
{
    return themesDir() + QLatin1Char('/') + name + QStringLiteral(".json");
}

// 主題的 styles 區塊與 styles.json 共用同一份 schema（含 global 邊界/底色 + 每語言覆寫），
// 序列化統一委派給 StyleStore，確保欄位不漂移（IL-3 單一真相）。
static QJsonObject themeToJson(const Theme &t)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("dark"), t.dark);
    o.insert(QStringLiteral("styles"), StyleStore::styleSettingsToJson(t.styles));
    return o;
}

static Theme themeFromJson(const QJsonObject &o)
{
    Theme t;
    t.name = o.value(QStringLiteral("name")).toString();
    t.dark = o.value(QStringLiteral("dark")).toBool();
    t.styles = StyleStore::styleSettingsFromJson(o.value(QStringLiteral("styles")).toObject());
    return t;
}

QStringList ThemeStore::listThemes()
{
    QStringList names;
    const QDir dir(themesDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                             QDir::Files, QDir::Name);
    names.reserve(files.size());
    for (const QString &f : files)
        names.append(QFileInfo(f).completeBaseName());
    return names;
}

Theme ThemeStore::load(const QString &name)
{
    if (name.isEmpty())
        return Theme();
    const QJsonObject o = JsonFile::load(themeFilePath(name));
    if (o.isEmpty())
        return Theme();
    Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        t.name = name;  // 容錯：檔案內未存 name 時以檔名回填
    return t;
}

bool ThemeStore::save(const Theme &theme)
{
    if (theme.name.isEmpty())
        return false;
    return JsonFile::save(themeFilePath(theme.name), themeToJson(theme));
}

bool ThemeStore::importFromFile(const QString &path)
{
    const QJsonObject o = JsonFile::load(path);
    if (o.isEmpty())
        return false;
    const Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        return false;
    return save(t);
}

bool ThemeStore::exportToFile(const QString &name, const QString &path)
{
    const Theme t = load(name);
    if (t.name.isEmpty())
        return false;
    return JsonFile::save(path, themeToJson(t));
}

bool ThemeStore::remove(const QString &name)
{
    if (name.isEmpty())
        return false;
    return QFile::remove(themeFilePath(name));
}

int ThemeStore::seedBundledThemes()
{
    const QDir res(QStringLiteral(":/themes"));
    const QStringList files = res.entryList(QStringList() << QStringLiteral("*.json"),
                                             QDir::Files, QDir::Name);
    int seeded = 0;
    for (const QString &f : files) {
        const QString name = QFileInfo(f).completeBaseName();
        const QString target = themeFilePath(name);
        if (QFileInfo::exists(target))
            continue;  // 已存在（使用者可能已修改）→ 不覆蓋；使用者刪掉的也不還原
        // 直接複製資源檔內容（本身已是合法的主題 JSON）；QFile::copy 對唯讀資源可正常讀取。
        if (QFile::copy(res.filePath(f), target)) {
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner
                                          | QFile::ReadGroup | QFile::ReadOther);  // 資源檔為唯讀，放寬使其可被匯出/刪除
            ++seeded;
        }
    }
    return seeded;
}

}  // namespace macpad::persistence
