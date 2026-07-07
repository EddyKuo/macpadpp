// macpad++ — 應用進入點
// 單機原生桌面程式（Qt6 + QScintilla）。無後端、無遠端網路、無資料庫。
// （單一實例協調使用本地 IPC：QLocalSocket，屬本機 domain socket，非網路連線。）
#include <QApplication>
#include <QLocale>
#include <QTranslator>

#include "app/MainWindow.h"
#include "features/cli/CliArgs.h"
#include "persistence/SettingsStore.h"
#include "platform/SingleInstance.h"

// 依設定(或系統語系)決定介面語言碼,對應到內建 .qm。
static QString resolveLanguage(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;  // 使用者明確指定
    const QString sys = QLocale::system().name();          // 如 "zh_TW"、"ja_JP"
    if (sys.startsWith("zh_TW") || sys.startsWith("zh_Hant")) return QStringLiteral("zh_TW");
    if (sys.startsWith("zh"))                                return QStringLiteral("zh_CN");
    if (sys.startsWith("ja"))                                return QStringLiteral("ja");
    return QStringLiteral("en");
}

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

    const auto settings = macpad::persistence::SettingsStore::load();

    // 介面語言(i18n):載入對應 .qm 並安裝翻譯器(須在建立 MainWindow 前,選單才會被翻譯)。
    // i18n.qrc 編在靜態庫,須顯式初始化資源。
    Q_INIT_RESOURCE(i18n);
    const QString lang = resolveLanguage(settings.language);
    static QTranslator translator;
    if (translator.load(QStringLiteral(":/i18n/macpad_%1.qm").arg(lang)))
        app.installTranslator(&translator);

    // 單一/多執行個體（FR-034）
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
