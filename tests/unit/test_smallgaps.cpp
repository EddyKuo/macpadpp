// 單元測試：零散小缺口補強——集中補齊各模組「錯誤處理與邊界情況」的未覆蓋路徑。
//
// 涵蓋範圍（每項只補既有測試沒碰到的分支，不重複既有斷言）：
//   features/update/UpdateChecker         —— check() 的完整回應解析與錯誤處理
//   extension/builtin/MarkdownPreview     —— 面板的編輯器改接、延後渲染、選單動作
//   platform/DesktopIntegration           —— 三個動作函式的早退與啟動路徑
//   persistence/SettingsStore             —— 列舉序列化的所有分支與非法值回退
//   persistence/ThemeStore                —— 載入/匯入/匯出的失敗路徑與 NPP XML 匯入
//   core/FileEncoding                     —— UTF-16 BOM、非法列舉、codec 名稱回退
//   features/autocomplete/ApiDatabase     —— 外部 API 檔合併、內建語言關鍵字字典
//   features/findinfiles/FindInFilesEngine—— 排除規則、取消、無法讀取、二進位檔略過
//
// 網路：UpdateChecker 全程不連外網——回應由自製的 QAbstractNetworkCache 直接供給，
//       並額外把應用層 proxy 指向本機關閉的埠，任何漏網請求都會被本機立刻拒絕。
// 行程：DesktopIntegration 不真的啟動任何應用程式——測試前把 PATH 換成空目錄，
//       使 QProcess 找不到 open/explorer 而啟動失敗；file: URL 則以自訂 handler 攔截。
#include <QtTest>

#include <QAbstractNetworkCache>
#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QWebEngineView>

#include "core/EditorWidget.h"
#include "core/FileEncoding.h"
#include "extension/builtin/MarkdownPreviewExtension.h"
#include "features/autocomplete/ApiDatabase.h"
#include "features/autocomplete/ApiFileStore.h"
#include "features/findinfiles/FindInFilesEngine.h"
#include "features/update/UpdateChecker.h"
#include "persistence/AppPaths.h"
#include "persistence/SettingsStore.h"
#include "persistence/ThemeStore.h"
#include "platform/DesktopIntegration.h"

using macpad::core::Encoding;
using macpad::core::Eol;
using macpad::core::FileEncoding;
using macpad::features::ApiDatabase;
using macpad::features::ApiFileStore;
using macpad::features::FindInFilesEngine;
using macpad::features::FindInFilesOptions;
using macpad::features::UpdateChecker;
using namespace macpad::persistence;

// ── 假的網路快取：讓 UpdateChecker::check() 完全不觸網也能拿到回應 ─────────
// QNetworkAccessManager 在快取項目仍「新鮮」時會直接回放快取內容（預設的
// PreferNetwork 策略即為「快取過期才走網路」），因此不需要任何伺服器。
class FakeReplyCache : public QAbstractNetworkCache {
public:
    explicit FakeReplyCache(const QByteArray &payload, QObject *parent = nullptr)
        : QAbstractNetworkCache(parent), m_payload(payload) {}

    QNetworkCacheMetaData metaData(const QUrl &url) override
    {
        QNetworkCacheMetaData md;
        md.setUrl(url);
        md.setSaveToDisk(true);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        md.setLastModified(now.addSecs(-10));
        md.setExpirationDate(now.addDays(1));   // 仍新鮮 → 不會走網路
        QNetworkCacheMetaData::RawHeaderList headers;
        headers << qMakePair(QByteArray("Content-Type"), QByteArray("application/json"));
        headers << qMakePair(QByteArray("Date"), now.toString(Qt::RFC2822Date).toUtf8());
        md.setRawHeaders(headers);
        QNetworkCacheMetaData::AttributesMap attrs;
        attrs[QNetworkRequest::HttpStatusCodeAttribute] = 200;
        attrs[QNetworkRequest::HttpReasonPhraseAttribute] = QByteArray("OK");
        md.setAttributes(attrs);
        return md;
    }
    void updateMetaData(const QNetworkCacheMetaData &) override {}
    QIODevice *data(const QUrl &) override
    {
        auto *buf = new QBuffer;
        buf->setData(m_payload);
        buf->open(QIODevice::ReadOnly);
        return buf;
    }
    bool remove(const QUrl &) override { return true; }
    qint64 cacheSize() const override { return m_payload.size(); }
    QIODevice *prepare(const QNetworkCacheMetaData &) override { return nullptr; }
    void insert(QIODevice *) override {}
    void clear() override {}

private:
    QByteArray m_payload;
};

// ── 假宿主：Markdown 預覽面板需要 activeEditor 與 hostWindow ─────────────
class StubHost : public macpad::extension::IHostServices {
public:
    macpad::core::EditorWidget *activeEditor() override { return editor; }
    void addMenuAction(const QString &menu, const QString &text,
                       std::function<void()> cb) override
    {
        menus << menu;
        texts << text;
        callbacks.push_back(std::move(cb));
    }
    void showStatusMessage(const QString &msg, int) override { lastStatus = msg; }
    QWidget *hostWindow() override { return window; }

    macpad::core::EditorWidget *editor = nullptr;
    QWidget *window = nullptr;
    QStringList menus;
    QStringList texts;
    std::vector<std::function<void()>> callbacks;
    QString lastStatus;
};

// 攔截 file: URL 的開啟請求，避免測試真的叫起系統應用程式。
class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void onUrl(const QUrl &u) { urls << u; }
};

class TestSmallGaps : public QObject {
    Q_OBJECT

    // 依 UpdateChecker 內部持有的 QNetworkAccessManager 掛上假快取
    static void installCache(UpdateChecker *checker, const QByteArray &payload)
    {
        auto *nam = checker->findChild<QNetworkAccessManager *>();
        QVERIFY(nam);
        nam->setCache(new FakeReplyCache(payload));
    }

    // 送出一次查詢並取回 finished() 的參數（逾時視為失敗，絕不無限等待）
    static QList<QVariant> runCheck(UpdateChecker *checker, const QString &currentVersion)
    {
        QSignalSpy spy(checker, &UpdateChecker::finished);
        checker->check(currentVersion);
        if (!spy.wait(15000))
            return {};
        return spy.at(0);
    }

    static void writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
        f.close();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        m_config.reset(new QTemporaryDir);
        QVERIFY(m_config->isValid());
        AppPaths::setConfigDirOverride(m_config->path());

        // 保險絲：任何真的走上網路的請求都會被導向本機的關閉埠而立刻失敗，
        // 測試永遠不會連到外部服務，也不會卡在 DNS/連線等待。
        QNetworkProxy::setApplicationProxy(
            QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1));
    }

    void cleanupTestCase()
    {
        AppPaths::setConfigDirOverride(QString());
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

    // ══════════════ UpdateChecker ══════════════

    // 完整回應：解析 tag/html_url/assets，並判定有新版
    void updateCheckParsesReleaseJson()
    {
        UpdateChecker checker;
        installCache(&checker,
                     "{\"tag_name\":\"v9.9.9\",\"html_url\":\"https://example.invalid/rel\","
                     "\"assets\":["
                     "{\"name\":\"macpad++-9.9.9-arm64.dmg\","
                     " \"browser_download_url\":\"https://example.invalid/a.dmg\","
                     " \"size\":1234},"
                     "{\"name\":\"macpad++-9.9.9-x64.zip\","
                     " \"browser_download_url\":\"https://example.invalid/a.zip\","
                     " \"size\":4321}]}");
        const QList<QVariant> r = runCheck(&checker, QStringLiteral("0.1.0"));
        QCOMPARE(r.size(), 6);
        QVERIFY2(r.at(0).toBool(), "0.1.0 < 9.9.9 應判定為有新版");
        QCOMPARE(r.at(1).toString(), QStringLiteral("v9.9.9"));
        QCOMPARE(r.at(2).toString(), QStringLiteral("https://example.invalid/rel"));
        QVERIFY(r.at(3).toString().isEmpty());   // 成功時 errorText 必須為空
#if defined(Q_OS_MACOS)
        QCOMPARE(r.at(4).toString(), QStringLiteral("https://example.invalid/a.dmg"));
        QCOMPARE(r.at(5).toString(), QStringLiteral("1234"));
#elif defined(Q_OS_WIN)
        QCOMPARE(r.at(4).toString(), QStringLiteral("https://example.invalid/a.zip"));
        QCOMPARE(r.at(5).toString(), QStringLiteral("4321"));
#endif
    }

    // 目前版本已是最新：仍要回報版本資訊，但 hasUpdate 為 false
    void updateCheckReportsNoUpdateWhenCurrent()
    {
        UpdateChecker checker;
        installCache(&checker, "{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://example.invalid/r\"}");
        const QList<QVariant> r = runCheck(&checker, QStringLiteral("1.0.0"));
        QCOMPARE(r.size(), 6);
        QVERIFY(!r.at(0).toBool());
        QCOMPARE(r.at(1).toString(), QStringLiteral("v1.0.0"));
        QVERIFY(r.at(3).toString().isEmpty());
        QVERIFY(r.at(4).toString().isEmpty());   // 無 assets → 無平台安裝檔
    }

    // 回應不是 JSON 物件（陣列或垃圾）→ 明確錯誤，不臆測
    void updateCheckRejectsNonObjectJson()
    {
        UpdateChecker checker;
        installCache(&checker, "[1,2,3]");
        const QList<QVariant> r = runCheck(&checker, QStringLiteral("1.0.0"));
        QCOMPARE(r.size(), 6);
        QVERIFY(!r.at(0).toBool());
        QVERIFY2(!r.at(3).toString().isEmpty(), "格式無法解析必須回報錯誤訊息");
        QVERIFY(r.at(1).toString().isEmpty());
    }

    // 回應是物件但沒有 tag_name → 視為錯誤（不能拿空版本去比較）
    void updateCheckRejectsMissingTag()
    {
        UpdateChecker checker;
        installCache(&checker, "{\"html_url\":\"https://example.invalid/r\"}");
        const QList<QVariant> r = runCheck(&checker, QStringLiteral("1.0.0"));
        QCOMPARE(r.size(), 6);
        QVERIFY(!r.at(0).toBool());
        QVERIFY2(!r.at(3).toString().isEmpty(), "缺版本資訊必須回報錯誤訊息");
    }

    // 連線失敗（本機 proxy 拒絕）→ errorText 帶出 QNetworkReply 的說明
    void updateCheckReportsNetworkError()
    {
        UpdateChecker checker;   // 不掛快取 → 只能走網路 → 被本機 proxy 立刻拒絕
        const QList<QVariant> r = runCheck(&checker, QStringLiteral("1.0.0"));
        QCOMPARE(r.size(), 6);
        QVERIFY(!r.at(0).toBool());
        QVERIFY2(!r.at(3).toString().isEmpty(), "網路錯誤必須回報 errorString");
        QVERIFY(r.at(1).toString().isEmpty());
    }

    // 副檔名相符但沒有下載網址的 asset 必須被跳過（不能回傳空網址當成功）
    void pickAssetSkipsEntryWithoutUrl()
    {
        const QJsonDocument doc = QJsonDocument::fromJson(
            "[{\"name\":\"broken-arm64.dmg\",\"size\":10},"
            " {\"name\":\"broken-x64.zip\",\"size\":10},"
            " {\"name\":\"good-arm64.dmg\",\"browser_download_url\":\"https://x/g.dmg\","
            "  \"size\":7},"
            " {\"name\":\"good-x64.zip\",\"browser_download_url\":\"https://x/g.zip\","
            "  \"size\":7}]");
        QString size;
        const QString url = UpdateChecker::pickAssetForPlatform(doc.array(), &size);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QVERIFY2(!url.isEmpty(), "應跳過無網址者並採用後面合法的 asset");
        QVERIFY(url.endsWith(QLatin1String("g.dmg")) || url.endsWith(QLatin1String("g.zip")));
        QCOMPARE(size, QStringLiteral("7"));
#else
        QVERIFY(url.isEmpty());
#endif
    }

    // ══════════════ MarkdownPreviewExtension ══════════════

    void markdownPreviewDockLifecycle()
    {
        QMainWindow mw;
        macpad::core::EditorWidget editor;
        StubHost host;
        host.window = &mw;
        host.editor = &editor;

        macpad::extension::MarkdownPreviewExtension ext;
        const auto caps = ext.capabilities();
        QCOMPARE(caps.id, QStringLiteral("builtin.markdownpreview"));
        ext.onLoad(&host);

        auto *dock = mw.findChild<QDockWidget *>(QStringLiteral("MarkdownPreviewDock"));
        QVERIFY2(dock, "onLoad 應在宿主視窗掛上預覽面板");
        QVERIFY(host.texts.contains(QStringLiteral("Markdown Preview")));
        QVERIFY(!dock->isVisible());   // 預設隱藏，由選單動作叫出

        mw.show();
        QVERIFY(!host.callbacks.empty());
        host.callbacks.front()();      // 選單動作：show + raise + refresh
        QVERIFY2(dock->isVisible(), "選單動作應叫出面板");

        auto *view = dock->findChild<QWebEngineView *>();
        QVERIFY(view);

        // 明確驅動 loadFinished，讓「頁面未就緒 → 暫存 → 就緒後補渲染」可被確定性地測到
        QVERIFY(QMetaObject::invokeMethod(view, "loadFinished", Q_ARG(bool, false)));

        editor.setText(QStringLiteral("# 標題\n\n內文 with \"quotes\" and \\backslash\\"));
        QVERIFY(QMetaObject::invokeMethod(dock, "pollActiveEditor"));   // 接上作用中編輯器
        QVERIFY(QMetaObject::invokeMethod(dock, "refresh"));            // 未就緒 → 只暫存
        QVERIFY(QMetaObject::invokeMethod(view, "loadFinished", Q_ARG(bool, true)));
        QVERIFY(QMetaObject::invokeMethod(dock, "refresh"));            // 就緒 → 直接渲染

        // 編輯器換成 nullptr：舊連線要被斷開，且不得崩潰
        host.editor = nullptr;
        QVERIFY(QMetaObject::invokeMethod(dock, "pollActiveEditor"));

        // 面板隱藏時輪詢應直接早退（不去碰宿主）
        dock->hide();
        host.editor = &editor;
        QVERIFY(QMetaObject::invokeMethod(dock, "pollActiveEditor"));

        ext.onUnload();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(!mw.findChild<QDockWidget *>(QStringLiteral("MarkdownPreviewDock")),
                 "onUnload 後面板應被銷毀");

        // 卸載後選單動作若仍被觸發（宿主尚未移除該項），必須安靜地什麼都不做
        host.callbacks.front()();
        QVERIFY(true);
    }

    // 宿主未提供主視窗時不得建立面板（也不得崩潰）
    void markdownPreviewWithoutHostWindow()
    {
        StubHost host;   // window == nullptr
        macpad::extension::MarkdownPreviewExtension ext;
        ext.onLoad(&host);
        QVERIFY(host.texts.isEmpty());
        ext.onUnload();
    }

    // ══════════════ DesktopIntegration ══════════════

    // 空路徑/空目錄：三個動作函式都必須早退，不啟動任何行程
    void desktopActionsIgnoreEmptyInput()
    {
        macpad::platform::revealInFileManager(QString());
        macpad::platform::openInTerminal(QString());
        macpad::platform::openInApp(QStringLiteral("Whatever"), QString());
        QVERIFY(true);   // 沒有崩潰、沒有行程被啟動即為通過
    }

    // 未指定應用程式 → 交給系統預設開啟。以自訂 URL handler 攔截，不真的叫起應用程式。
    void openInAppWithoutAppNameUsesSystemHandler()
    {
        UrlCatcher catcher;
        QDesktopServices::setUrlHandler(QStringLiteral("file"), &catcher, "onUrl");
        const QString path = m_config->path() + QStringLiteral("/open-me.txt");
        writeFile(path, "x");

        macpad::platform::openInApp(QString(), path);
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));

        QCOMPARE(catcher.urls.size(), 1);
        QCOMPARE(catcher.urls.at(0).toLocalFile(), path);
    }

    // 啟動路徑：把 PATH 換成空目錄，讓 open/explorer 找不到而啟動失敗，
    // 藉此覆蓋「組指令 → 交給 QProcess」這段，同時保證不會真的開視窗。
    void desktopActionsStartDetachedPathIsExercised()
    {
        const QByteArray oldPath = qgetenv("PATH");
        QTemporaryDir emptyDir;
        QVERIFY(emptyDir.isValid());
        qputenv("PATH", emptyDir.path().toLocal8Bit());

        // 前置條件：確認在此 PATH 下真的找不到執行檔；否則直接放棄，
        // 絕不能冒著把 Finder/終端機叫起來的風險繼續。
        const bool neutralised =
            QStandardPaths::findExecutable(QStringLiteral("open")).isEmpty()
            && QStandardPaths::findExecutable(QStringLiteral("explorer.exe")).isEmpty()
            && QStandardPaths::findExecutable(QStringLiteral("cmd.exe")).isEmpty()
            && QStandardPaths::findExecutable(QStringLiteral("wt.exe")).isEmpty();
        if (!neutralised) {
            qputenv("PATH", oldPath);
            QSKIP("PATH 無法隔離，略過以避免真的啟動外部應用程式");
        }

        const QString file = m_config->path() + QStringLiteral("/reveal-me.txt");
        writeFile(file, "x");
        macpad::platform::revealInFileManager(file);
        macpad::platform::openInTerminal(m_config->path());
        macpad::platform::openInApp(QStringLiteral("NoSuchApp"), file);

        qputenv("PATH", oldPath);
        QVERIFY(!QStandardPaths::findExecutable(QStringLiteral("ls")).isEmpty());  // PATH 已復原
    }

    // ══════════════ SettingsStore ══════════════

    // 所有列舉值都必須能序列化並原樣讀回（涵蓋每個 enum 的所有分支）
    void settingsEnumRoundTripCoversAllValues()
    {
        Settings a;
        a.theme = ThemeMode::Light;
        a.backupMode = BackupMode::Simple;
        a.defaultEol = Eol::Cr;
        a.defaultEncoding = Encoding::Utf8Bom;
        a.toolbarIconSize = ToolbarIconSize::Small;
        a.edgeMode = EdgeMode::Line;
        a.foldMarginStyle = FoldMarginStyle::None;
        a.defaultDirPolicy = DefaultDirPolicy::RememberLast;
        a.multiInstanceMode = MultiInstanceMode::MonoInstance;
        a.fileStatusAutoDetect = FileStatusAutoDetectMode::Disabled;
        QVERIFY(SettingsStore::save(a));
        Settings r = SettingsStore::load();
        QVERIFY(r.theme == ThemeMode::Light);
        QVERIFY(r.backupMode == BackupMode::Simple);
        QVERIFY(r.defaultEol == Eol::Cr);
        QVERIFY(r.defaultEncoding == Encoding::Utf8Bom);
        QVERIFY(r.toolbarIconSize == ToolbarIconSize::Small);
        QVERIFY(r.edgeMode == EdgeMode::Line);
        QVERIFY(r.foldMarginStyle == FoldMarginStyle::None);
        QVERIFY(r.defaultDirPolicy == DefaultDirPolicy::RememberLast);
        QVERIFY(r.multiInstanceMode == MultiInstanceMode::MonoInstance);
        QVERIFY(r.fileStatusAutoDetect == FileStatusAutoDetectMode::Disabled);

        Settings b;
        b.defaultEncoding = Encoding::Utf16BE;
        b.toolbarIconSize = ToolbarIconSize::Large;
        b.edgeMode = EdgeMode::Background;
        b.foldMarginStyle = FoldMarginStyle::Arrow;
        b.defaultDirPolicy = DefaultDirPolicy::FixedPath;
        b.multiInstanceMode = MultiInstanceMode::AlwaysMulti;
        b.fileStatusAutoDetect = FileStatusAutoDetectMode::EnabledSilent;
        QVERIFY(SettingsStore::save(b));
        r = SettingsStore::load();
        QVERIFY(r.defaultEncoding == Encoding::Utf16BE);
        QVERIFY(r.toolbarIconSize == ToolbarIconSize::Large);
        QVERIFY(r.edgeMode == EdgeMode::Background);
        QVERIFY(r.foldMarginStyle == FoldMarginStyle::Arrow);
        QVERIFY(r.defaultDirPolicy == DefaultDirPolicy::FixedPath);
        QVERIFY(r.multiInstanceMode == MultiInstanceMode::AlwaysMulti);
        QVERIFY(r.fileStatusAutoDetect == FileStatusAutoDetectMode::EnabledSilent);

        Settings c;
        c.defaultEncoding = Encoding::Latin1;
        c.foldMarginStyle = FoldMarginStyle::Circle;
        c.defaultEol = Eol::CrLf;
        QVERIFY(SettingsStore::save(c));
        r = SettingsStore::load();
        QVERIFY(r.defaultEncoding == Encoding::Latin1);
        QVERIFY(r.foldMarginStyle == FoldMarginStyle::Circle);
        QVERIFY(r.defaultEol == Eol::CrLf);

        Settings d;
        d.foldMarginStyle = FoldMarginStyle::Box;
        d.backupMode = BackupMode::Verbose;
        d.theme = ThemeMode::Dark;
        QVERIFY(SettingsStore::save(d));
        r = SettingsStore::load();
        QVERIFY(r.foldMarginStyle == FoldMarginStyle::Box);
        QVERIFY(r.backupMode == BackupMode::Verbose);
        QVERIFY(r.theme == ThemeMode::Dark);
    }

    // 記憶體中出現非法列舉值（例如未來新增值被舊版讀到）時，序列化必須回退為預設值，
    // 而不是寫出無法解析的字串把設定檔弄壞。
    void settingsInvalidEnumFallsBackToDefault()
    {
        Settings s;
        s.theme = static_cast<ThemeMode>(99);
        s.backupMode = static_cast<BackupMode>(99);
        s.defaultEol = static_cast<Eol>(99);
        s.defaultEncoding = static_cast<Encoding>(99);
        s.toolbarIconSize = static_cast<ToolbarIconSize>(99);
        s.edgeMode = static_cast<EdgeMode>(99);
        s.foldMarginStyle = static_cast<FoldMarginStyle>(99);
        s.defaultDirPolicy = static_cast<DefaultDirPolicy>(99);
        s.multiInstanceMode = static_cast<MultiInstanceMode>(99);
        s.fileStatusAutoDetect = static_cast<FileStatusAutoDetectMode>(99);
        QVERIFY(SettingsStore::save(s));

        const Settings r = SettingsStore::load();
        QVERIFY(r.theme == ThemeMode::System);
        QVERIFY(r.backupMode == BackupMode::None);
        QVERIFY(r.defaultEol == Eol::Lf);
        QVERIFY(r.defaultEncoding == Encoding::Utf8);
        QVERIFY(r.toolbarIconSize == ToolbarIconSize::Standard);
        QVERIFY(r.edgeMode == EdgeMode::None);
        QVERIFY(r.foldMarginStyle == FoldMarginStyle::Simple);
        QVERIFY(r.defaultDirPolicy == DefaultDirPolicy::FollowCurrentDoc);
        QVERIFY(r.multiInstanceMode == MultiInstanceMode::MultiInstOnSession);
        QVERIFY(r.fileStatusAutoDetect == FileStatusAutoDetectMode::Enabled);
    }

    // 隱藏的工具列按鈕清單要 round-trip；空字串 id 不得被讀進來
    void settingsHiddenToolbarButtonsRoundTrip()
    {
        Settings s;
        s.hiddenToolbarButtons = QStringList{QStringLiteral("btn.new"), QString(),
                                             QStringLiteral("btn.print")};
        QVERIFY(SettingsStore::save(s));
        const Settings r = SettingsStore::load();
        QCOMPARE(r.hiddenToolbarButtons,
                 (QStringList{QStringLiteral("btn.new"), QStringLiteral("btn.print")}));

        Settings empty;
        QVERIFY(SettingsStore::save(empty));
        QVERIFY(SettingsStore::load().hiddenToolbarButtons.isEmpty());
    }

    // ══════════════ ThemeStore ══════════════

    void themeStoreFailurePaths()
    {
        QVERIFY(ThemeStore::load(QString()).name.isEmpty());     // 空名稱
        QVERIFY(!ThemeStore::remove(QString()));                 // 空名稱不得刪東西
        QVERIFY(!ThemeStore::exportToFile(QStringLiteral("NoSuchTheme"),
                                          m_config->path() + QStringLiteral("/out.json")));

        // 檔案不存在 / 內容非合法 JSON 物件 → 匯入失敗
        QVERIFY(!ThemeStore::importFromFile(m_config->path() + QStringLiteral("/missing.json")));
        const QString junk = m_config->path() + QStringLiteral("/junk.json");
        writeFile(junk, "not json at all");
        QVERIFY(!ThemeStore::importFromFile(junk));

        // 合法 JSON 但沒有 name → 無法決定檔名，必須拒絕
        const QString noName = m_config->path() + QStringLiteral("/noname.json");
        writeFile(noName, "{\"dark\":true}");
        QVERIFY(!ThemeStore::importFromFile(noName));

        // 主題檔內未存 name → 以檔名回填（容錯）
        const QString themeFile = AppPaths::configDir()
                                  + QStringLiteral("/themes/Nameless.json");
        QDir().mkpath(QFileInfo(themeFile).absolutePath());
        writeFile(themeFile, "{\"dark\":true}");
        const Theme t = ThemeStore::load(QStringLiteral("Nameless"));
        QCOMPARE(t.name, QStringLiteral("Nameless"));
        QVERIFY(t.dark);
        QVERIFY(ThemeStore::remove(QStringLiteral("Nameless")));
    }

    // Notepad++ stylers.xml：各 GlobalStyles 名稱的對應、非法顏色與非法 styleID 的容錯
    void themeFromNppXmlCoversAllWidgetNames()
    {
        const QByteArray xml =
            "<NotepadPlus><GlobalStyles>"
            "<WidgetStyle name=\"Default Style\" fgColor=\"E0E0E0\"/>"
            "<WidgetStyle name=\"Edge colour\" fgColor=\"404040\"/>"
            "<WidgetStyle name=\"Current line number\" fgColor=\"C0C0C0\"/>"
            "<WidgetStyle name=\"Fold margin\" fgColor=\"111111\"/>"
            "<WidgetStyle name=\"Fold active\" fgColor=\"222222\"/>"
            "<WidgetStyle name=\"White space symbol\" fgColor=\"333333\"/>"
            "<WidgetStyle name=\"Bookmark margin\" fgColor=\"444444\"/>"
            "<WidgetStyle name=\"Bad brace colour\" fgColor=\"555555\"/>"
            "<WidgetStyle name=\"Mark colour\" fgColor=\"666666\"/>"
            "<WidgetStyle name=\"URL hovered\" fgColor=\"777777\"/>"
            "<WidgetStyle name=\"Indent guideline style\" fgColor=\"888888\"/>"
            "<WidgetStyle name=\"Caret colour\" fgColor=\"ZZZZZZ\"/>"   // 非法十六進位 → 不覆寫
            "<WidgetStyle name=\"Unmapped thing\" fgColor=\"999999\"/>"
            "</GlobalStyles><LexerStyles>"
            "<LexerType name=\"cpp\">"
            "<WordsStyle name=\"BAD\" styleID=\"abc\" fgColor=\"ABCDEF\"/>"   // styleID 非數字 → 略過
            "<WordsStyle name=\"COMMENT\" styleID=\"1\" fgColor=\"6A9955\" fontStyle=\"7\"/>"
            "</LexerType>"
            "</LexerStyles></NotepadPlus>";
        QString err;
        const Theme t = ThemeStore::themeFromNppXml(xml, QStringLiteral("Widgets"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(t.name, QStringLiteral("Widgets"));
        QCOMPARE(t.styles.global.editorFg, QStringLiteral("#E0E0E0"));
        QCOMPARE(t.styles.global.edgeColor, QStringLiteral("#404040"));
        QCOMPARE(t.styles.global.currentLineNumber, QStringLiteral("#C0C0C0"));
        QCOMPARE(t.styles.global.foldMargin, QStringLiteral("#111111"));
        QCOMPARE(t.styles.global.foldActive, QStringLiteral("#222222"));
        QCOMPARE(t.styles.global.whitespaceFg, QStringLiteral("#333333"));
        QCOMPARE(t.styles.global.bookmarkMargin, QStringLiteral("#444444"));
        QCOMPARE(t.styles.global.badBrace, QStringLiteral("#555555"));
        QCOMPARE(t.styles.global.markColor, QStringLiteral("#666666"));
        QCOMPARE(t.styles.global.urlHovered, QStringLiteral("#777777"));
        QCOMPARE(t.styles.global.indentGuide, QStringLiteral("#888888"));
        QVERIFY2(t.styles.global.caretColor.isEmpty(), "非法顏色不得被採用");
        // 沒有背景色可判斷 → 不能亂猜成深色
        QVERIFY(!t.dark);
        // 非數字 styleID 的那筆必須被丟掉，只留合法的 COMMENT
        QCOMPARE(t.styles.byLang.value(QStringLiteral("cpp")).size(), 1);
        const StyleOverride so = t.styles.byLang.value(QStringLiteral("cpp")).at(0);
        QCOMPARE(so.style, 1);
        QVERIFY(so.bold && so.italic && so.underline);   // fontStyle=7 → 三者皆開
    }

    // 從檔案匯入 NPP 主題：不存在的檔案要帶出錯誤訊息；合法檔案要落地成主題
    void importFromNppXmlFile()
    {
        QString err;
        QVERIFY(!ThemeStore::importFromNppXmlFile(
            m_config->path() + QStringLiteral("/no-such-theme.xml"), &err));
        QVERIFY2(!err.isEmpty(), "開檔失敗必須回報原因");

        const QString path = m_config->path() + QStringLiteral("/Deepish.xml");
        writeFile(path,
                  "<NotepadPlus><GlobalStyles>"
                  "<WidgetStyle name=\"Global override\" fgColor=\"DCDCDC\" bgColor=\"1E1E1E\"/>"
                  "</GlobalStyles></NotepadPlus>");
        err.clear();
        QVERIFY2(ThemeStore::importFromNppXmlFile(path, &err), qPrintable(err));
        QVERIFY(ThemeStore::listThemes().contains(QStringLiteral("Deepish")));
        const Theme t = ThemeStore::load(QStringLiteral("Deepish"));
        QVERIFY2(t.dark, "深色背景應被判定為 dark 主題");
        QCOMPARE(t.styles.global.editorBg, QStringLiteral("#1E1E1E"));
        QVERIFY(ThemeStore::remove(QStringLiteral("Deepish")));

        // 不是 NPP 主題的 XML → 匯入失敗且不落地
        const QString bad = m_config->path() + QStringLiteral("/NotATheme.xml");
        writeFile(bad, "<root><x/></root>");
        err.clear();
        QVERIFY(!ThemeStore::importFromNppXmlFile(bad, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!ThemeStore::listThemes().contains(QStringLiteral("NotATheme")));
    }

    // ══════════════ FileEncoding ══════════════

    void utf16BeBomIsDetectedAndDecoded()
    {
        const QByteArray be = QByteArray("\xFE\xFF", 2)
                              + QByteArray("\x00H\x00i", 4);
        const macpad::core::DetectResult r = FileEncoding::detect(be);
        QVERIFY(r.encoding == Encoding::Utf16BE);
        QVERIFY(r.hasBom);
        QVERIFY(r.declaredCharset.isEmpty());   // 有 BOM 時不再嗅探文字宣告
        QCOMPARE(FileEncoding::decode(be, Encoding::Utf16BE), QStringLiteral("Hi"));

        // LE 亦同：顯式解碼時前導 BOM 要被剝掉
        const QByteArray le = QByteArray("\xFF\xFE", 2) + QByteArray("H\x00i\x00", 4);
        QCOMPARE(FileEncoding::decode(le, Encoding::Utf16LE), QStringLiteral("Hi"));
    }

    void encodeDecodeCoversEveryEncoding()
    {
        const QString text = QStringLiteral("Aé中");
        QVERIFY(FileEncoding::encode(text, Encoding::Utf16LE).startsWith(QByteArray("\xFF\xFE", 2)));
        QVERIFY(FileEncoding::encode(text, Encoding::Utf16BE).startsWith(QByteArray("\xFE\xFF", 2)));
        QCOMPARE(FileEncoding::decode(FileEncoding::encode(text, Encoding::Utf16LE),
                                      Encoding::Utf16LE), text);
        QCOMPARE(FileEncoding::decode(FileEncoding::encode(text, Encoding::Utf16BE),
                                      Encoding::Utf16BE), text);
        // Latin1 無法表示的字元會被替換，但長度與可表示的部分必須正確
        const QByteArray latin1 = FileEncoding::encode(text, Encoding::Latin1);
        QCOMPARE(latin1.size(), 3);
        QCOMPARE(latin1.at(0), 'A');

        // 非法列舉值（例如設定檔被手改壞）必須回退為 UTF-8，不得崩潰
        const Encoding bogus = static_cast<Encoding>(99);
        QCOMPARE(FileEncoding::encode(text, bogus), text.toUtf8());
        QCOMPARE(FileEncoding::decode(text.toUtf8(), bogus), text);
        QCOMPARE(FileEncoding::encodingName(bogus), QStringLiteral("UTF-8"));
    }

    void encodingAndEolNames()
    {
        QCOMPARE(FileEncoding::encodingName(Encoding::Utf8Bom), QStringLiteral("UTF-8 BOM"));
        QCOMPARE(FileEncoding::encodingName(Encoding::Utf16LE), QStringLiteral("UTF-16 LE"));
        QCOMPARE(FileEncoding::encodingName(Encoding::Utf16BE), QStringLiteral("UTF-16 BE"));
        QCOMPARE(FileEncoding::eolName(Eol::Lf), QStringLiteral("LF"));
        QCOMPARE(FileEncoding::eolName(Eol::CrLf), QStringLiteral("CRLF"));
        QCOMPARE(FileEncoding::eolName(Eol::Cr), QStringLiteral("CR"));
        QCOMPARE(FileEncoding::eolName(static_cast<Eol>(99)), QStringLiteral("LF"));
    }

    // 未知的 codec 名稱：解碼/編碼都必須安全回退為 UTF-8，而不是回傳空字串
    void unknownCodecFallsBackToUtf8()
    {
        const QString text = QStringLiteral("中文 test");
        const QByteArray utf8 = text.toUtf8();
        QCOMPARE(FileEncoding::decodeWithCodec(utf8, QStringLiteral("no-such-codec-42")), text);
        QCOMPARE(FileEncoding::encodeWithCodec(text, QStringLiteral("no-such-codec-42")), utf8);
        // 對照組：合法 codec 名稱走真正的轉換
        QCOMPARE(FileEncoding::decodeWithCodec(utf8, QStringLiteral("UTF-8")), text);
    }

    // ══════════════ ApiDatabase ══════════════

    // 使用者提供的外部 API 檔要與內建清單合併、去重、排序
    void externalApiFileMergesWithBuiltin()
    {
        const QString apiDir = AppPaths::configDir() + QStringLiteral("/apis");
        QDir().mkpath(apiDir);
        writeFile(apiDir + QStringLiteral("/cpp.xml"),
                  "<NotepadPlus><AutoComplete language=\"C++\">"
                  "<KeyWord name=\"printf\" func=\"yes\">"    // 與內建重複 → 去重
                  "<Overload retVal=\"int\" descr=\"外部覆寫\"></Overload></KeyWord>"
                  "<KeyWord name=\"myOwnHelper\" func=\"yes\">"
                  "<Overload retVal=\"void\" descr=\"\"><Param name=\"int x\"/></Overload>"
                  "</KeyWord>"
                  "</AutoComplete></NotepadPlus>");
        ApiFileStore::clearCache();

        const QStringList cpp = ApiDatabase::entriesFor(QStringLiteral("cpp"));
        QVERIFY2(cpp.contains(QStringLiteral("myOwnHelper")), "外部條目應被併入");
        QVERIFY(cpp.contains(QStringLiteral("std::sort")));   // 內建條目仍在
        QCOMPARE(cpp.count(QStringLiteral("printf")), 1);     // 重複只留一筆
        for (int i = 1; i < cpp.size(); ++i)                  // 必須是排序後的清單
            QVERIFY(cpp.at(i - 1).compare(cpp.at(i), Qt::CaseInsensitive) <= 0);

        // 外部檔提供的簽名優先於內建表
        const QStringList tips = ApiDatabase::callTipsFor(QStringLiteral("printf"),
                                                          QStringLiteral("cpp"));
        QCOMPARE(tips.size(), 1);
        QVERIFY2(tips.at(0).contains(QStringLiteral("外部覆寫")), qPrintable(tips.at(0)));
        // 外部檔沒提供的字仍回落到內建表
        QVERIFY(!ApiDatabase::callTipFor(QStringLiteral("std::sort"),
                                         QStringLiteral("cpp")).isEmpty());

        QFile::remove(apiDir + QStringLiteral("/cpp.xml"));
        ApiFileStore::clearCache();
    }

    // 沒有手寫清單、但有內建語言定義的語言（如 Dart）：以關鍵字群組當自動完成字典
    void builtinLanguageKeywordsBecomeEntries()
    {
        const QStringList dart = ApiDatabase::entriesFor(QStringLiteral("dart"));
        QVERIFY2(!dart.isEmpty(), "內建語言應以關鍵字群組提供自動完成");
        QVERIFY(dart.contains(QStringLiteral("class")));
        for (int i = 1; i < dart.size(); ++i)
            QVERIFY(dart.at(i - 1).compare(dart.at(i), Qt::CaseInsensitive) <= 0);
        QCOMPARE(dart.count(QStringLiteral("class")), 1);
    }

    // 完全沒有內建清單的語言，仍可完全靠使用者的外部 API 檔提供自動完成
    void unknownLanguageUsesExternalApiFileOnly()
    {
        const QString apiDir = AppPaths::configDir() + QStringLiteral("/apis");
        QDir().mkpath(apiDir);
        writeFile(apiDir + QStringLiteral("/mylang.xml"),
                  "<NotepadPlus><AutoComplete language=\"MyLang\">"
                  "<KeyWord name=\"zeta\"/><KeyWord name=\"alpha\"/><KeyWord name=\"alpha\"/>"
                  "</AutoComplete></NotepadPlus>");
        ApiFileStore::clearCache();

        const QStringList e = ApiDatabase::entriesFor(QStringLiteral("mylang"));
        QCOMPARE(e, (QStringList{QStringLiteral("alpha"), QStringLiteral("zeta")}));

        QFile::remove(apiDir + QStringLiteral("/mylang.xml"));
        ApiFileStore::clearCache();
    }

    // 路徑補完：目錄不存在時回空清單（不得崩潰、不得亂猜）
    void completePathOnMissingDirectoryIsEmpty()
    {
        QVERIFY(ApiDatabase::completePath(
                    m_config->path() + QStringLiteral("/definitely/missing/dir/pre")).isEmpty());
    }

    // ══════════════ FindInFilesEngine ══════════════

    // 排除規則的各種寫法：空白項、資料夾尾綴、去掉後變空的規則
    void excludeFilterEdgeCases()
    {
        using E = FindInFilesEngine;
        const QStringList filters{QStringLiteral("   "),       // 空白 → 略過
                                  QStringLiteral("!+\\"),      // 去掉標記後為空 → 略過
                                  QStringLiteral("node_modules\\"),  // 尾綴 '\' → 視為資料夾
                                  QStringLiteral("build/")};         // 尾綴 '/' → 視為資料夾
        QVERIFY(E::isExcluded(QStringLiteral("node_modules/x/a.js"),
                              QStringLiteral("a.js"), filters));
        QVERIFY(E::isExcluded(QStringLiteral("build/a.js"), QStringLiteral("a.js"), filters));
        QVERIFY(!E::isExcluded(QStringLiteral("src/a.js"), QStringLiteral("a.js"), filters));
        // 只有空白與空規則時，不得排除任何東西
        QVERIFY(!E::isExcluded(QStringLiteral("src/a.js"), QStringLiteral("a.js"),
                               QStringList{QStringLiteral("  "), QStringLiteral("!+\\")}));
    }

    // searchInText / replaceInText 的單檔入口：CRLF 行尾與不合法 regex
    void searchAndReplaceInTextEdgeCases()
    {
        FindInFilesOptions o;
        o.pattern = QStringLiteral("bar");
        const auto hits = FindInFilesEngine::searchInText(QStringLiteral("f.txt"),
                                                          QStringLiteral("foo\r\nbar baz\r\n"), o);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).line, 2);
        QCOMPARE(hits.at(0).column, 1);
        QCOMPARE(hits.at(0).lineText, QStringLiteral("bar baz"));   // 行尾 '\r' 必須被去掉

        FindInFilesOptions bad;
        bad.regex = true;
        bad.pattern = QStringLiteral("[unclosed");
        QVERIFY(FindInFilesEngine::searchInText(QStringLiteral("f.txt"),
                                                QStringLiteral("x"), bad).isEmpty());
        FindInFilesOptions empty;
        QVERIFY(FindInFilesEngine::searchInText(QStringLiteral("f.txt"),
                                                QStringLiteral("x"), empty).isEmpty());

        int count = -1;
        QCOMPARE(FindInFilesEngine::replaceInText(QStringLiteral("x"), bad,
                                                  QStringLiteral("y"), &count),
                 QStringLiteral("x"));
        QCOMPARE(count, 0);   // 不合法 regex 必須明確回報 0 次，不得留下舊值
    }

    // 目錄搜尋的略過條件：超大檔、隱藏路徑、排除規則、無法讀取、二進位檔、取消
    void searchSkipsFilesForEveryReason()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString base = root.path();
        QVERIFY(QDir().mkpath(base + QStringLiteral("/.hidden")));
        QVERIFY(QDir().mkpath(base + QStringLiteral("/skipme")));
        writeFile(base + QStringLiteral("/plain.txt"), "needle here\n");
        writeFile(base + QStringLiteral("/big.txt"), QByteArray(4096, 'x') + "needle\n");
        writeFile(base + QStringLiteral("/.hidden/h.txt"), "needle hidden\n");
        writeFile(base + QStringLiteral("/skipme/s.txt"), "needle skipped\n");
        writeFile(base + QStringLiteral("/bin.txt"), QByteArray("needle\0binary", 13));
        const QString unreadable = base + QStringLiteral("/locked.txt");
        writeFile(unreadable, "needle locked\n");
        QVERIFY(QFile::setPermissions(unreadable, QFile::Permissions()));
        const bool reallyLocked = !QFileInfo(unreadable).isReadable();

        FindInFilesOptions o;
        o.pattern = QStringLiteral("needle");
        o.maxFileBytes = 1024;                                   // big.txt 超標
        o.excludeFilters = QStringList{QStringLiteral("!+\\skipme")};

        const auto hits = FindInFilesEngine::search(base, o);
        QStringList names;
        for (const auto &m : hits)
            names << QFileInfo(m.filePath).fileName();
        QCOMPARE(names, QStringList{QStringLiteral("plain.txt")});
        QVERIFY(!names.contains(QStringLiteral("big.txt")));      // 超過單檔上限
        QVERIFY(!names.contains(QStringLiteral("h.txt")));        // 隱藏目錄
        QVERIFY(!names.contains(QStringLiteral("s.txt")));        // 排除規則
        QVERIFY(!names.contains(QStringLiteral("bin.txt")));      // 含 NUL 的二進位檔
        if (reallyLocked)
            QVERIFY(!names.contains(QStringLiteral("locked.txt")));

        // includeHidden = true → 隱藏目錄內容也要被搜到
        FindInFilesOptions withHidden = o;
        withHidden.includeHidden = true;
        bool sawHidden = false;
        for (const auto &m : FindInFilesEngine::search(base, withHidden))
            sawHidden = sawHidden || m.filePath.contains(QStringLiteral("/.hidden/"));
        QVERIFY(sawHidden);

        // 不合法 regex → 直接回空，不掃磁碟
        FindInFilesOptions bad;
        bad.regex = true;
        bad.pattern = QStringLiteral("(unclosed");
        QVERIFY(FindInFilesEngine::search(base, bad).isEmpty());

        // 取消旗標：一開始就設起來 → 一筆都不處理
        std::atomic<bool> cancel{true};
        QVERIFY(FindInFilesEngine::search(base, o, &cancel).isEmpty());

        QFile::setPermissions(unreadable, QFile::ReadOwner | QFile::WriteOwner);
    }

    // 指定檔案清單的搜尋：不存在的路徑、排除規則、無法讀取、二進位檔、取消
    void searchInFilesSkipsFilesForEveryReason()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString base = root.path();
        const QString good = base + QStringLiteral("/good.txt");
        const QString binary = base + QStringLiteral("/bin.dat");
        const QString excluded = base + QStringLiteral("/skip.min.js");
        const QString locked = base + QStringLiteral("/locked.txt");
        writeFile(good, "needle\n");
        writeFile(binary, QByteArray("needle\0x", 8));
        writeFile(excluded, "needle\n");
        writeFile(locked, "needle\n");
        QVERIFY(QFile::setPermissions(locked, QFile::Permissions()));
        const bool reallyLocked = !QFileInfo(locked).isReadable();

        FindInFilesOptions o;
        o.pattern = QStringLiteral("needle");
        o.excludeFilters = QStringList{QStringLiteral("!*.min.js")};
        const QStringList list{good, binary, excluded, locked,
                               base + QStringLiteral("/missing.txt"), base};

        const auto hits = FindInFilesEngine::searchInFiles(list, o);
        if (reallyLocked)
            QCOMPARE(hits.size(), 1);
        QVERIFY(!hits.isEmpty());
        QCOMPARE(hits.at(0).filePath, good);

        FindInFilesOptions bad;
        bad.regex = true;
        bad.pattern = QStringLiteral("(unclosed");
        QVERIFY(FindInFilesEngine::searchInFiles(list, bad).isEmpty());

        std::atomic<bool> cancel{true};
        QVERIFY(FindInFilesEngine::searchInFiles(list, o, &cancel).isEmpty());

        QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner);
    }

    // 目錄取代：與搜尋相同的略過條件，且取代要以原編碼寫回
    void replaceInFilesSkipsFilesForEveryReason()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString base = root.path();
        QVERIFY(QDir().mkpath(base + QStringLiteral("/.hidden")));
        QVERIFY(QDir().mkpath(base + QStringLiteral("/vendor")));
        writeFile(base + QStringLiteral("/a.txt"), "old value\n");
        writeFile(base + QStringLiteral("/big.txt"), QByteArray(4096, 'x') + "old\n");
        writeFile(base + QStringLiteral("/.hidden/h.txt"), "old\n");
        writeFile(base + QStringLiteral("/vendor/v.txt"), "old\n");
        writeFile(base + QStringLiteral("/bin.dat"), QByteArray("old\0x", 5));
        const QString locked = base + QStringLiteral("/locked.txt");
        writeFile(locked, "old\n");
        QVERIFY(QFile::setPermissions(locked, QFile::Permissions()));

        FindInFilesOptions o;
        o.pattern = QStringLiteral("old");
        o.maxFileBytes = 1024;
        o.excludeFilters = QStringList{QStringLiteral("vendor/")};

        // 不合法 regex → 一個檔都不能動
        FindInFilesOptions bad;
        bad.regex = true;
        bad.pattern = QStringLiteral("(unclosed");
        const auto none = FindInFilesEngine::replaceInFiles(base, bad, QStringLiteral("new"));
        QCOMPARE(none.filesChanged, 0);

        // 取消旗標：一開始就設起來 → 一個檔都不能動
        std::atomic<bool> cancel{true};
        const auto cancelled =
            FindInFilesEngine::replaceInFiles(base, o, QStringLiteral("new"), &cancel);
        QCOMPARE(cancelled.filesChanged, 0);

        const auto r = FindInFilesEngine::replaceInFiles(base, o, QStringLiteral("new"));
        QCOMPARE(r.filesChanged, 1);          // 只有 a.txt 該被改
        QCOMPARE(r.replacements, 1);

        QFile f(base + QStringLiteral("/a.txt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("new value\n"));
        f.close();

        QFile h(base + QStringLiteral("/.hidden/h.txt"));
        QVERIFY(h.open(QIODevice::ReadOnly));
        QCOMPARE(h.readAll(), QByteArray("old\n"));   // 隱藏檔不得被改
        h.close();

        QFile v(base + QStringLiteral("/vendor/v.txt"));
        QVERIFY(v.open(QIODevice::ReadOnly));
        QCOMPARE(v.readAll(), QByteArray("old\n"));   // 被排除的目錄不得被改
        v.close();

        // includeHidden = true → 隱藏檔這次要被改到
        FindInFilesOptions withHidden = o;
        withHidden.includeHidden = true;
        const auto r2 = FindInFilesEngine::replaceInFiles(base, withHidden,
                                                          QStringLiteral("new"));
        QVERIFY(r2.filesChanged >= 1);
        QFile h2(base + QStringLiteral("/.hidden/h.txt"));
        QVERIFY(h2.open(QIODevice::ReadOnly));
        QCOMPARE(h2.readAll(), QByteArray("new\n"));
        h2.close();

        QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner);
    }

private:
    QScopedPointer<QTemporaryDir> m_config;
};

QTEST_MAIN(TestSmallGaps)
#include "test_smallgaps.moc"
