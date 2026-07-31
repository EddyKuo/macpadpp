// 單元測試：MainWindow_File.cpp——MainWindow 的檔案命令實作層
//（開檔/存檔/另存/存副本/改名/關閉/重新載入/最近關閉/垃圾桶/拖放/唯讀屬性/關窗前存檔提示）。
//
// 測試策略：
//  1. 這一層幾乎都是 private slot，測試類別不是 MainWindow 的 friend，
//     因此一律以 QMetaObject::invokeMethod 走 moc 為 private slot 產生的 invoke 入口，
//     或走公開入口（openFile / openFileNotepadStyle / 事件送入）。測到的是真正接在
//     UI 上的那條線，而不是繞過 UI 的內部捷徑。
//  2. 這些路徑大量使用 QFileDialog 與 QMessageBox。initTestCase() 設定
//     Qt::AA_DontUseNativeDialogs，讓對話框是 Qt 自己的 widget 版本而非 macOS 原生
//     NSOpenPanel——如此才能在事件迴圈中從外部 selectFile()+accept()，把「使用者真的
//     選了檔案」的完整成功路徑測出來，而不是只能測取消分支。
//  3. 所有 modal 都由 driveModals() 於事件迴圈中依序接手；另有一個全域看門狗
//     （installModalWatchdog）會強制關閉滯留過久的對話框並讓測試失敗，
//     任何情況下都不會把測試卡住。
//  4. 斷言看「磁碟上的檔案內容 / 分頁數 / dirty 狀態 / 狀態列訊息」的實際變化，
//     而不是只確認呼叫沒崩潰。
//  5. 所有檔案操作一律在 QTemporaryDir 內；設定目錄以 AppPaths::setConfigDirOverride()
//     導向暫存目錄，絕不觸碰使用者真實的 ~/Library/Application Support/macpad++。
#include <QtTest>

#include <memory>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "features/backup/BackupService.h"
#include "features/langs/BuiltinLanguages.h"
#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlLexer.h"
#include "features/udl/UdlManager.h"
#include "persistence/AppPaths.h"
#include "persistence/RecentFiles.h"
#include "persistence/SettingsStore.h"

using macpad::core::EditorWidget;
using macpad::persistence::Settings;
using macpad::persistence::SettingsStore;

// ---------------------------------------------------------------------------
// modal 驅動與看門狗
// ---------------------------------------------------------------------------

// 看門狗曾經開火＝有對話框沒人接手（測試設計有誤），於 cleanupTestCase 斷言為 false。
static bool g_watchdogFired = false;

// 全域看門狗：任何 modal 對話框連續滯留超過約 3 秒就強制關閉。
// 沒有它，一個沒被預期到的對話框會讓整個測試程序永久停在 exec() 裡。
static void installModalWatchdog()
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(100);
    auto *seen = new QWidget *(nullptr);      // 上一輪看到的 modal
    auto *ticks = new int(0);                 // 同一個 modal 連續出現的輪數
    QObject::connect(timer, &QTimer::timeout, timer, [seen, ticks] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            *seen = nullptr;
            *ticks = 0;
            return;
        }
        if (modal != *seen) {
            *seen = modal;
            *ticks = 0;
            return;
        }
        if (++(*ticks) >= 30) {   // 約 3 秒
            g_watchdogFired = true;
            if (auto *dlg = qobject_cast<QDialog *>(modal))
                dlg->reject();
            else
                modal->close();
            *seen = nullptr;
            *ticks = 0;
        }
    });
    timer->start();
}

using ModalHandler = std::function<void(QWidget *)>;

// 依序接手接下來的 N 個 modal 對話框：handlers[i] 處理第 i 個出現的對話框，
// 每個 handler 負責把它關掉（沒關就強制收尾）。回傳「實際接手了幾個」的計數器，
// 測試據以斷言「該出現的對話框真的出現了 / 不該出現的沒出現」。
//
// 為何要排隊而不是一次掛一個：像「另存對話框 → 寫入失敗警告」這種連續兩個 modal，
// 第一個 exec() 返回到第二個 exec() 之間不會跑事件迴圈，事後再掛 handler 一定來不及，
// 只會讓第二個 handler 誤抓到第一個對話框。
//
// handler 用完就停止輪詢，不干擾後續測試；若之後仍冒出沒人接手的對話框，
// 由全域看門狗強制關閉並讓測試失敗。timeoutMs 內沒有任何對話框則放棄輪詢。
// 同時只允許一個 driver 存在：上一個「預期不會有對話框」的 driver 若還在輪詢，
// 會誤抓下一步才該出現的對話框（測試之間也會互相污染），開新的之前一律先收掉。
static QPointer<QTimer> g_modalDriver;

static void stopModalDrivers()
{
    if (g_modalDriver) {
        g_modalDriver->stop();
        delete g_modalDriver.data();
    }
}

static std::shared_ptr<int> driveModals(const QList<ModalHandler> &handlers,
                                        int timeoutMs = 3000)
{
    stopModalDrivers();
    auto fired = std::make_shared<int>(0);
    auto idx = std::make_shared<int>(0);
    auto *timer = new QTimer;
    g_modalDriver = timer;
    timer->setInterval(5);
    const QDeadlineTimer deadline(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, timer,
                     [timer, handlers, fired, idx, deadline] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            if (deadline.hasExpired()) {
                timer->stop();
                timer->deleteLater();
            }
            return;
        }
        if (*idx >= handlers.size()) {          // 不該出現的對話框
            g_watchdogFired = true;
        } else {
            handlers.at(*idx)(modal);
            ++(*idx);
            ++(*fired);
        }
        if (modal->isVisible()) {
            if (auto *dlg = qobject_cast<QDialog *>(modal))
                dlg->reject();
            else
                modal->close();
        }
        if (*idx >= handlers.size()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
    return fired;
}

// QFileDialog：填入檔名後 accept。DontConfirmOverwrite 避免覆寫確認再彈第二個 modal
//（那屬於 Qt 自身行為，不是本檔要測的邏輯）。
static void acceptFileDialog(QWidget *modal, const QString &path)
{
    auto *fd = qobject_cast<QFileDialog *>(modal);
    QVERIFY2(fd, "預期出現檔案對話框，實際卻是別的 modal 視窗");
    fd->setOption(QFileDialog::DontConfirmOverwrite, true);
    fd->selectFile(path);   // 這一步會把對話框切到目標目錄

    // QFileDialog 取結果時「檔案清單的選取」優先於檔名輸入框：對話框以既有檔案為起始路徑
    // 時（例如 Rename 帶入原檔名），那一列會被預先選取，於是不論輸入框填什麼都拿回原檔名。
    // 而清單是非同步填充的，預選是否成立取決於時序——正是間歇性失敗的來源。
    // 因此明確清掉選取，再直接把檔名寫進輸入框，讓結果只由我們指定的名字決定。
    const auto views = fd->findChildren<QAbstractItemView *>();
    for (QAbstractItemView *v : views) {
        if (v->selectionModel())
            v->selectionModel()->clearSelection();
    }
    auto *nameEdit = fd->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    QVERIFY2(nameEdit, "找不到 QFileDialog 的檔名輸入框");
    nameEdit->setText(QFileInfo(path).fileName());

    QVERIFY2(!fd->selectedFiles().isEmpty(), "檔案對話框沒有選到任何檔案");
    QCOMPARE(QFileInfo(fd->selectedFiles().constFirst()).fileName(),
             QFileInfo(path).fileName());
    // QFileDialog 把 accept() 收成 protected；透過 QDialog 介面呼叫同一個虛擬函式
    static_cast<QDialog *>(fd)->accept();
}

// QMessageBox：點下指定的標準按鈕
static void clickMessageBox(QWidget *modal, QMessageBox::StandardButton which)
{
    auto *mb = qobject_cast<QMessageBox *>(modal);
    QVERIFY2(mb, "預期出現訊息框，實際卻是別的 modal 視窗");
    QAbstractButton *b = mb->button(which);
    if (!b) {
        QStringList names;
        const auto btns = mb->buttons();
        for (const QAbstractButton *x : btns)
            names << x->text();
        QFAIL(qPrintable(QStringLiteral("訊息框沒有預期的按鈕；標題=%1 內容=%2 按鈕=[%3]")
                             .arg(mb->windowTitle(), mb->text(), names.join(','))));
    }
    b->click();
}

// 便利包裝
static std::function<void(QWidget *)> pickFile(const QString &path)
{
    return [path](QWidget *m) { acceptFileDialog(m, path); };
}

static std::function<void(QWidget *)> cancelModal()
{
    return [](QWidget *m) {
        if (auto *dlg = qobject_cast<QDialog *>(m))
            dlg->reject();
        else
            m->close();
    };
}

static std::function<void(QWidget *)> clickButton(QMessageBox::StandardButton which)
{
    return [which](QWidget *m) { clickMessageBox(m, which); };
}

// ---------------------------------------------------------------------------
// 其他小工具
// ---------------------------------------------------------------------------

// 主檢視 / 第二檢視的分頁容器（測試類別非 friend，故從中央 QSplitter 取）
static QTabWidget *viewTabs(MainWindow &w, int which)
{
    auto *split = qobject_cast<QSplitter *>(w.centralWidget());
    return split ? qobject_cast<QTabWidget *>(split->widget(which)) : nullptr;
}

static int tabCount(MainWindow &w)
{
    QTabWidget *t = viewTabs(w, 0);
    return t ? t->count() : -1;
}

static QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("<open failed>");
    return QString::fromUtf8(f.readAll());
}

static bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(bytes) == bytes.size();
}

// 符號連結（macOS 的 /var → /private/var）會讓字串比對誤判，路徑一律以 canonical 比
static bool samePath(const QString &a, const QString &b)
{
    return QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath();
}

// 只讀目錄：用來製造「寫入失敗」。QSaveFile 需在目標目錄建立暫存檔，
// 目錄不可寫時 open() 就會失敗——這是唯一能穩定觸發存檔失敗分支的方式
//（把檔案本身設 0444 沒用：QSaveFile 是寫暫存檔再 rename，目錄可寫就會成功）。
static bool setDirWritable(const QString &dir, bool writable)
{
    QFile d(dir);
    QFileDevice::Permissions p = QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                 | QFileDevice::ReadUser | QFileDevice::ExeUser;
    if (writable)
        p |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
    return d.setPermissions(p);
}

class TestMainWindowFile : public QObject {
    Q_OBJECT

    QTemporaryDir *m_cfg = nullptr;   // 設定目錄（整個測試類別共用）

    // 每個測試自己的工作目錄
    static QString mkfile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
    {
        const QString p = dir.filePath(name);
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (!writeAll(p, content))
            return QString();
        return QFileInfo(p).absoluteFilePath();
    }

    // 還原為乾淨的預設設定（每個測試前呼叫），避免測試之間互相污染
    static void resetSettings()
    {
        Settings s;                        // 全部欄位為預設值
        s.enableSessionSnapshot = false;   // 不讓快照計時器在測試間寫入檔案
        s.autoUpdater = false;             // 絕不連網
        s.restoreOnLaunch = false;
        // 檔案監看的「已被外部程式修改，要重新載入嗎？」是另一條路徑（MainWindow_Session）
        // 的對話框；本檔的測試會頻繁寫入磁碟，讓它插隊只會干擾判讀，一律關閉。
        s.autoDetectFileStatus = false;
        QVERIFY(SettingsStore::save(s));
    }

private slots:
    void initTestCase()
    {
        // 隔離設定/快取，避免動到使用者真實的 ~/Library/Application Support/macpad++
        QStandardPaths::setTestModeEnabled(true);
        // 關鍵：讓 QFileDialog 使用 Qt 自己的 widget 對話框而非 macOS 原生 NSOpenPanel，
        // 否則無法從測試中設定檔名並 accept，開檔/存檔的成功路徑就完全測不到。
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

        m_cfg = new QTemporaryDir;
        QVERIFY(m_cfg->isValid());
        macpad::persistence::AppPaths::setConfigDirOverride(m_cfg->path());

        macpad::features::BackupService::clearSnapshots();
        macpad::persistence::RecentFiles::clear();
        installModalWatchdog();
    }

    void cleanup()
    {
        stopModalDrivers();
    }

    void cleanupTestCase()
    {
        macpad::persistence::AppPaths::setConfigDirOverride(QString());
        delete m_cfg;
        m_cfg = nullptr;
        QVERIFY2(!g_watchdogFired,
                 "有 modal 對話框沒被任何 handler 接手，是看門狗強制關閉的");
    }

    void init()
    {
        resetSettings();
        stopModalDrivers();
        macpad::persistence::RecentFiles::clear();
        macpad::features::BackupService::clearSnapshots();
    }

    // ===================================================================
    // newFile
    // ===================================================================

    // 新文件套用偏好的預設 EOL/編碼，且不得被標記為 dirty
    //（剛開的空白文件若是 dirty，關閉時會無謂地跳存檔提示）
    void newFileAppliesDefaultsWithoutDirtyFlag()
    {
        Settings s = SettingsStore::load();
        s.defaultEol = macpad::core::Eol::Cr;
        s.defaultEncoding = macpad::core::Encoding::Utf8Bom;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const int before = tabCount(w);
        QCOMPARE(before, 1);

        QMetaObject::invokeMethod(&w, "newFile");
        QCOMPARE(tabCount(w), before + 1);

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QVERIFY(e->isUntitled());
        QVERIFY2(!e->isDirty(), "剛建立的空白文件不應是 dirty");
        QCOMPARE(e->eol(), macpad::core::Eol::Cr);
        QCOMPARE(e->encoding(), macpad::core::Encoding::Utf8Bom);
    }

    // ===================================================================
    // openFileDialog
    // ===================================================================

    // 對話框選檔 → 真的載入內容，且記住目錄（供 defaultDirPolicy=RememberLast）
    void openFileDialogLoadsSelectedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("open_me.txt"), "alpha\nbeta\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto fired = driveModals({pickFile(path)});
        QMetaObject::invokeMethod(&w, "openFileDialog");
        QVERIFY2(*fired == 1, "Open File 沒有開出對話框");

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QVERIFY2(samePath(e->filePath(), path), qPrintable(e->filePath()));
        QCOMPARE(e->text(), QStringLiteral("alpha\nbeta\n"));
    }

    // 取消對話框 → 目前文件不得被換掉
    void openFileDialogCancelKeepsCurrentDocument()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("untouched"));

        auto fired = driveModals({cancelModal()});
        QMetaObject::invokeMethod(&w, "openFileDialog");
        QCOMPARE(*fired, 1);

        QCOMPARE(tabCount(w), 1);
        QVERIFY(w.activeEditor()->isUntitled());
        QCOMPARE(w.activeEditor()->text(), QStringLiteral("untouched"));
    }

    // ===================================================================
    // openFile：分頁沿用 / 去重 / 最近開啟
    // ===================================================================

    void openFileReusesBlankTabThenAddsNewOnes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("a.txt"), "AAA");
        const QString p2 = mkfile(dir, QStringLiteral("b.txt"), "BBB");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QCOMPARE(tabCount(w), 1);

        // (1) 目前是空白未動的 Untitled → 直接沿用，不新增分頁
        w.openFile(p1);
        QCOMPARE(tabCount(w), 1);
        QCOMPARE(w.activeEditor()->filePath(), p1);
        QCOMPARE(w.activeEditor()->text(), QStringLiteral("AAA"));
        QVERIFY2(macpad::persistence::RecentFiles::load().contains(p1),
                 "開檔後未寫入最近開啟清單");

        // (2) 目前分頁已有檔 → 開第二個檔必須新增分頁
        w.openFile(p2);
        QCOMPARE(tabCount(w), 2);
        QCOMPARE(w.activeEditor()->filePath(), p2);

        // (3) 重複開啟已開的檔 → 聚焦既有分頁，不再多開一個
        w.openFile(p1);
        QCOMPARE(tabCount(w), 2);
        QCOMPARE(w.activeEditor()->filePath(), p1);

        // (4) 目前分頁 dirty 的 untitled 也不可被沿用（會弄丟使用者的草稿）
        QMetaObject::invokeMethod(&w, "newFile");
        w.activeEditor()->setText(QStringLiteral("draft"));
        QVERIFY(w.activeEditor()->isDirty());
        const QString p3 = mkfile(dir, QStringLiteral("c.txt"), "CCC");
        w.openFile(p3);
        QCOMPARE(tabCount(w), 4);
        QCOMPARE(w.activeEditor()->filePath(), p3);
    }

    // 讀不到的檔案 → 顯示警告，且不得留下一個假裝開好的分頁
    void openFileFailureWarnsAndKeepsDocumentUntitled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString missing = QFileInfo(dir.filePath(QStringLiteral("ghost.txt")))
                                    .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto fired = driveModals({clickButton(QMessageBox::Ok)});
        w.openFile(missing);
        QVERIFY2(*fired == 1, "開檔失敗時沒有出現警告");

        QCOMPARE(tabCount(w), 1);
        QVERIFY2(w.activeEditor()->isUntitled(), "開檔失敗卻仍把路徑掛到分頁上");
        QVERIFY(!macpad::persistence::RecentFiles::load().contains(missing));
    }

    // 大檔守衛：超過門檻先確認。No 不開、Yes 才開。
    void openFileLargeFileGuard()
    {
        Settings s = SettingsStore::load();
        s.largeFileMB = 1;
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString big = mkfile(dir, QStringLiteral("big.txt"),
                                   QByteArray(1500 * 1024, 'x'));
        QVERIFY(!big.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 回答 No → 不開啟
        auto fired = driveModals({clickButton(QMessageBox::No)});
        w.openFile(big);
        QVERIFY2(*fired == 1, "超過門檻的大檔沒有先詢問");
        QCOMPARE(tabCount(w), 1);
        QVERIFY(w.activeEditor()->isUntitled());

        // (2) 回答 Yes → 照常開啟
        fired = driveModals({clickButton(QMessageBox::Yes)});
        w.openFile(big);
        QCOMPARE(*fired, 1);
        QCOMPARE(w.activeEditor()->filePath(), big);
        QCOMPARE(w.activeEditor()->length(), 1500 * 1024);
    }

    // sessionFileExt：副檔名匹配時改以 session 還原開啟，session 檔本身不該變成一個分頁
    void openFileWithSessionExtensionRestoresSession()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target = mkfile(dir, QStringLiteral("in_session.txt"), "SESSION-DOC");

        QJsonObject tab;
        tab.insert(QStringLiteral("path"), target);
        QJsonArray tabs;
        tabs.append(tab);
        QJsonObject root;
        root.insert(QStringLiteral("schema_version"), 1);
        root.insert(QStringLiteral("active_index"), 0);
        root.insert(QStringLiteral("tabs"), tabs);
        const QString sessionPath = mkfile(dir, QStringLiteral("work.npsess"),
                                           QJsonDocument(root).toJson());
        QVERIFY(!sessionPath.isEmpty());

        Settings s = SettingsStore::load();
        s.sessionFileExt = QStringLiteral("npsess");
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(sessionPath);

        // session 內列出的文件被開啟，而 .npsess 本身沒有被當成文字檔開起來
        bool foundTarget = false;
        QTabWidget *tabsW = viewTabs(w, 0);
        QVERIFY(tabsW);
        for (int i = 0; i < tabsW->count(); ++i) {
            const auto *ed = tabsW->widget(i)->findChild<EditorWidget *>();
            if (ed && samePath(ed->filePath(), target))
                foundTarget = true;
            QVERIFY2(!(ed && samePath(ed->filePath(), sessionPath)),
                     "session 檔本身被當成一般文字檔開啟了");
        }
        QVERIFY2(foundTarget, "session 內列出的文件沒有被開啟");
        QVERIFY(macpad::persistence::RecentFiles::load().contains(sessionPath));
    }

    // UDL：副檔名匹配使用者自訂語言時，套用 UdlLexer（而不是留在預設 lexer）
    void openFileAppliesUdlLexerAndFilters()
    {
        macpad::features::UdlDefinition def;
        def.name = QStringLiteral("AgentLang");
        def.extensions = QStringList{QStringLiteral("agl")};
        // 另存一個「有名稱但沒有副檔名」的 UDL：它無法對應任何檔案，
        // 必須被存檔篩選器略過（否則會出現一條選了也沒用的空篩選器）
        macpad::features::UdlDefinition noExt;
        noExt.name = QStringLiteral("NoExtLang");
        macpad::features::UdlManager mgr;
        QVERIFY(mgr.save(def));
        QVERIFY(mgr.save(noExt));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("sample.agl"), "keyword body");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->filePath(), path);
        QVERIFY2(qobject_cast<macpad::features::UdlLexer *>(e->lexer()),
                 ".agl 沒有套用使用者自訂語言的 lexer");

        // 同一份 UDL 也必須出現在存檔對話框的類型篩選中（Notepad++ v8.7）
        QString filters;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveDialogFilters",
                                          Q_RETURN_ARG(QString, filters)));
        const QStringList parts = filters.split(QStringLiteral(";;"));
        QCOMPARE(parts.first(), QStringLiteral("All files (*)"));
        QVERIFY2(parts.contains(QStringLiteral("AgentLang (*.agl)")),
                 qPrintable(filters.left(200)));
        // 組成必須是「All files + 使用者 UDL + 每個有副檔名的內建語言」，且 UDL 排在內建語言之前
        int builtinWithExt = 0;
        for (const auto &en : macpad::features::BuiltinLanguages::entries()) {
            if (!en.extensions.isEmpty())
                ++builtinWithExt;
        }
        QVERIFY(builtinWithExt > 10);
        QCOMPARE(parts.size(), 2 + builtinWithExt);
        QCOMPARE(parts.indexOf(QStringLiteral("AgentLang (*.agl)")), 1);
        for (const QString &f : parts)
            QVERIFY2(!f.startsWith(QStringLiteral("NoExtLang")),
                     "沒有副檔名的 UDL 不該出現在存檔篩選器中");

        QVERIFY(mgr.remove(QStringLiteral("AgentLang")));
        QVERIFY(mgr.remove(QStringLiteral("NoExtLang")));
    }

    // ===================================================================
    // openFileNotepadStyle（-notepadStyleCmdline）
    // ===================================================================

    void openFileNotepadStyleCreatesMissingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QFileInfo(dir.filePath(QStringLiteral("fresh.txt")))
                                 .absoluteFilePath();
        QVERIFY(!QFileInfo::exists(path));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 回答 No → 不建立、不開啟
        auto fired = driveModals({clickButton(QMessageBox::No)});
        w.openFileNotepadStyle(path);
        QVERIFY2(*fired == 1, "檔案不存在時沒有詢問是否建立");
        QVERIFY2(!QFileInfo::exists(path), "使用者回答 No 卻仍建立了檔案");
        QVERIFY(w.activeEditor()->isUntitled());

        // (2) 回答 Yes → 建立空檔並開啟
        fired = driveModals({clickButton(QMessageBox::Yes)});
        w.openFileNotepadStyle(path);
        QCOMPARE(*fired, 1);
        QVERIFY2(QFileInfo::exists(path), "回答 Yes 卻沒有建立檔案");
        QCOMPARE(QFileInfo(path).size(), qint64(0));
        QCOMPARE(w.activeEditor()->filePath(), path);

        // (3) 檔案已存在 → 不再詢問，直接開啟
        QVERIFY(writeAll(path, "content-now"));
        fired = driveModals({cancelModal()}, 300);
        w.openFileNotepadStyle(path);
        QVERIFY2(*fired == 0, "檔案已存在時不應再詢問");
    }

    // 建立失敗（目標目錄不存在）→ 警告，且不得留下分頁
    void openFileNotepadStyleReportsCreateFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QFileInfo(dir.filePath(QStringLiteral("no/such/dir/x.txt")))
                                 .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        // 先答 Yes 要建立，建立失敗後緊接著會彈第二個 modal（警告）
        auto fired = driveModals({clickButton(QMessageBox::Yes),
                                  clickButton(QMessageBox::Ok)});
        w.openFileNotepadStyle(path);
        QVERIFY2(*fired == 2, "檔案建立失敗時應先詢問、再出現警告");
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(w.activeEditor()->isUntitled());
    }

    // ===================================================================
    // openFileAtLine
    // ===================================================================

    void openFileAtLinePlacesCursor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("lines.txt"),
                                    "l0\nl1\nl2\nl3\nl4\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFileAtLine(path, 3, 2);       // 1-based
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->filePath(), path);
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 2);
        QCOMPARE(col, 1);

        // 0 / 負數要被夾到第一行第一欄，而不是變成無效位置
        w.openFileAtLine(path, 0, 0);
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 0);
        QCOMPARE(col, 0);
    }

    // ===================================================================
    // saveCurrent / saveCurrentAs
    // ===================================================================

    // 存檔要真的把新內容寫到磁碟，並清掉 dirty；Simple 備份要在指定目錄產生 .bak
    void saveCurrentWritesFileAndSimpleBackup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("doc.txt"), "original\n");
        const QString backupDir = dir.filePath(QStringLiteral("bak"));

        Settings s = SettingsStore::load();
        s.backupMode = macpad::persistence::BackupMode::Simple;
        s.backupDir = backupDir;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("changed\n"));
        QVERIFY(e->isDirty());

        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrent", Q_RETURN_ARG(bool, ok)));
        QVERIFY2(ok, "saveCurrent 回報失敗");
        QCOMPARE(readAll(path), QStringLiteral("changed\n"));
        QVERIFY2(!e->isDirty(), "存檔後仍是 dirty");
        QCOMPARE(readAll(backupDir + QStringLiteral("/doc.txt.bak")),
                 QStringLiteral("changed\n"));
    }

    // Verbose 備份會另存帶時間戳記的檔；備份失敗不得阻擋存檔本身（best-effort）
    void saveCurrentVerboseBackupIsBestEffort()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("v.txt"), "one\n");
        const QString backupDir = dir.filePath(QStringLiteral("vbak"));

        Settings s = SettingsStore::load();
        s.backupMode = macpad::persistence::BackupMode::Verbose;
        s.backupDir = backupDir;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        w.activeEditor()->setText(QStringLiteral("two\n"));
        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrent", Q_RETURN_ARG(bool, ok)));
        QVERIFY(ok);
        QCOMPARE(readAll(path), QStringLiteral("two\n"));
        const QStringList made = QDir(backupDir).entryList({QStringLiteral("v.txt.*.bak")},
                                                           QDir::Files);
        QCOMPARE(made.size(), 1);
    }

    // 未命名文件按「儲存」→ 自動轉為另存新檔，內容真的落地
    void saveCurrentOnUntitledFallsBackToSaveAs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QFileInfo(dir.filePath(QStringLiteral("named.txt")))
                                 .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && e->isUntitled());
        e->setText(QStringLiteral("brand new\n"));

        auto fired = driveModals({pickFile(path)});
        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrent", Q_RETURN_ARG(bool, ok)));
        QVERIFY2(*fired == 1, "未命名文件存檔時沒有開出另存對話框");
        QVERIFY(ok);
        QVERIFY(samePath(e->filePath(), path));
        QCOMPARE(readAll(path), QStringLiteral("brand new\n"));
        QVERIFY(!e->isDirty());
    }

    // 寫入失敗（目錄不可寫）→ 警告、回傳 false、dirty 保留（否則使用者以為存好了）
    void saveCurrentFailureWarnsAndKeepsDirty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sub = dir.filePath(QStringLiteral("locked"));
        QVERIFY(QDir().mkpath(sub));
        const QString path = QFileInfo(sub + QStringLiteral("/ro.txt")).absoluteFilePath();
        QVERIFY(writeAll(path, "before"));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("after"));
        QVERIFY(setDirWritable(sub, false));

        auto fired = driveModals({clickButton(QMessageBox::Ok)});
        bool ok = true;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrent", Q_RETURN_ARG(bool, ok)));
        QVERIFY(setDirWritable(sub, true));   // 先還原，QTemporaryDir 才清得掉

        QVERIFY2(*fired == 1, "存檔失敗時沒有出現警告");
        QVERIFY2(!ok, "存檔失敗卻回報成功");
        QVERIFY2(e->isDirty(), "存檔失敗後 dirty 被清掉了——使用者會誤以為已存檔");
        QCOMPARE(readAll(path), QStringLiteral("before"));
    }

    // 另存：取消 → false 且不動任何檔；失敗 → 警告且不改分頁的檔名
    void saveCurrentAsCancelAndFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("src.txt"), "keep\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // (1) 取消
        auto fired = driveModals({cancelModal()});
        bool ok = true;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrentAs", Q_RETURN_ARG(bool, ok)));
        QCOMPARE(*fired, 1);
        QVERIFY2(!ok, "取消另存卻回報成功");
        QCOMPARE(e->filePath(), path);

        // (2) 目標目錄不可寫 → 警告，分頁仍指向原檔
        const QString sub = dir.filePath(QStringLiteral("nowrite"));
        QVERIFY(QDir().mkpath(sub));
        QVERIFY(setDirWritable(sub, false));
        const QString target = QFileInfo(sub + QStringLiteral("/out.txt")).absoluteFilePath();

        fired = driveModals({pickFile(target), clickButton(QMessageBox::Ok)});
        ok = true;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrentAs", Q_RETURN_ARG(bool, ok)));
        QVERIFY(setDirWritable(sub, true));

        QVERIFY2(*fired == 2, "另存失敗時應先開出對話框、再出現警告");
        QVERIFY(!ok);
        QCOMPARE(e->filePath(), path);
        QVERIFY(!QFileInfo::exists(target));
    }

    // 另存成功：新檔寫出、分頁改指向新檔、原檔保持不變
    void saveCurrentAsWritesNewFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = mkfile(dir, QStringLiteral("orig.txt"), "v1\n");
        const QString dst = QFileInfo(dir.filePath(QStringLiteral("copy.txt")))
                                .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(src);
        EditorWidget *e = w.activeEditor();
        e->setText(QStringLiteral("v2\n"));

        auto fired = driveModals({pickFile(dst)});
        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(&w, "saveCurrentAs", Q_RETURN_ARG(bool, ok)));
        QCOMPARE(*fired, 1);
        QVERIFY(ok);
        QCOMPARE(readAll(dst), QStringLiteral("v2\n"));
        QCOMPARE(readAll(src), QStringLiteral("v1\n"));   // 原檔不動
        QVERIFY(samePath(e->filePath(), dst));
        QVERIFY(!e->isDirty());
    }

    // ===================================================================
    // saveAll
    // ===================================================================

    // 兩個檢視的 dirty 分頁全部存檔；乾淨分頁不重寫；clone 分頁不重複處理。
    // 未命名的 dirty 分頁會轉為另存對話框。
    void saveAllSavesEveryDirtyTabAcrossViews()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("s1.txt"), "one\n");
        const QString p2 = mkfile(dir, QStringLiteral("s2.txt"), "two\n");
        const QString p3 = mkfile(dir, QStringLiteral("clean.txt"), "clean\n");
        const QString untitledTarget = QFileInfo(dir.filePath(QStringLiteral("fromsaveall.txt")))
                                           .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(p1);
        w.activeEditor()->setText(QStringLiteral("one!\n"));
        w.openFile(p2);
        w.activeEditor()->setText(QStringLiteral("two!\n"));
        w.openFile(p3);                       // 保持乾淨

        // 把目前分頁複製到第二檢視 → clone 應被略過（與來源共享文件）
        QMetaObject::invokeMethod(&w, "cloneToOtherView");
        QVERIFY2(viewTabs(w, 1) && viewTabs(w, 1)->count() == 1, "clone 到第二檢視失敗");

        // 切回主檢視後再加一個未命名 dirty 分頁 → saveAll 會替它開另存對話框
        viewTabs(w, 0)->setCurrentIndex(0);
        QMetaObject::invokeMethod(&w, "newFile");
        w.activeEditor()->setText(QStringLiteral("scratch\n"));

        auto fired = driveModals({pickFile(untitledTarget)});
        QMetaObject::invokeMethod(&w, "saveAll");
        QVERIFY2(*fired == 1, "saveAll 沒有為未命名分頁開出另存對話框");

        QCOMPARE(readAll(p1), QStringLiteral("one!\n"));
        QCOMPARE(readAll(p2), QStringLiteral("two!\n"));
        QCOMPARE(readAll(p3), QStringLiteral("clean\n"));
        QCOMPARE(readAll(untitledTarget), QStringLiteral("scratch\n"));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已全部儲存"));
    }

    // ===================================================================
    // saveCopyAs
    // ===================================================================

    void saveCopyAsWritesCopyWithoutRebindingTab()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = mkfile(dir, QStringLiteral("main.txt"), "body\n");
        const QString copy = QFileInfo(dir.filePath(QStringLiteral("main_copy.txt")))
                                 .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(src);
        EditorWidget *e = w.activeEditor();
        e->setText(QStringLiteral("edited\n"));

        // (1) 取消 → 什麼都不寫
        auto fired = driveModals({cancelModal()});
        QMetaObject::invokeMethod(&w, "saveCopyAs");
        QCOMPARE(*fired, 1);
        QVERIFY(!QFileInfo::exists(copy));

        // (2) 確認 → 副本寫出，但分頁仍指向原檔且仍是 dirty（副本不是儲存）
        fired = driveModals({pickFile(copy)});
        QMetaObject::invokeMethod(&w, "saveCopyAs");
        QCOMPARE(*fired, 1);
        QCOMPARE(readAll(copy), QStringLiteral("edited\n"));
        QCOMPARE(readAll(src), QStringLiteral("body\n"));
        QCOMPARE(e->filePath(), src);
        QVERIFY2(e->isDirty(), "存副本不應清掉原分頁的 dirty 狀態");
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("副本已儲存")));
        QCOMPARE(tabCount(w), 1);
    }

    // openCopyAfterSaveACopy=true（Notepad++ v8.7）→ 存完副本後把副本也開起來
    void saveCopyAsCanOpenTheCopy()
    {
        Settings s = SettingsStore::load();
        s.openCopyAfterSaveACopy = true;
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = mkfile(dir, QStringLiteral("m.txt"), "data\n");
        const QString copy = QFileInfo(dir.filePath(QStringLiteral("m2.txt")))
                                 .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(src);
        auto fired = driveModals({pickFile(copy)});
        QMetaObject::invokeMethod(&w, "saveCopyAs");
        QCOMPARE(*fired, 1);
        QCOMPARE(tabCount(w), 2);
        QVERIFY(samePath(w.activeEditor()->filePath(), copy));
    }

    // 寫入失敗 → 警告，不得靜默失敗
    void saveCopyAsFailureWarns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString src = mkfile(dir, QStringLiteral("m.txt"), "data\n");
        const QString sub = dir.filePath(QStringLiteral("ro"));
        QVERIFY(QDir().mkpath(sub));
        QVERIFY(setDirWritable(sub, false));
        const QString copy = QFileInfo(sub + QStringLiteral("/c.txt")).absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(src);
        auto fired = driveModals({pickFile(copy), clickButton(QMessageBox::Ok)});
        QMetaObject::invokeMethod(&w, "saveCopyAs");
        QVERIFY(setDirWritable(sub, true));

        QVERIFY2(*fired == 2, "副本寫入失敗時應先開出對話框、再出現警告");
        QVERIFY(!QFileInfo::exists(copy));
    }

    // ===================================================================
    // setAllDocumentsReadOnly
    // ===================================================================

    void setAllDocumentsReadOnlyRespectsPolicyLock()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("r1.txt"), "1");
        const QString p2 = mkfile(dir, QStringLiteral("r2.txt"), "2");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(p1);
        w.openFile(p2);
        QCOMPARE(tabCount(w), 2);

        QMetaObject::invokeMethod(&w, "setAllDocumentsReadOnly", Q_ARG(bool, true));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已將 2 個文件設為唯讀"));
        QTabWidget *tabs = viewTabs(w, 0);
        QVERIFY(tabs);
        for (int i = 0; i < tabs->count(); ++i)
            QVERIFY(tabs->widget(i)->findChild<EditorWidget *>()->isReadOnly());

        // 已是唯讀 → 再套用一次不計數（changed=0）
        QMetaObject::invokeMethod(&w, "setAllDocumentsReadOnly", Q_ARG(bool, true));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已將 0 個文件設為唯讀"));

        // 其中一個改為「政策唯讀」（-fullReadOnly / 監控）→ 解除時必須跳過它
        EditorWidget *locked = tabs->widget(0)->findChild<EditorWidget *>();
        QVERIFY(locked);
        locked->setPolicyReadOnly(true);
        QMetaObject::invokeMethod(&w, "setAllDocumentsReadOnly", Q_ARG(bool, false));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已解除 1 個文件的唯讀"));
        QVERIFY2(locked->isReadOnly(), "政策唯讀的分頁被批次解鎖了");
        QVERIFY(!tabs->widget(1)->findChild<EditorWidget *>()->isReadOnly());
    }

    // ===================================================================
    // reloadFromDisk
    // ===================================================================

    void reloadFromDiskAllPaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("live.txt"), "disk-v1\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 未命名 → 直接早退，不得有任何訊息或對話框
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "reloadFromDisk");
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // (2) 不 dirty → 不詢問，直接重新載入磁碟上的新內容
        QVERIFY(writeAll(path, "disk-v2\n"));
        QMetaObject::invokeMethod(&w, "reloadFromDisk");
        QCOMPARE(e->text(), QStringLiteral("disk-v2\n"));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已重新載入"));

        // (3) dirty 且回答 No → 保留使用者的未存變更
        e->setText(QStringLiteral("my-edit\n"));
        QVERIFY(e->isDirty());
        QVERIFY(writeAll(path, "disk-v3\n"));
        auto fired = driveModals({clickButton(QMessageBox::No)});
        QMetaObject::invokeMethod(&w, "reloadFromDisk");
        QVERIFY2(*fired == 1, "dirty 時重新載入沒有先確認");
        QCOMPARE(e->text(), QStringLiteral("my-edit\n"));

        // (4) dirty 且回答 Yes → 捨棄未存變更，換成磁碟內容
        fired = driveModals({clickButton(QMessageBox::Yes)});
        QMetaObject::invokeMethod(&w, "reloadFromDisk");
        QCOMPARE(*fired, 1);
        QCOMPARE(e->text(), QStringLiteral("disk-v3\n"));
        QVERIFY(!e->isDirty());

        // (5) 檔案已從磁碟消失 → 警告，且不得把編輯器清空
        QVERIFY(QFile::remove(path));
        fired = driveModals({clickButton(QMessageBox::Ok)});
        QMetaObject::invokeMethod(&w, "reloadFromDisk");
        QVERIFY2(*fired == 1, "重新載入失敗時沒有出現警告");
        QCOMPARE(e->text(), QStringLiteral("disk-v3\n"));
    }

    // ===================================================================
    // renameCurrentFile
    // ===================================================================

    void renameCurrentFileMovesFileOnDisk()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldPath = mkfile(dir, QStringLiteral("old.txt"), "payload\n");
        const QString newPath = QFileInfo(dir.filePath(QStringLiteral("new.txt")))
                                    .absoluteFilePath();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(oldPath);
        EditorWidget *e = w.activeEditor();
        e->setText(QStringLiteral("payload edited\n"));   // 未存變更也要一併帶到新檔名

        // (1) 取消 → 舊檔留著、不產生新檔
        auto fired = driveModals({cancelModal()});
        QMetaObject::invokeMethod(&w, "renameCurrentFile");
        QCOMPARE(*fired, 1);
        QVERIFY(QFileInfo::exists(oldPath));
        QVERIFY(!QFileInfo::exists(newPath));

        // (2) 選同一個名字 → 視同取消，舊檔不可被刪掉
        fired = driveModals({pickFile(oldPath)});
        QMetaObject::invokeMethod(&w, "renameCurrentFile");
        QCOMPARE(*fired, 1);
        QVERIFY2(QFileInfo::exists(oldPath), "改成同名時把原檔刪掉了");

        // (3) 真正改名 → 新檔含未存變更、舊檔消失
        fired = driveModals({pickFile(newPath)});
        QMetaObject::invokeMethod(&w, "renameCurrentFile");
        QCOMPARE(*fired, 1);
        QCOMPARE(readAll(newPath), QStringLiteral("payload edited\n"));
        QVERIFY2(!QFileInfo::exists(oldPath), "改名後舊檔仍存在");
        QVERIFY(samePath(e->filePath(), newPath));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("已改名為：new.txt")));
    }

    void renameCurrentFileFailureAndUntitledFallback()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // (1) 未命名 → 等同另存新檔
        const QString saveTarget = QFileInfo(dir.filePath(QStringLiteral("from_rename.txt")))
                                       .absoluteFilePath();
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.activeEditor()->setText(QStringLiteral("unsaved\n"));
        auto fired = driveModals({pickFile(saveTarget)});
        QMetaObject::invokeMethod(&w, "renameCurrentFile");
        QCOMPARE(*fired, 1);
        QCOMPARE(readAll(saveTarget), QStringLiteral("unsaved\n"));

        // (2) 寫入新名稱失敗 → 警告，且舊檔絕不可被刪除
        const QString oldPath = mkfile(dir, QStringLiteral("keep.txt"), "safe\n");
        w.openFile(oldPath);
        const QString sub = dir.filePath(QStringLiteral("rolock"));
        QVERIFY(QDir().mkpath(sub));
        QVERIFY(setDirWritable(sub, false));
        const QString bad = QFileInfo(sub + QStringLiteral("/n.txt")).absoluteFilePath();

        fired = driveModals({pickFile(bad), clickButton(QMessageBox::Ok)});
        QMetaObject::invokeMethod(&w, "renameCurrentFile");
        QVERIFY(setDirWritable(sub, true));

        QVERIFY2(*fired == 2, "改名失敗時應先開出對話框、再出現警告");
        QVERIFY2(QFileInfo::exists(oldPath), "改名失敗卻把原檔刪了——資料遺失");
        QCOMPARE(readAll(oldPath), QStringLiteral("safe\n"));
    }

    // ===================================================================
    // maybeSave / closeTab / closeAllTabs / closeAllButCurrent
    // ===================================================================

    // 關閉 dirty 分頁的三種回答：Save 寫檔、Discard 不寫、Cancel 擋下關閉
    void closeTabHonoursSaveDiscardCancel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("q.txt"), "disk\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        w.activeEditor()->setText(QStringLiteral("edited\n"));
        QCOMPARE(tabCount(w), 1);

        // (1) Cancel → 分頁必須留著，磁碟不變
        auto fired = driveModals({clickButton(QMessageBox::Cancel)});
        QMetaObject::invokeMethod(&w, "closeTab", Q_ARG(int, 0));
        QVERIFY2(*fired == 1, "關閉 dirty 分頁時沒有跳出存檔提示");
        QCOMPARE(tabCount(w), 1);
        QCOMPARE(readAll(path), QStringLiteral("disk\n"));

        // (2) Save → 內容落地後才關閉；兩個檢視都空了會補一張空白分頁
        fired = driveModals({clickButton(QMessageBox::Save)});
        QMetaObject::invokeMethod(&w, "closeTab", Q_ARG(int, 0));
        QCOMPARE(*fired, 1);
        QCOMPARE(readAll(path), QStringLiteral("edited\n"));
        QCOMPARE(tabCount(w), 1);
        QVERIFY2(w.activeEditor()->isUntitled(), "關掉最後一個分頁後沒有補上空白分頁");

        // (3) Discard → 不寫檔就關閉
        w.openFile(path);
        w.activeEditor()->setText(QStringLiteral("throw away\n"));
        fired = driveModals({clickButton(QMessageBox::Discard)});
        QMetaObject::invokeMethod(&w, "closeTab", Q_ARG(int, 0));
        QCOMPARE(*fired, 1);
        QCOMPARE(readAll(path), QStringLiteral("edited\n"));
        QVERIFY(w.activeEditor()->isUntitled());
    }

    // clone 分頁與來源共享文件：關閉 clone 不應觸發存檔提示，也不進「最近關閉」堆疊
    void closingCloneSkipsSavePrompt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("shared.txt"), "shared\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        w.activeEditor()->setText(QStringLiteral("dirty now\n"));
        QMetaObject::invokeMethod(&w, "cloneToOtherView");
        QTabWidget *second = viewTabs(w, 1);
        QVERIFY(second);
        QCOMPARE(second->count(), 1);

        // 關閉第二檢視的 clone：即使文件是 dirty，也不該有任何對話框。
        // 以分頁的關閉鈕訊號進入 closeTabIn(第二檢視, 0)——這正是使用者按 ✕ 走的那條線。
        auto fired = driveModals({cancelModal()}, 400);
        QMetaObject::invokeMethod(second, "tabCloseRequested", Q_ARG(int, 0));
        QVERIFY2(*fired == 0, "關閉 clone 分頁時不該詢問存檔");
        QCOMPARE(second->count(), 0);
        QVERIFY2(!second->isVisible(), "第二檢視空了應自動隱藏");
        QCOMPARE(tabCount(w), 1);       // 來源分頁仍在
    }

    void closeAllTabsAndCancel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("c1.txt"), "1");
        const QString p2 = mkfile(dir, QStringLiteral("c2.txt"), "2");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(p1);
        w.openFile(p2);
        QCOMPARE(tabCount(w), 2);

        // 全部乾淨 → 全關，最後補一張空白分頁
        QMetaObject::invokeMethod(&w, "closeAllTabs");
        QCOMPARE(tabCount(w), 1);
        QVERIFY(w.activeEditor()->isUntitled());

        // 其中一個 dirty 且使用者取消 → 整個批次立刻停止（不可繼續關其他分頁）
        // 空白分頁會被第一個檔沿用，所以總數是 2
        w.openFile(p1);
        w.openFile(p2);
        w.activeEditor()->setText(QStringLiteral("dirty"));
        QCOMPARE(tabCount(w), 2);
        auto fired = driveModals({clickButton(QMessageBox::Cancel)});
        QMetaObject::invokeMethod(&w, "closeAllTabs");
        QCOMPARE(*fired, 1);
        QVERIFY2(tabCount(w) == 2, "使用者取消後仍關掉了分頁");
    }

    void closeAllButCurrentKeepsActiveAndPinned()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("k1.txt"), "1");
        const QString p2 = mkfile(dir, QStringLiteral("k2.txt"), "2");
        const QString p3 = mkfile(dir, QStringLiteral("k3.txt"), "3");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(p1);
        w.openFile(p2);
        w.openFile(p3);
        QCOMPARE(tabCount(w), 3);

        QTabWidget *tabs = viewTabs(w, 0);
        QVERIFY(tabs);
        // 釘選第一個分頁：批次關閉不得波及它（複刻 Notepad++ Pin Tab）
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));

        EditorWidget *keep = w.activeEditor();
        QVERIFY(keep);
        QMetaObject::invokeMethod(&w, "closeAllButCurrent");
        QCOMPARE(tabCount(w), 2);       // 作用中 + 釘選

        QStringList left;
        for (int i = 0; i < tabs->count(); ++i)
            left << QFileInfo(tabs->widget(i)->findChild<EditorWidget *>()->filePath()).fileName();
        QVERIFY2(left.contains(QStringLiteral("k1.txt")), qPrintable(left.join(',')));
        QVERIFY2(left.contains(QStringLiteral("k3.txt")), qPrintable(left.join(',')));

        // 使用者取消時整個批次停止
        w.openFile(p2);
        w.activeEditor()->setText(QStringLiteral("x"));
        EditorWidget *dirtyEd = w.activeEditor();
        w.openFile(p3);                 // 讓 dirty 的那個不是「目前分頁」
        QVERIFY(dirtyEd->isDirty());
        const int before = tabCount(w);
        auto fired = driveModals({clickButton(QMessageBox::Cancel)});
        QMetaObject::invokeMethod(&w, "closeAllButCurrent");
        QCOMPARE(*fired, 1);
        QVERIFY2(tabCount(w) > 1, "使用者取消後仍把其他分頁關光了");
        QVERIFY(tabCount(w) <= before);
    }

    // ===================================================================
    // restoreClosedTab
    // ===================================================================

    void restoreClosedTabReopensLastClosedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("gone.txt"), "come back\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // 堆疊為空 → 只提示
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "restoreClosedTab");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("沒有最近關閉的檔案"));

        w.openFile(path);
        QMetaObject::invokeMethod(&w, "closeTab", Q_ARG(int, 0));
        QCOMPARE(tabCount(w), 1);
        QVERIFY(w.activeEditor()->isUntitled());

        QMetaObject::invokeMethod(&w, "restoreClosedTab");
        QCOMPARE(w.activeEditor()->filePath(), path);
        QCOMPARE(w.activeEditor()->text(), QStringLiteral("come back\n"));

        // 取出後堆疊即清空，再按一次只會提示
        QMetaObject::invokeMethod(&w, "restoreClosedTab");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("沒有最近關閉的檔案"));
    }

    // 最近關閉堆疊上限為 20：關掉第 21 個檔時最舊的那筆必須被擠掉，
    // 否則這個清單會無限成長。
    void closedFileStackIsCappedAtTwentyEntries()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QStringList paths;
        for (int i = 0; i < 21; ++i) {
            paths << mkfile(dir, QStringLiteral("stack%1.txt").arg(i, 2, 10, QLatin1Char('0')),
                            QByteArray::number(i));
            QVERIFY(!paths.last().isEmpty());
        }

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        for (const QString &p : std::as_const(paths)) {
            w.openFile(p);
            QMetaObject::invokeMethod(&w, "closeTab",
                                      Q_ARG(int, viewTabs(w, 0)->currentIndex()));
        }

        // 由新到舊取回；最多只能取回 20 筆
        QStringList restored;
        for (int i = 0; i < 20; ++i) {
            QMetaObject::invokeMethod(&w, "restoreClosedTab");
            restored << w.activeEditor()->filePath();
        }
        QCOMPARE(restored.size(), 20);
        QCOMPARE(restored.first(), paths.at(20));      // 最後關的最先回來
        QCOMPARE(restored.last(), paths.at(1));
        QVERIFY2(!restored.contains(paths.at(0)),
                 "第 21 次關檔後，最舊的那筆沒有被擠出堆疊");

        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "restoreClosedTab");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("沒有最近關閉的檔案"));
    }

    // ===================================================================
    // moveCurrentToTrash
    // ===================================================================

    void moveCurrentToTrashRequiresSavedFileAndConfirmation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("trash_me.txt"), "bye\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 未命名 → 只提示，不做任何事
        auto fired = driveModals({clickButton(QMessageBox::Ok)});
        QMetaObject::invokeMethod(&w, "moveCurrentToTrash");
        QVERIFY2(*fired == 1, "未存檔分頁沒有出現提示");
        QCOMPARE(tabCount(w), 1);

        // (2) 已存檔但回答 No → 檔案留著
        w.openFile(path);
        fired = driveModals({clickButton(QMessageBox::No)});
        QMetaObject::invokeMethod(&w, "moveCurrentToTrash");
        QCOMPARE(*fired, 1);
        QVERIFY2(QFileInfo::exists(path), "回答 No 卻仍把檔案丟進垃圾桶");
        QCOMPARE(tabCount(w), 1);

        // (3) 檔案已不在磁碟 → moveToTrash 失敗，須明確警告而非靜默
        const QString vanished = mkfile(dir, QStringLiteral("vanished.txt"), "x");
        w.openFile(vanished);
        QVERIFY(QFile::remove(vanished));
        fired = driveModals({clickButton(QMessageBox::Yes), clickButton(QMessageBox::Ok)});
        QMetaObject::invokeMethod(&w, "moveCurrentToTrash");
        QVERIFY2(*fired == 2, "移到垃圾桶失敗時應先確認、再出現警告");
        QVERIFY2(tabCount(w) >= 2, "移到垃圾桶失敗卻仍把分頁關掉了");
    }

    // 成功路徑：檔案離開原位置、分頁關閉；關光了會補一張空白分頁。
    // 註：這會真的把「測試自己建立的」暫存檔移進系統垃圾桶，不涉及使用者的檔案。
    void moveCurrentToTrashClosesTabOnSuccess()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("macpadpp_test_trash.txt"), "gone\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        QCOMPARE(tabCount(w), 1);

        auto fired = driveModals({clickButton(QMessageBox::Yes)});
        QMetaObject::invokeMethod(&w, "moveCurrentToTrash");
        QCOMPARE(*fired, 1);

        if (QFileInfo::exists(path))
            QSKIP("此環境不支援 QFile::moveToTrash，略過成功路徑斷言");
        QCOMPARE(tabCount(w), 1);
        QVERIFY2(w.activeEditor()->isUntitled(),
                 "刪除最後一個檔案後沒有補上空白分頁");
    }

    // ===================================================================
    // 拖放開檔
    // ===================================================================

    void dropOpensFilesAndIgnoresNonFileMime()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = mkfile(dir, QStringLiteral("d1.txt"), "D1");
        const QString p2 = mkfile(dir, QStringLiteral("d2.txt"), "D2");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) openDroppedFiles：多個檔案逐一開成分頁，最後一個成為作用中分頁
        QMetaObject::invokeMethod(&w, "openDroppedFiles",
                                  Q_ARG(QStringList, (QStringList{p1, p2})));
        QCOMPARE(tabCount(w), 2);
        QCOMPARE(w.activeEditor()->filePath(), p2);

        // (2) dragEnterEvent：帶本機檔案 URL 的拖曳必須被接受，否則使用者根本放不下來
        QMimeData fileMime;
        fileMime.setUrls({QUrl::fromLocalFile(p1)});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &fileMime,
                              Qt::LeftButton, Qt::NoModifier);
        enter.ignore();
        QApplication::sendEvent(&w, &enter);
        QVERIFY2(enter.isAccepted(), "拖入檔案時視窗沒有接受拖放");

        // 純文字（非檔案）→ 不接受，避免把選取文字誤判成開檔請求
        QMimeData textMime;
        textMime.setText(QStringLiteral("just text"));
        QDragEnterEvent enterText(QPoint(10, 10), Qt::CopyAction, &textMime,
                                  Qt::LeftButton, Qt::NoModifier);
        enterText.ignore();
        QApplication::sendEvent(&w, &enterText);
        QVERIFY2(!enterText.isAccepted(), "非檔案的拖曳不應被接受");

        // (3) dropEvent：放下檔案 → 真的開起來。
        // 每個 Drop 前都補送一次「會被接受的」DragEnter：Qt 依前一個 DragEnter 建立的
        // 拖放對象來派送 Drop，少了它合成的 Drop 根本不會送到 widget。
        const QString p3 = mkfile(dir, QStringLiteral("d3.txt"), "D3");
        QMimeData dropMime;
        dropMime.setUrls({QUrl::fromLocalFile(p3)});
        QDragEnterEvent enter3(QPoint(10, 10), Qt::CopyAction, &dropMime,
                               Qt::LeftButton, Qt::NoModifier);
        enter3.ignore();
        QApplication::sendEvent(&w, &enter3);
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &dropMime,
                        Qt::LeftButton, Qt::NoModifier);
        drop.ignore();
        QApplication::sendEvent(&w, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(tabCount(w), 3);
        QCOMPARE(w.activeEditor()->filePath(), p3);

        // 沒有本機檔案的 drop → 忽略，不得多開分頁。
        // 用另一個乾淨視窗發送，並先送一個帶檔案、會被接受的 DragEnter 建立拖放對象，
        // 否則後面的 Drop 不會被派送（等於什麼都沒測到）。
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        QMimeData emptyMime;
        emptyMime.setText(QStringLiteral("nope"));
        QMimeData enterMime;
        enterMime.setUrls({QUrl::fromLocalFile(p1)});
        QDragEnterEvent enter4(QPoint(10, 10), Qt::CopyAction, &enterMime,
                               Qt::LeftButton, Qt::NoModifier);
        enter4.ignore();
        QApplication::sendEvent(&w2, &enter4);
        QDropEvent dropText(QPointF(10, 10), Qt::CopyAction, &emptyMime,
                            Qt::LeftButton, Qt::NoModifier);
        dropText.ignore();
        QApplication::sendEvent(&w2, &dropText);
        QVERIFY(!dropText.isAccepted());
        QCOMPARE(tabCount(w2), 1);
        QVERIFY(w2.activeEditor()->isUntitled());
    }

    // ===================================================================
    // clearFileReadOnlyFlag
    // ===================================================================

    void clearFileReadOnlyFlagPaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString normal = mkfile(dir, QStringLiteral("rw.txt"), "rw\n");
        const QString locked = mkfile(dir, QStringLiteral("ro.txt"), "ro\n");

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 未命名 → 早退，不留訊息
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "clearFileReadOnlyFlag");
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // (2) 檔案本來就可寫 → 明確告知，不做任何變更
        w.openFile(normal);
        QMetaObject::invokeMethod(&w, "clearFileReadOnlyFlag");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("The file is not read-only"));

        // (3) 檔案是唯讀 → 清除屬性並一併解除 app 內編輯鎖
        QVERIFY(EditorWidget::setFileReadOnly(locked, true));
        w.openFile(locked);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QVERIFY(e->isFileReadOnly());
        QMetaObject::invokeMethod(&w, "clearFileReadOnlyFlag");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("Read-only attribute cleared"));
        QVERIFY2(!EditorWidget::isFileReadOnly(locked), "檔案唯讀屬性沒有真的被清掉");
        QVERIFY2(!e->isReadOnly(), "屬性清掉了但編輯鎖沒解，使用者仍動不了");

        // (4) 因 -fullReadOnly / 監控而鎖定者：屬性可清，但分頁維持鎖定
        QVERIFY(EditorWidget::setFileReadOnly(locked, true));
        e->setPolicyReadOnly(true);
        QMetaObject::invokeMethod(&w, "clearFileReadOnlyFlag");
        QVERIFY(!EditorWidget::isFileReadOnly(locked));
        QVERIFY2(e->isReadOnly(), "政策唯讀的分頁被解鎖了");
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("stays locked")));
    }

    // ===================================================================
    // revealInFinder / openInDefaultApp（只測未存檔的早退分支）
    // ===================================================================

    // 已存檔的分支會真的啟動 Finder / 外部應用程式，測試環境不觸發；
    // 這裡驗證未存檔時必須安靜早退（不可對空路徑呼叫平台整合）。
    void revealAndOpenExternallyIgnoreUntitled()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(w.activeEditor()->isUntitled());
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "revealInFinder");
        QMetaObject::invokeMethod(&w, "openInDefaultApp");
        QCOMPARE(tabCount(w), 1);
        QVERIFY(w.activeEditor()->isUntitled());
    }

    // ===================================================================
    // closeEvent
    // ===================================================================

    // 停用 session 快照：所有未存分頁逐一確認，Cancel 必須擋下關閉
    void closeEventBlocksOnCancelWithoutSnapshot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("close.txt"), "disk\n");

        Settings s = SettingsStore::load();
        s.enableSessionSnapshot = false;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        w.activeEditor()->setText(QStringLiteral("unsaved\n"));

        // (1) Cancel → 事件被 ignore，視窗不關
        auto fired = driveModals({clickButton(QMessageBox::Cancel)});
        QCloseEvent ev1;
        QApplication::sendEvent(&w, &ev1);
        QVERIFY2(*fired == 1, "關窗時沒有為未存分頁跳出提示");
        QVERIFY2(!ev1.isAccepted(), "使用者取消了，關閉事件卻仍被接受");
        QCOMPARE(readAll(path), QStringLiteral("disk\n"));

        // (2) Discard → 事件被接受，磁碟內容不變
        fired = driveModals({clickButton(QMessageBox::Discard)});
        QCloseEvent ev2;
        QApplication::sendEvent(&w, &ev2);
        QCOMPARE(*fired, 1);
        QVERIFY(ev2.isAccepted());
        QCOMPARE(readAll(path), QStringLiteral("disk\n"));
    }

    // 啟用 session 快照：未命名未存緩衝關閉不打擾（內容由快照持久化），
    // 但「已命名的 dirty 檔」仍必須提示——磁碟檔不會被寫入，靜默跳過等於資料遺失。
    void closeEventSnapshotSkipsUntitledButNotNamedDirty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = mkfile(dir, QStringLiteral("named.txt"), "disk\n");

        Settings s = SettingsStore::load();
        s.enableSessionSnapshot = true;
        QVERIFY(SettingsStore::save(s));

        // (1) 只有未命名 dirty 分頁 → 完全不提示
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.activeEditor()->setText(QStringLiteral("scratch\n"));
            QVERIFY(w.activeEditor()->isDirty());
            auto fired = driveModals({cancelModal()}, 400);
            QCloseEvent ev;
            QApplication::sendEvent(&w, &ev);
            QVERIFY2(*fired == 0, "啟用快照時，未命名未存緩衝關閉不該打擾使用者");
            QVERIFY(ev.isAccepted());
        }

        // (2) 已命名的 dirty 檔 → 仍要提示；選 Save 就必須真的寫入磁碟
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.openFile(path);
            w.activeEditor()->setText(QStringLiteral("must-persist\n"));
            // 複製到第二檢視：clone 與來源共享文件，關窗時只能問一次
            QMetaObject::invokeMethod(&w, "cloneToOtherView");
            QVERIFY(viewTabs(w, 1) && viewTabs(w, 1)->count() == 1);
            auto fired = driveModals({clickButton(QMessageBox::Save)});
            QCloseEvent ev;
            QApplication::sendEvent(&w, &ev);
            QVERIFY2(*fired == 1,
                     "啟用快照時，已命名 dirty 檔仍必須提示存檔，且 clone 不得重複詢問");
            QVERIFY(ev.isAccepted());
            QCOMPARE(readAll(path), QStringLiteral("must-persist\n"));
        }

        // 正常關閉會清空當機復原快照
        QVERIFY(macpad::features::BackupService::pendingSnapshots().isEmpty());
    }
};

QTEST_MAIN(TestMainWindowFile)
#include "test_mainwindow_file.moc"
