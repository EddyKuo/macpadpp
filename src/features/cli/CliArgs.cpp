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

}  // namespace macpad::features
