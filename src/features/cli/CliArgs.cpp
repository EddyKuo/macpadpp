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
            // 雙平台皆有作用：QSystemTrayIcon 在 Windows 為通知區、macOS 為選單列狀態區。
            out.systemTray = true;
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
        // -notepadStyleCmdline：複刻 notepad.exe 的命令列語意——自「第一個檔名 token」起，
        // 剩下的整段命令列都屬於同一個檔名，未加引號的空白也是檔名的一部分
        //（Notepad++ 用此模式取代系統記事本，才能開啟 C:\my notes\a b.txt 這種未引號路徑）。
        //
        // 關鍵在於這裡要「就地停止旗標解析」：若繼續讓後續 token 走旗標分派，
        // 檔名中長得像旗標的字（如 "my -log.txt" 的 -log.txt 會被誤判為 -l<lang>，
        // 或 "-z" 會連同吞掉下一個 token）就會被吃掉，檔名靜默殘缺。
        // 旗標僅在檔名之前有效，與實際的啟動方式一致。
        if (out.notepadStyleCmdline) {
            QStringList rest;
            for (int k = i; k < args.size(); ++k)
                rest << args.at(k);
            FileArg single;
            // 不解析 path:line 後綴：notepad.exe 沒有這個語法，貿然拆解會拆錯檔名。
            // 註：args 是 OS/Qt 切好的 argv，連續空白早已被壓成一個，
            // 故此處只能以單一空白重組——含連續空白的路徑無法逐字還原（已知限制，
            // 真正的 notepad.exe 讀的是未切割的原始命令列字串）。
            single.path = rest.join(QLatin1Char(' '));
            out.files.append(single);
            break;
        }

        // 其餘視為檔案路徑（仍支援 path:line 後綴）
        out.files.append(parseFileArg(arg));
    }

    return out;
}

}  // namespace macpad::features
