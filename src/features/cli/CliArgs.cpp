#include "features/cli/CliArgs.h"

namespace macpad::features {

FileArg CliArgs::parseFileArg(const QString &arg)
{
    FileArg out;
    const int colon = arg.lastIndexOf(QLatin1Char(':'));
    // 尾端須為 ":數字"，且冒號不在開頭（避免 ":80" 這種無路徑情況誤判）
    if (colon > 0 && colon < arg.size() - 1) {
        const QString tail = arg.mid(colon + 1);
        bool ok = false;
        const int line = tail.toInt(&ok);
        if (ok && line > 0) {
            out.path = arg.left(colon);
            out.line = line;
            return out;
        }
    }
    out.path = arg;
    return out;
}

ParsedArgs CliArgs::parse(const QStringList &args)
{
    ParsedArgs out;

    for (const QString &arg : args) {
        if (arg == QLatin1String("-ro")) {
            out.readOnly = true;
            continue;
        }
        if (arg == QLatin1String("-nosession")) {
            out.noSession = true;
            continue;
        }
        if (arg == QLatin1String("-multiInst")) {
            out.multiInstance = true;
            continue;
        }
        if (arg.startsWith(QLatin1String("-n")) && arg.size() > 2) {
            bool ok = false;
            const int line = arg.mid(2).toInt(&ok);
            if (ok) {
                out.gotoLine = line;
                continue;
            }
        }
        if (arg.startsWith(QLatin1String("-c")) && arg.size() > 2) {
            bool ok = false;
            const int col = arg.mid(2).toInt(&ok);
            if (ok) {
                out.gotoColumn = col;
                continue;
            }
        }
        // 其餘視為檔案路徑（仍支援 path:line 後綴）
        out.files.append(parseFileArg(arg));
    }

    return out;
}

}  // namespace macpad::features
