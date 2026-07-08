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

    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);

        // 吞噬下一個 token 並前移索引（用於帶值旗標）；無下一個 token 則回傳空字串
        auto takeNextToken = [&args, &i]() -> QString {
            if (i + 1 < args.size()) {
                return args.at(++i);
            }
            return QString();
        };

        if (arg == QLatin1String("-ro") || arg == QLatin1String("-r")) {
            // -r 為 -ro 的別名
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
        if (arg == QLatin1String("-alwaysOnTop")) {
            out.alwaysOnTop = true;
            continue;
        }
        if (arg == QLatin1String("-quickPrint")) {
            out.quickPrint = true;
            continue;
        }
        if (arg == QLatin1String("-monitor")) {
            out.monitorMode = true;
            continue;
        }
        if (arg == QLatin1String("-notabbar")) {
            out.hideTabBar = true;
            continue;
        }
        if (arg == QLatin1String("-fullReadOnly")) {
            out.fullReadOnly = true;
            continue;
        }
        if (arg == QLatin1String("-notepadStyleCmdline")) {
            out.notepadStyleCmdline = true;
            continue;
        }
        if (arg == QLatin1String("-systemtray")) {
            // Windows-only：macOS 無系統匣，僅辨識並吞噬
            out.systemTrayIgnored = true;
            continue;
        }
        if (arg == QLatin1String("-noPlugin")) {
            // Windows-only：macOS 外掛機制不同，僅辨識並吞噬
            out.noPluginIgnored = true;
            continue;
        }
        if (arg == QLatin1String("-z")) {
            // Notepad++ 相容：吞噬下一個 token，無實際作用（swallow-next-token no-op）
            takeNextToken();
            continue;
        }
        if (arg == QLatin1String("-openSession")) {
            out.openSessionPath = takeNextToken();
            continue;
        }
        if (arg == QLatin1String("-openFoldersAsWorkspace")) {
            const QString folder = takeNextToken();
            if (!folder.isEmpty()) {
                out.openFoldersAsWorkspace.append(folder);
            }
            continue;
        }
        if (arg == QLatin1String("-x")) {
            // 只有下一個 token 為合法整數時才吞噬，避免把後續檔案路徑誤當座標值消耗掉
            if (i + 1 < args.size()) {
                bool ok = false;
                const int x = args.at(i + 1).toInt(&ok);
                if (ok) {
                    out.windowX = x;
                    ++i;
                }
            }
            continue;
        }
        if (arg == QLatin1String("-y")) {
            if (i + 1 < args.size()) {
                bool ok = false;
                const int y = args.at(i + 1).toInt(&ok);
                if (ok) {
                    out.windowY = y;
                    ++i;
                }
            }
            continue;
        }
        if (arg == QLatin1String("-settingsDir")) {
            out.settingsDir = takeNextToken();
            continue;
        }
        if (arg == QLatin1String("-pluginMessage")) {
            // Windows-only：外掛間通訊，macOS 無對應機制，僅吞噬其值
            takeNextToken();
            out.pluginMessageIgnored = true;
            continue;
        }
        if (arg == QLatin1String("-loadingTime")) {
            // 啟動計時除錯旗標，僅吞噬其值
            takeNextToken();
            out.loadingTimeIgnored = true;
            continue;
        }
        if (arg == QLatin1String("-qn") || arg == QLatin1String("-qt") ||
            arg == QLatin1String("-qf") || arg == QLatin1String("-qSpeed")) {
            // Ghost-typing 除錯旗標，僅吞噬其值
            takeNextToken();
            out.ghostTypingIgnored = true;
            continue;
        }
        if (arg.startsWith(QLatin1String("-titleAdd:"))) {
            out.titleAdd = arg.mid(10);
            continue;
        }
        if (arg.startsWith(QLatin1String("-title:"))) {
            out.titleAdd = arg.mid(7);
            continue;
        }
        if (arg.startsWith(QLatin1String("-udl="))) {
            out.udlName = arg.mid(5);
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
        if (arg.startsWith(QLatin1String("-p")) && arg.size() > 2) {
            bool ok = false;
            const int pos = arg.mid(2).toInt(&ok);
            if (ok) {
                out.gotoPos = pos;
                continue;
            }
        }
        if (arg.startsWith(QLatin1String("-L")) && arg.size() > 2) {
            // 大寫 -L<langCode>：介面語言代碼；與小寫 -l<lang>（語法高亮語言）區分
            out.uiLangCode = arg.mid(2);
            continue;
        }
        if (arg.startsWith(QLatin1String("-l")) && arg.size() > 2) {
            out.forceLanguage = arg.mid(2);
            continue;
        }
        // 無法辨識但以 "-" 開頭的旗標：忽略，不視為檔案路徑
        if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        }
        // 其餘視為檔案路徑（仍支援 path:line 後綴）
        out.files.append(parseFileArg(arg));
    }

    return out;
}

}  // namespace macpad::features
