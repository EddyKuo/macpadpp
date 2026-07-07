#include "features/run/RunCommand.h"

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

}  // namespace macpad::features
