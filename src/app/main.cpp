// macpad++ — 應用進入點
// 單機原生桌面程式（Qt6 + QScintilla）。無後端、無遠端網路、無資料庫。
// （單一實例協調使用本地 IPC：QLocalSocket，屬本機 domain socket，非網路連線。）
#include <QApplication>
#include <QLocale>
#include <QTranslator>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "core/LexerFactory.h"
#include "features/cli/CliArgs.h"
#include "persistence/AppPaths.h"
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

// 依解析結果開啟所有檔案，並套用 -ro / -n<line> / -c<col>（FR-033, FR-051）
static void openParsedArgs(MainWindow &window, const macpad::features::ParsedArgs &parsed)
{
    for (const auto &fa : parsed.files) {
        if (fa.line > 0)
            window.openFileAtLine(fa.path, fa.line, qMax(1, fa.column));
        else
            window.openFile(fa.path);
        if (auto *e = window.activeEditor()) {
            if (parsed.readOnly)
                e->setReadOnly(true);
            // -l<lang>：強制指定語言（覆寫副檔名自動判斷）
            if (!parsed.forceLanguage.isEmpty()) {
                if (QsciLexer *lex =
                        macpad::core::LexerFactory::createForLanguage(parsed.forceLanguage, e))
                    e->setLanguageLexer(lex);
            }
        }
    }
    // -n<line>/-c<col>：對開啟後的作用中編輯器跳至指定行/欄（1-based）
    if (parsed.gotoLine > 0) {
        if (auto *e = window.activeEditor()) {
            const int line = qMax(0, parsed.gotoLine - 1);
            const int col = qMax(0, parsed.gotoColumn - 1);
            e->setCursorPosition(line, col);
            e->ensureLineVisible(line);
            e->setFocus();
        }
    }
    // -p<pos>：以原始字元位置定位（SCI_GOTOPOS）
    if (parsed.gotoPos > 0) {
        if (auto *e = window.activeEditor()) {
            e->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                             static_cast<unsigned long>(parsed.gotoPos));
            e->setFocus();
        }
    }
    // -openSession <file>：從指定 session 檔還原分頁（在開檔前後皆可，這裡於開檔後附加還原）
    if (!parsed.openSessionPath.isEmpty())
        window.openSessionFile(parsed.openSessionPath);
    // -openFoldersAsWorkspace <folder>（可重複）：各資料夾加入工作區
    for (const QString &folder : parsed.openFoldersAsWorkspace)
        window.addWorkspaceFolder(folder);
    // -notabbar：隱藏分頁列
    if (parsed.hideTabBar)
        window.setTabBarVisible(false);
    // -fullReadOnly：全域唯讀（較 -ro 嚴格，套用到所有已開啟分頁）
    if (parsed.fullReadOnly)
        window.setFullReadOnly(true);
    // -monitor：對所有已開啟且已存檔的分頁啟用外部異動監控
    if (parsed.monitorMode)
        window.enableMonitoringForOpenFiles();
    // -x <n> / -y <n>：視窗初始座標（兩者皆給定才移動）
    if (parsed.windowX.has_value() && parsed.windowY.has_value())
        window.move(parsed.windowX.value(), parsed.windowY.value());
    // -udl=<name>：對作用中編輯器套用指定的使用者定義語言（UDL）
    if (!parsed.udlName.isEmpty())
        window.applyUdlByName(parsed.udlName);
    // -notepadStyleCmdline：Notepad 風格多檔命令列語意（產品決策，暫不改變現行開檔行為）。
    // -uiLangCode（-L<code>）：介面語言於啟動前決定（見 main() 內 resolveLanguage）；此處不重載。
    // -alwaysOnTop / -title:/-titleAdd:：視窗層級選項（開檔後統一套用）
    window.applyCliWindowOptions(parsed.alwaysOnTop, parsed.titleAdd);
    // -quickPrint：直印目前檔案後保留開啟。
    // TODO(sprint5): 需接上預設印表機直印流程（QsciPrinter 免對話框列印），暫留待補。
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("macpad++");
    // 不設 organizationName：讓設定路徑為 ~/Library/Application Support/macpad++/（不重複巢狀）
    app.setApplicationDisplayName("macpad++");

    const QStringList rawArgs = app.arguments().mid(1);
    const macpad::features::ParsedArgs parsed = macpad::features::CliArgs::parse(rawArgs);

    // -settingsDir <dir>：自訂設定目錄——須在任何設定讀取前套用（否則會讀到預設位置）
    if (!parsed.settingsDir.isEmpty())
        macpad::persistence::AppPaths::setConfigDirOverride(parsed.settingsDir);

    const auto settings = macpad::persistence::SettingsStore::load();

    // 介面語言(i18n):載入對應 .qm 並安裝翻譯器(須在建立 MainWindow 前,選單才會被翻譯)。
    // i18n.qrc 編在靜態庫,須顯式初始化資源。
    // -L<langCode>（大寫）命令列指定介面語言優先於設定檔。
    Q_INIT_RESOURCE(i18n);
    const QString lang = resolveLanguage(
        !parsed.uiLangCode.isEmpty() ? parsed.uiLangCode : settings.language);
    static QTranslator translator;
    if (translator.load(QStringLiteral(":/i18n/macpad_%1.qm").arg(lang)))
        app.installTranslator(&translator);

    // 單一/多執行個體（FR-034, FR-051 -multiInst：一律以 primary 執行，略過轉送）
    macpad::platform::SingleInstance instance(QStringLiteral("macpad++.instance"));
    if (settings.singleInstance && !parsed.multiInstance && !instance.isPrimary()) {
        // 已有實例在跑：把原始參數轉交後結束
        instance.sendToPrimary(rawArgs);
        return 0;
    }

    // -nosession：略過本次啟動的 session 還原（FR-051）
    MainWindow window(nullptr, /*restoreSessionOnLaunch=*/!parsed.noSession);

    // primary：收到後續實例的參數時開檔並前景
    QObject::connect(&instance, &macpad::platform::SingleInstance::messageReceived,
                     &window, [&window](const QStringList &args) {
        openParsedArgs(window, macpad::features::CliArgs::parse(args));
        window.show();
        window.raise();
        window.activateWindow();
    });

    window.show();
    openParsedArgs(window, parsed);

    return app.exec();
}
