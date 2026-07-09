#pragma once

// 跨平台桌面整合：把「在檔案管理器顯示」「在終端機開啟」「以外部程式開啟」
// 「平台預設等寬字型」等 OS 專屬行為集中於一處（見 docs/windows_sa_sd.md §3）。
// 上層（app/ui/core）呼叫本層抽象，不得直接內嵌平台專屬指令字串（IL-3 單一真相）。

#include <QString>
#include <QStringList>

namespace macpad::platform {

// 一筆子行程指令（program + argv）。program 為空代表「無對應指令，改走 QDesktopServices 後備」。
struct ShellCommand {
    QString program;
    QStringList args;
    bool isValid() const { return !program.isEmpty(); }
};

// === 純函式（可測試，不啟動任何行程） ===

// 在系統檔案管理器中「顯示並選取」指定檔案所對應的指令。
//   macOS  : open -R <path>
//   Windows: explorer.exe /select,<native path>
//   其他   : 空 program（呼叫端改開所在目錄）
ShellCommand revealArgsFor(const QString &path);

// 於指定資料夾開啟終端機的「首選」指令。
//   macOS  : open -a Terminal <dir>
//   Windows: wt.exe -d <native dir>（失敗時呼叫端退回 cmd）
//   其他   : 空 program
ShellCommand terminalCommandFor(const QString &dir);

// 平台預設等寬字型家族名稱（執行期依 QFontDatabase 挑選實際存在者）。
//   macOS  : Menlo
//   Windows: Cascadia Mono → Consolas → Courier New（取已安裝的第一個）
//   其他   : Monospace
QString defaultMonospaceFamily();

// === 動作（實際啟動行程／開啟 URL） ===

// 在檔案管理器中顯示並選取檔案。
void revealInFileManager(const QString &path);

// 於指定資料夾開啟終端機（Windows 先試 Windows Terminal，失敗退回 cmd）。
void openInTerminal(const QString &dir);

// 以外部應用開啟檔案；appName 為空時交給系統預設程式（QDesktopServices）。
void openInApp(const QString &appName, const QString &path);

}  // namespace macpad::platform
