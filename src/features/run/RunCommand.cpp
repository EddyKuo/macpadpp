#include "features/run/RunCommand.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QStringView>
#include <QVector>

namespace macpad::features {

QString RunCommand::expand(const QString &command, const RunVars &vars)
{
    // 單次線性掃描：一旦某 marker 被替換，插入的值不會再被後續 marker 重掃，
    // 避免值本身含其他 placeholder 字面量時被二次替換而破壞指令。
    const QVector<QPair<QString, QString>> subs = {
        {QStringLiteral("$(FULL_CURRENT_PATH)"), vars.fullCurrentPath},
        {QStringLiteral("$(CURRENT_DIRECTORY)"), vars.currentDirectory},
        {QStringLiteral("$(FILE_NAME)"), vars.fileName},
        {QStringLiteral("$(NAME_PART)"), vars.nameNoExt},
        {QStringLiteral("$(CURRENT_WORD)"), vars.currentWord},
    };

    QString out;
    out.reserve(command.size());
    int i = 0;
    while (i < command.size()) {
        bool matched = false;
        for (const auto &sub : subs) {
            const QString &marker = sub.first;
            if (QStringView{command}.mid(i).startsWith(marker)) {
                out += sub.second;
                i += marker.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            out += command.at(i);
            ++i;
        }
    }
    return out;
}

QStringList RunCommand::tokenize(const QString &command)
{
    QStringList tokens;
    QString cur;
    bool inQuote = false;
    bool hasToken = false;
    for (int i = 0; i < command.size(); ++i) {
        const QChar c = command.at(i);
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            hasToken = true;  // 引號本身即代表一個（可能空的）token
        } else if (c.isSpace() && !inQuote) {
            if (hasToken) {
                tokens << cur;
                cur.clear();
                hasToken = false;
            }
        } else {
            cur += c;
            hasToken = true;
        }
    }
    if (hasToken)
        tokens << cur;
    return tokens;
}

static QString runCommandsPath()
{
    return macpad::persistence::AppPaths::filePath(QStringLiteral("run_commands.json"));
}

QList<RunCommandEntry> RunCommandStore::load()
{
    QList<RunCommandEntry> out;
    const QJsonObject root = macpad::persistence::JsonFile::load(runCommandsPath());
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        RunCommandEntry entry;
        entry.name = name;
        entry.command = o.value(QStringLiteral("command")).toString();
        // shortcut 為新增欄位；舊檔缺此鍵時 toString() 預設回傳空字串（向下相容）
        entry.shortcut = o.value(QStringLiteral("shortcut")).toString();
        out.append(entry);
    }
    return out;
}

bool RunCommandStore::save(const QList<RunCommandEntry> &entries)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kSchemaVersion);
    QJsonArray items;
    for (const RunCommandEntry &entry : entries) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), entry.name);
        o.insert(QStringLiteral("command"), entry.command);
        o.insert(QStringLiteral("shortcut"), entry.shortcut);
        items.append(o);
    }
    root.insert(QStringLiteral("items"), items);
    return macpad::persistence::JsonFile::save(runCommandsPath(), root);
}

}  // namespace macpad::features
