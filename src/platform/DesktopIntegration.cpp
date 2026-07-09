#include "platform/DesktopIntegration.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QUrl>

namespace macpad::platform {

ShellCommand revealArgsFor(const QString &path)
{
#if defined(Q_OS_MACOS)
    return {QStringLiteral("open"), {QStringLiteral("-R"), path}};
#elif defined(Q_OS_WIN)
    // explorer 的 /select, 參數怪癖：逗號緊接路徑、無空格，且路徑須為反斜線（native）。
    // 例：explorer.exe /select,C:\dir\file.txt
    return {QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,") + QDir::toNativeSeparators(path)}};
#else
    Q_UNUSED(path);
    return {};  // 其他平台：呼叫端改開所在目錄
#endif
}

ShellCommand terminalCommandFor(const QString &dir)
{
#if defined(Q_OS_MACOS)
    return {QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), dir}};
#elif defined(Q_OS_WIN)
    // 首選 Windows Terminal；-d 指定起始目錄。失敗時由 openInTerminal 退回 cmd。
    return {QStringLiteral("wt.exe"), {QStringLiteral("-d"), QDir::toNativeSeparators(dir)}};
#else
    Q_UNUSED(dir);
    return {};
#endif
}

QString defaultMonospaceFamily()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("Menlo");
#elif defined(Q_OS_WIN)
    // 依序挑選 Windows 上實際存在的等寬字型；避免硬編不存在的家族名稱。
    const QStringList candidates = {QStringLiteral("Cascadia Mono"),
                                    QStringLiteral("Consolas"),
                                    QStringLiteral("Courier New")};
    const QStringList families = QFontDatabase::families();
    for (const QString &c : candidates) {
        if (families.contains(c, Qt::CaseInsensitive))
            return c;
    }
    return QStringLiteral("Courier New");
#else
    return QStringLiteral("Monospace");
#endif
}

void revealInFileManager(const QString &path)
{
    if (path.isEmpty())
        return;
    const ShellCommand cmd = revealArgsFor(path);
    if (cmd.isValid()) {
        // explorer 選取檔案後常回傳非零 exit code，屬正常；此處僅啟動、不判定成敗。
        QProcess::startDetached(cmd.program, cmd.args);
    } else {
        // 後備：開啟所在目錄
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void openInTerminal(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const ShellCommand cmd = terminalCommandFor(dir);
    if (!cmd.isValid())
        return;
#if defined(Q_OS_WIN)
    // 先試 Windows Terminal；未安裝（startDetached 回 false）則退回 cmd。
    if (QProcess::startDetached(cmd.program, cmd.args))
        return;
    const QString native = QDir::toNativeSeparators(dir);
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            {QStringLiteral("/c"), QStringLiteral("start"),
                             QStringLiteral("cmd"), QStringLiteral("/k"),
                             QStringLiteral("cd"), QStringLiteral("/d"), native});
#else
    QProcess::startDetached(cmd.program, cmd.args);
#endif
}

void openInApp(const QString &appName, const QString &path)
{
    if (path.isEmpty())
        return;
    if (appName.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"),
                            {QStringLiteral("-a"), appName, path});
#elif defined(Q_OS_WIN)
    // cmd start 的第一個引號參數為視窗標題（留空），再帶應用與檔案路徑。
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            {QStringLiteral("/c"), QStringLiteral("start"),
                             QString(), appName, QDir::toNativeSeparators(path)});
#else
    Q_UNUSED(appName);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

}  // namespace macpad::platform
