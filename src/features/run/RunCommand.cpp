#include "features/run/RunCommand.h"

namespace macpad::features {

QString RunCommand::expand(const QString &command, const RunVars &vars)
{
    QString out = command;
    out.replace(QStringLiteral("$(FULL_CURRENT_PATH)"), vars.fullCurrentPath);
    out.replace(QStringLiteral("$(CURRENT_DIRECTORY)"), vars.currentDirectory);
    out.replace(QStringLiteral("$(FILE_NAME)"), vars.fileName);
    out.replace(QStringLiteral("$(NAME_PART)"), vars.nameNoExt);
    out.replace(QStringLiteral("$(CURRENT_WORD)"), vars.currentWord);
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
