// macpad++ — 應用進入點
// 單機原生桌面程式（Qt6 + QScintilla）。無後端、無遠端網路、無資料庫。
// （單一實例協調使用本地 IPC：QLocalSocket，屬本機 domain socket，非網路連線。）
#include <QApplication>

#include "app/MainWindow.h"
#include "features/cli/CliArgs.h"
#include "persistence/SettingsStore.h"
#include "platform/SingleInstance.h"

static void openArgs(MainWindow &window, const QStringList &fileArgs)
{
    for (const QString &a : fileArgs) {
        const auto fa = macpad::features::CliArgs::parseFileArg(a);
        if (fa.line > 0)
            window.openFileAtLine(fa.path, fa.line, 1);
        else
            window.openFile(fa.path);
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("macpad++");
    // 不設 organizationName：讓設定路徑為 ~/Library/Application Support/macpad++/（不重複巢狀）
    app.setApplicationDisplayName("macpad++");

    const QStringList fileArgs = app.arguments().mid(1);

    // 單一/多執行個體（FR-034）
    const auto settings = macpad::persistence::SettingsStore::load();
    macpad::platform::SingleInstance instance(QStringLiteral("macpad++.instance"));
    if (settings.singleInstance && !instance.isPrimary()) {
        // 已有實例在跑：把檔案參數轉交後結束
        instance.sendToPrimary(fileArgs);
        return 0;
    }

    MainWindow window;

    // primary：收到後續實例的參數時開檔並前景
    QObject::connect(&instance, &macpad::platform::SingleInstance::messageReceived,
                     &window, [&window](const QStringList &args) {
        openArgs(window, args);
        window.show();
        window.raise();
        window.activateWindow();
    });

    window.show();
    openArgs(window, fileArgs);

    return app.exec();
}
