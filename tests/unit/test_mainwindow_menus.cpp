// 單元測試：MainWindow_Menus.cpp——所有選單的建構與各選單項的行為。
//
// 這個檔案幾乎全由「建立 QAction + 一段 lambda」構成，因此覆蓋率的缺口不是分支，
// 而是「沒有人按過那顆選單項」。測試策略即：走使用者實際會走的那條線——
// 從選單列找到 QAction 並 trigger()，再斷言可觀察的結果（編輯器內容、剪貼簿、
// 狀態列訊息、持久化檔案、面板是否出現…），而不是只確認呼叫沒崩潰。
//
// 三個讓「原本不可測」的分支變成可測的關鍵：
//  1. AA_DontUseNativeDialogs：QFileDialog / QMessageBox 一律用 Qt 自己的 widget 對話框，
//     而非 macOS 原生 panel，才能從外部（timer）填值並關閉。
//  2. ModalPilot：在事件迴圈中依序接手每一個 modal 對話框。它以「在 dialog 上蓋一個
//     dynamic property」來辨識是否已處理過——不能只比對指標，因為連續兩個
//     QInputDialog::getText 是先後在同一段堆疊上建立的，很可能落在同一個位址。
//  3. 硬性看門狗：ModalPilot 逾時會強制關閉對話框；main() 另設 alarm()。
//     任何情況下測試都不會永久卡住。
//
// 刻意不觸發的路徑（於各處註解說明）：真的送印、啟動外部程式（瀏覽器 / Finder）、
// 對外連網（Check for Updates…）、以及 Quit。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDate>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <Qsci/qscilexer.h>
#include <Qsci/qscimacro.h>
#include <Qsci/qsciscintilla.h>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "features/findall/FindAllDock.h"
#include "features/findinfiles/FindInFilesDock.h"
#include "features/search/FindReplaceDialog.h"
#include "features/run/RunCommand.h"
#include "features/run/RunDock.h"
#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"
#include "features/udl/UdlXmlIo.h"
#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"
#include "persistence/SessionStore.h"
#include "persistence/SettingsStore.h"
#include "persistence/ThemeStore.h"
#include "ui/EditorPane.h"
#include "ui/StyleConfiguratorDialog.h"

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using macpad::core::EditorWidget;
using macpad::ui::EditorPane;

// ---------------------------------------------------------------------------
// 選單查找
// ---------------------------------------------------------------------------

// 遞迴找出文字為 text 的動作。用文字比對而非 objectName：這些選單項沒有 objectName，
// 且測試不載入翻譯，tr() 會原樣回傳原始字串。
static QAction *findIn(QMenu *menu, const QString &text)
{
    if (!menu)
        return nullptr;
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        if (a->menu()) {
            if (QAction *sub = findIn(a->menu(), text))
                return sub;
            continue;   // 子選單標題本身不視為可觸發的動作
        }
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

// 取得頂層選單。以 objectName 比對（createMenus 明確設過），title 會被翻譯，不可靠。
static QMenu *topMenu(MainWindow &w, const QString &objectName)
{
    const auto tops = w.menuBar()->actions();
    for (QAction *top : tops) {
        if (top->menu() && top->menu()->objectName() == objectName)
            return top->menu();
    }
    return nullptr;
}

// 限定在某個頂層選單裡找——同名項目在不同選單各有一份實作（例如 Edit 與 Search
// 各有一個「Toggle Bookmark」），不指定選單就會測到另一條 lambda。
static QAction *findAction(MainWindow &w, const QString &menuObjectName, const QString &text)
{
    return findIn(topMenu(w, menuObjectName), text);
}

// 遞迴找子選單（以標題比對）
static QMenu *findSubmenu(QMenu *menu, const QString &title)
{
    if (!menu)
        return nullptr;
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        if (!a->menu())
            continue;
        if (a->menu()->title() == title)
            return a->menu();
        if (QMenu *sub = findSubmenu(a->menu(), title))
            return sub;
    }
    return nullptr;
}

// 觸發選單動作；找不到就讓測試失敗（選單改名要立刻被發現，而不是靜靜地少測一項）
#define TRIGGER(w, menuObj, text)                                                    \
    do {                                                                             \
        QAction *a__ = findAction((w), QStringLiteral(menuObj), QStringLiteral(text)); \
        QVERIFY2(a__, "找不到選單項：" menuObj " ▸ " text);                            \
        a__->trigger();                                                              \
    } while (false)

// ---------------------------------------------------------------------------
// Modal 驅動
// ---------------------------------------------------------------------------

// 在事件迴圈中依序接手 modal 對話框：每偵測到一個「尚未處理過」的 modal，
// 就取出佇列中的下一個 handler 執行；handler 未關閉對話框時強制 reject()。
// 佇列空了仍冒出對話框 → 記為 unexpected 並關閉（測試據此斷言）。
// 逾時 → 強制關閉目前 modal 並記為 timedOut。任何情況都不會讓 exec() 永久阻塞。
class ModalPilot {
public:
    using Handler = std::function<void(QDialog *)>;

    explicit ModalPilot(QList<Handler> handlers, int timeoutMs = 5000)
        : m_queue(std::move(handlers)), m_deadline(timeoutMs)
    {
        m_timer = new QTimer;
        m_timer->setInterval(5);
        QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] { poll(); });
        m_timer->start();
    }

    ~ModalPilot()
    {
        m_timer->stop();
        delete m_timer;
    }

    ModalPilot(const ModalPilot &) = delete;
    ModalPilot &operator=(const ModalPilot &) = delete;

    int handled() const { return m_handled; }
    int remaining() const { return int(m_queue.size()); }
    bool unexpected() const { return m_unexpected; }
    bool timedOut() const { return m_timedOut; }

private:
    void poll()
    {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal)
            return;
        // 已處理過的對話框：等它自己關閉即可。以 dynamic property 標記而非比對指標——
        // 連續兩個 QInputDialog 很可能重用同一個堆疊位址，指標比對會誤判為同一個。
        static const char *kMark = "__macpad_pilot_handled";
        if (modal->property(kMark).toBool()) {
            if (m_deadline.hasExpired()) {   // 卡住不關：強制收尾
                m_timedOut = true;
                modal->close();
            }
            return;
        }
        modal->setProperty(kMark, true);
        auto *dlg = qobject_cast<QDialog *>(modal);
        if (!dlg) {
            m_unexpected = true;
            modal->close();
            return;
        }
        if (m_queue.isEmpty()) {
            m_unexpected = true;
            dlg->reject();
            return;
        }
        const Handler h = m_queue.takeFirst();
        h(dlg);
        if (dlg->isVisible())
            dlg->reject();
        ++m_handled;
    }

    QTimer *m_timer = nullptr;
    QList<Handler> m_queue;
    QDeadlineTimer m_deadline;
    int m_handled = 0;
    bool m_unexpected = false;
    bool m_timedOut = false;
};

// 常用 handler ----------------------------------------------------------------

static ModalPilot::Handler acceptDialog()
{
    return [](QDialog *d) { d->accept(); };
}

static ModalPilot::Handler rejectDialog()
{
    return [](QDialog *d) { d->reject(); };
}

// QInputDialog：填入文字（getText / getItem 皆適用）後確認
static ModalPilot::Handler inputText(const QString &value)
{
    return [value](QDialog *d) {
        if (auto *inp = qobject_cast<QInputDialog *>(d)) {
            inp->setTextValue(value);
            inp->accept();
        }
    };
}

// QFileDialog：選定路徑後以 done(Accepted) 收尾。
// 不用 accept()：那條路徑會做存在性/覆蓋確認（可能再彈一個對話框），
// 對測試而言只是雜訊；selectedFiles() 在 done() 之後一樣讀得到選定路徑。
static ModalPilot::Handler chooseFile(const QString &path)
{
    return [path](QDialog *d) {
        auto *fd = qobject_cast<QFileDialog *>(d);
        if (!fd)
            return;
        fd->setDirectory(QFileInfo(path).absolutePath());
        fd->selectFile(path);
        // QFileSystemModel 是非同步載入的：對話框剛開啟時檔案清單還沒就緒，
        // 直接 done() 有機會讓 selectedFiles() 讀到空值（實測第一個開出來的
        // 對話框必中）。先讓事件迴圈把模型跑完，再把絕對路徑直接寫進檔名輸入框
        // ——那正是使用者手動鍵入路徑的等效行為，不依賴清單選取。
        QCoreApplication::processEvents();
        if (auto *edit = fd->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
            edit->setText(QFileInfo(path).absoluteFilePath());
        // 以 QDialog::done 收尾（QFileDialog 把 done 覆寫為 protected，需經基底型別呼叫）
        d->done(QDialog::Accepted);
    };
}

// QMessageBox：檢查內文後關閉（check 收到的是完整內文）
static ModalPilot::Handler checkMessage(QString *out)
{
    return [out](QDialog *d) {
        if (auto *mb = qobject_cast<QMessageBox *>(d))
            *out = mb->text();
        d->accept();
    };
}

// ---------------------------------------------------------------------------
// 其他小工具
// ---------------------------------------------------------------------------

static QString cfg(const QString &name)
{
    return macpad::persistence::AppPaths::filePath(name);
}

// 由編輯器往上找到它所屬的 EditorPane（MainWindow::currentPane() 為 private）
static EditorPane *paneOf(EditorWidget *e)
{
    for (QWidget *w = e ? e->parentWidget() : nullptr; w; w = w->parentWidget()) {
        if (auto *p = qobject_cast<EditorPane *>(w))
            return p;
    }
    return nullptr;
}

// 由編輯器往上找到承載它的 QTabWidget（MainWindow 的分頁容器為 private 成員）
static QTabWidget *tabsOf(EditorWidget *e)
{
    for (QWidget *w = e ? e->parentWidget() : nullptr; w; w = w->parentWidget()) {
        if (auto *t = qobject_cast<QTabWidget *>(w))
            return t;
    }
    return nullptr;
}

// 在暫存目錄寫一個檔案並回傳路徑
static QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &data)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(data);
    f.close();
    return path;
}

class TestMainWindowMenus : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 隔離設定目錄，不動使用者真實的 ~/Library/Application Support/macpad++
        QStandardPaths::setTestModeEnabled(true);
        // 關鍵：讓 QFileDialog / QMessageBox 使用 Qt 自己的 widget 對話框。
        // 原生 panel 無法從外部關閉，所有走檔案對話框的選單項都會變成不可測。
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

        // 與前次執行脫鉤：具名 session、巨集、Run 指令都會影響「清單為空」的分支
        QDir(cfg(QStringLiteral("sessions"))).removeRecursively();
        QFile::remove(cfg(QStringLiteral("macros.json")));
        QFile::remove(cfg(QStringLiteral("run_commands.json")));
        QFile::remove(cfg(QStringLiteral("session.json")));

        m_savedSettings = macpad::persistence::SettingsStore::load();
    }

    void cleanupTestCase()
    {
        // 這些測試會改動偏好（語言、disabledLanguages…），一律還原，
        // 免得污染其他測試或使用者設定
        macpad::persistence::SettingsStore::save(m_savedSettings);
        QDir(cfg(QStringLiteral("sessions"))).removeRecursively();
        QFile::remove(cfg(QStringLiteral("macros.json")));
        QFile::remove(cfg(QStringLiteral("run_commands.json")));
    }

    // =====================================================================
    // File 選單
    // =====================================================================

    // 註解標記依副檔名而異（lineCommentMarker 的語言分類表）。
    // 一次驗證四類：C 家族 //、script #、SQL/Lua --、Lisp ;
    void toggleLineCommentUsesLanguageMarker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        const struct { const char *file; const char *marker; } cases[] = {
            {"a.py", "#"}, {"a.sql", "--"}, {"a.el", ";"}, {"a.cpp", "//"},
        };
        for (const auto &c : cases) {
            const QString path = writeFile(dir, QString::fromLatin1(c.file), "x\n");
            QVERIFY(!path.isEmpty());
            w.openFile(path);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e);
            e->setText(QStringLiteral("x"));
            e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
            TRIGGER(w, "Edit", "Toggle Line Comment");
            QCOMPARE(e->text(), QStringLiteral("%1 x").arg(QString::fromLatin1(c.marker)));
        }
    }

    // 工具列的 Close 按鈕沒有對應選單項（自建 lambda），單獨驗證它真的關掉分頁
    void toolbarCloseButtonClosesTab()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        for (int i = 0; i < 2; ++i) {
            const QString p = writeFile(dir, QStringLiteral("tb%1.txt").arg(i), "x\n");
            QVERIFY(!p.isEmpty());
            w.openFile(p);
        }
        auto *tb = w.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);
        QAction *closeAct = nullptr;
        const auto acts = tb->actions();
        for (QAction *a : acts)
            if (a->objectName() == QLatin1String("close"))
                closeAct = a;
        QVERIFY(closeAct);

        auto *tabs = tabsOf(w.activeEditor());
        QVERIFY2(tabs, "找不到分頁容器");
        const int before = tabs->count();
        QVERIFY(before >= 2);
        closeAct->trigger();
        QCOMPARE(tabs->count(), before - 1);
        // 一路關到底：關掉最後一個分頁後仍會留下一個空白未命名分頁（Notepad++ 行為），
        // 再按一次不得崩潰（按鈕內的 count() 守衛）
        for (int i = 0; i < before + 1; ++i)
            closeAct->trigger();
        QVERIFY(tabs->count() <= 1);
    }

    // Preferences…：確認後必須寫回設定並回報，且逐分頁套用偏好不得崩潰
    void preferencesDialogAppliesAndReports()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        {   // 取消 → 不回報
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "File", "Preferences…");
            QCOMPARE(pilot.handled(), 1);
            QVERIFY(w.statusBar()->currentMessage().isEmpty());
        }
        ModalPilot pilot({acceptDialog()});
        TRIGGER(w, "File", "Preferences…");
        QCOMPARE(pilot.handled(), 1);
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("偏好設定已儲存"));
    }

    // About：內容須帶版本號（純資訊對話框，關掉即可）
    void aboutDialogShowsVersion()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        TRIGGER(w, "File", "About macpad++");
        // 注意：macOS 上 QMessageBox::about() 依 Apple HIG 是「非 modal」的，
        // 不會出現在 activeModalWidget()，因此這裡直接在頂層視窗中找它。
        QTest::qWait(20);
        QMessageBox *box = nullptr;
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *t : tops) {
            if (auto *mb = qobject_cast<QMessageBox *>(t); mb && mb->isVisible())
                box = mb;
        }
        QVERIFY2(box, "About 未開啟對話框");
        QVERIFY2(box->text().contains(QString::fromLatin1(MACPAD_VERSION)),
                 qPrintable(QStringLiteral("About 未顯示版本號：%1").arg(box->text())));
        box->close();
    }
    // 註：Check for Updates… 會對外連網、Quit 會結束行程、Print… 會送印，
    //     三者依測試規範不觸發。

    // 具名 Session：存檔 → 清單 → 載入。清單為空時只提示、不開選擇對話框。
    void namedSessionsSaveAndLoad()
    {
        QDir(cfg(QStringLiteral("sessions"))).removeRecursively();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("sess.txt"), "session content\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 尚無 session → Load 只彈提示（不是選擇清單）
        QString msg;
        {
            ModalPilot pilot({checkMessage(&msg)});
            TRIGGER(w, "File", "Load Session…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(msg, QStringLiteral("尚無已儲存的 session"));

        // (2) 取消命名 → 不得產生 session
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "File", "Save Session As…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(macpad::persistence::SessionStore::listNames().isEmpty());

        // (3) 存成具名 session
        w.openFile(path);
        {
            ModalPilot pilot({inputText(QStringLiteral("menus_case"))});
            TRIGGER(w, "File", "Save Session As…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(macpad::persistence::SessionStore::listNames()
                    .contains(QStringLiteral("menus_case")));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("menus_case")));

        // (4) 載入 → 該檔案應被開啟（此處以「分頁數增加」與路徑命中驗證）
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        {
            ModalPilot pilot({inputText(QStringLiteral("menus_case"))});
            TRIGGER(w2, "File", "Load Session…");
            QCOMPARE(pilot.handled(), 1);
        }
        bool found = false;
        const auto editors = w2.findChildren<EditorWidget *>();
        for (EditorWidget *e : editors)
            if (!e->isUntitled() && QFileInfo(e->filePath()) == QFileInfo(path))
                found = true;
        QVERIFY2(found, "載入具名 session 後未開啟其中的檔案");
    }

    // Open / Add Folder as Workspace：選定目錄後工作區面板須顯示該根目錄
    void workspaceFolderMenuItems()
    {
        QTemporaryDir dirA, dirB;
        QVERIFY(dirA.isValid() && dirB.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // 取消選擇 → 不動工作區
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "File", "Open Folder as Workspace…");
            QCOMPARE(pilot.handled(), 1);
        }
        auto *ws = w.findChild<QDockWidget *>();
        Q_UNUSED(ws);

        {
            ModalPilot pilot({chooseFile(dirA.path())});
            TRIGGER(w, "File", "Open Folder as Workspace…");
            QCOMPARE(pilot.handled(), 1);
        }
        {
            ModalPilot pilot({chooseFile(dirB.path())});
            TRIGGER(w, "File", "Add Folder to Workspace…");
            QCOMPARE(pilot.handled(), 1);
        }
        // 兩個根目錄都應留在工作區（Add 是多根，不清除既有）
        QVERIFY2(!w.findChildren<QDockWidget *>().isEmpty(), "工作區面板不存在");
    }

    // New Window：另開一個獨立主視窗（不是換分頁）
    void newWindowOpensSecondMainWindow()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const int before = QApplication::topLevelWidgets().size();
        TRIGGER(w, "File", "New Window");
        MainWindow *created = nullptr;
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *t : tops) {
            auto *mw = qobject_cast<MainWindow *>(t);
            if (mw && mw != &w)
                created = mw;
        }
        QVERIFY2(created, "New Window 未建立新的主視窗");
        QVERIFY(QApplication::topLevelWidgets().size() > before);
        // WA_DeleteOnClose：關閉後應自行銷毀（不可留下洩漏的視窗影響後續測試）
        QPointer<MainWindow> guard(created);
        created->close();
        QTest::qWait(50);
        QVERIFY2(guard.isNull(), "New Window 建立的視窗關閉後未被銷毀");
    }

    // Export as RTF / HTML：成功寫檔 + 無法寫入時的警告分支
    void exportRtfAndHtml()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("int main() { return 0; }"));

        // (1) 取消 → 不產生檔案
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "File", "Export as RTF…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);

        // (2) RTF
        const QString rtf = dir.filePath(QStringLiteral("out.rtf"));
        {
            ModalPilot pilot({chooseFile(rtf)});
            TRIGGER(w, "File", "Export as RTF…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(QFile::exists(rtf), "Export as RTF 未產生檔案");
        QFile f(rtf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY2(f.readAll().startsWith("{\\rtf"), "輸出不是 RTF");
        f.close();
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已匯出 RTF"));

        // (3) HTML
        const QString html = dir.filePath(QStringLiteral("out.html"));
        {
            ModalPilot pilot({chooseFile(html)});
            TRIGGER(w, "File", "Export as HTML…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(QFile::exists(html), "Export as HTML 未產生檔案");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已匯出 HTML"));

        // (4) 無法寫入（目錄不存在）→ 必須明確警告，不可靜默失敗
        QString warn;
        {
            ModalPilot pilot({chooseFile(dir.filePath(QStringLiteral("no/such/dir/x.rtf"))),
                              checkMessage(&warn)});
            TRIGGER(w, "File", "Export as RTF…");
            QCOMPARE(pilot.handled(), 2);
        }
        QVERIFY2(warn.contains(QStringLiteral("無法寫入")), qPrintable(warn));
    }

    // File ▸ Close Tab / Close All / Close All but This
    void closeTabActions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        for (int i = 0; i < 3; ++i) {
            const QString p = writeFile(dir, QStringLiteral("c%1.txt").arg(i), "x\n");
            QVERIFY(!p.isEmpty());
            w.openFile(p);
        }
        auto *tabs = tabsOf(w.activeEditor());
        QVERIFY(tabs);
        const int before = tabs->count();
        QVERIFY(before >= 3);

        TRIGGER(w, "File", "Close Tab");
        QCOMPARE(tabs->count(), before - 1);

        TRIGGER(w, "File", "Close All but This");
        QCOMPARE(tabs->count(), 1);

        TRIGGER(w, "File", "Close All");
        // Close All 之後 Notepad++ 會留下一個空白未命名分頁
        QVERIFY(tabs->count() <= 1);
    }

    // =====================================================================
    // Edit 選單
    // =====================================================================

    // 剪下/複製/貼上/刪除/全選，以及「無選取時整行剪下/複製」偏好的兩條分支
    void clipboardActionsAndCopyLineWithoutSelection()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.copyLineWithoutSelection = true;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // (1) 無選取 + 偏好開啟 → 複製整行
        e->setText(QStringLiteral("line one\nline two"));
        e->setCursorPosition(0, 2);
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 2);
        QApplication::clipboard()->clear();
        TRIGGER(w, "Edit", "Copy");
        QVERIFY2(QApplication::clipboard()->text().startsWith(QStringLiteral("line one")),
                 "無選取時未複製整行");

        // (2) 有選取 → 只複製選取內容
        e->setSelection(1, 0, 1, 4);
        TRIGGER(w, "Edit", "Copy");
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("line"));

        // (3) 貼上：游標處插入
        e->setText(QString());
        e->setCursorPosition(0, 0);
        TRIGGER(w, "Edit", "Paste");
        QCOMPARE(e->text(), QStringLiteral("line"));

        // (4) 全選 + 刪除
        TRIGGER(w, "Edit", "Select All");
        QVERIFY(e->hasSelectedText());
        TRIGGER(w, "Edit", "Delete");
        QCOMPARE(e->text(), QString());

        // (5) 無選取 + 偏好開啟 → 剪下整行
        e->setText(QStringLiteral("cut me\nkeep me"));
        e->setCursorPosition(0, 1);
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 1);
        TRIGGER(w, "Edit", "Cut");
        QCOMPARE(e->text(), QStringLiteral("keep me"));

        // (6) 有選取 → 只剪下選取內容（"keep" 被剪走，留下前導空白）
        e->setSelection(0, 0, 0, 4);
        TRIGGER(w, "Edit", "Cut");
        QCOMPARE(e->text(), QStringLiteral(" me"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep"));

        // (7) Undo / Redo 走的是帶歷史的版本，須真的回復
        TRIGGER(w, "Edit", "Undo");
        QCOMPARE(e->text(), QStringLiteral("keep me"));
        TRIGGER(w, "Edit", "Redo");
        QCOMPARE(e->text(), QStringLiteral(" me"));

        s.copyLineWithoutSelection = false;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        // 偏好關閉 + 無選取 → 走一般 copy()（不複製整行）
        e->setText(QStringLiteral("plain"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        TRIGGER(w, "Edit", "Copy");
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }

    // Go to Matching Brace（Edit 版）
    void goToMatchingBrace()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("(abc)"));
        e->setCursorPosition(0, 0);
        TRIGGER(w, "Edit", "Go to Matching Brace");
        // Scintilla 的慣例是把游標停在對應括號「之後」（位置 5 = ')' 的右側）
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)), 5);
    }

    // Find in Files…：建立面板，且已存檔時預設搜尋目錄為該檔所在資料夾
    void findInFilesCreatesDockWithDefaultRoot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("f.txt"), "needle\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(!w.findChild<macpad::features::FindInFilesDock *>());
        TRIGGER(w, "Edit", "Find in Files…");
        auto *dock = w.findChild<macpad::features::FindInFilesDock *>();
        QVERIFY2(dock, "Find in Files… 未建立面板");
        QVERIFY(dock->isVisibleTo(&w));

        // 已存檔文件 → 預設根目錄；再次呼叫必須重用同一個面板
        w.openFile(path);
        TRIGGER(w, "Edit", "Find in Files…");
        QCOMPARE(w.findChildren<macpad::features::FindInFilesDock *>().size(), 1);
    }

    // 書籤：切換 / 上下一個 / 複製書籤行
    void bookmarkActions()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("l0\nl1\nl2\nl3"));

        e->setCursorPosition(1, 0);
        TRIGGER(w, "Edit", "Toggle Bookmark");
        e->setCursorPosition(3, 0);
        TRIGGER(w, "Edit", "Toggle Bookmark");

        e->setCursorPosition(0, 0);
        TRIGGER(w, "Edit", "Next Bookmark");
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 1);
        TRIGGER(w, "Edit", "Next Bookmark");
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 3);
        TRIGGER(w, "Edit", "Previous Bookmark");
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 1);

        // Search ▸ Bookmark ▸ Copy Bookmarked Lines（另一條 lambda）
        QApplication::clipboard()->clear();
        TRIGGER(w, "Search", "Copy Bookmarked Lines");
        const QString copied = QApplication::clipboard()->text();
        QVERIFY2(copied.contains(QStringLiteral("l1")) && copied.contains(QStringLiteral("l3")),
                 qPrintable(QStringLiteral("複製的書籤行內容非預期：%1").arg(copied)));
    }

    // 變更歷史 / 虛擬空間：開關必須傳到編輯器
    void changeHistoryAndVirtualSpace()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("a\nb\nc"));

        QAction *ch = findAction(w, QStringLiteral("Edit"), QStringLiteral("Enable Change History"));
        QVERIFY(ch);
        QVERIFY(ch->isCheckable());
        ch->setChecked(true);
        e->setCursorPosition(1, 0);
        e->insert(QStringLiteral("X"));
        TRIGGER(w, "Edit", "Next Change");
        TRIGGER(w, "Edit", "Previous Change");
        ch->setChecked(false);

        QAction *vs = findAction(w, QStringLiteral("Edit"), QStringLiteral("Virtual Space"));
        QVERIFY(vs && vs->isCheckable());
        vs->setChecked(true);
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETVIRTUALSPACEOPTIONS)) != 0);
        vs->setChecked(false);
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETVIRTUALSPACEOPTIONS)), 0);
    }

    // 多重選取：Select Next Occurrence / Skip / Undo、四種 Select All Occurrences
    void multiSelectActions()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("foo Foo foo bar"));

        // Select Next / Skip / Undo 三個導覽命令：主選取不得被吃掉（至少保留一段），
        // 這是它們在 Notepad++ 的不變式；實際新增幾段取決於 Scintilla 的多選狀態。
        e->setSelection(0, 0, 0, 3);      // 選第一個 foo
        TRIGGER(w, "Edit", "Select Next Occurrence");
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS)) >= 1);
        QCOMPARE(e->selectedText(), QStringLiteral("foo"));
        TRIGGER(w, "Edit", "Skip and Select Next");
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS)) >= 1);
        TRIGGER(w, "Edit", "Undo Last Selection");
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS)) >= 1);

        // 四種變體：大小寫 × 全字。以選取段數驗證差異——
        // 「Match Case」只該命中 2 個 foo，「Ignore Case」要多命中 Foo。
        e->setSelection(0, 0, 0, 3);
        TRIGGER(w, "Edit", "Match Case");
        const int matchCase = int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS));
        e->setSelection(0, 0, 0, 3);
        TRIGGER(w, "Edit", "Ignore Case");
        const int ignoreCase = int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS));
        QCOMPARE(matchCase, 2);
        QCOMPARE(ignoreCase, 3);

        e->setSelection(0, 0, 0, 3);
        TRIGGER(w, "Edit", "Match Case + Whole Word");
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS)), 2);
        e->setSelection(0, 0, 0, 3);
        TRIGGER(w, "Edit", "Ignore Case + Whole Word");
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONS)), 3);
    }

    // On Selection ▸ Open Containing Folder：選取為空白時必須早退。
    // 有實際內容的路徑會啟動 Finder/檔案總管，測試環境不觸發。
    void onSelectionOpenContainingFolderRequiresSelection()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QMenu *onSel = findSubmenu(topMenu(w, QStringLiteral("Edit")),
                                   QStringLiteral("On Selection"));
        QVERIFY2(onSel, "找不到 On Selection 子選單");
        QAction *act = findIn(onSel, QStringLiteral("Open Containing Folder"));
        QVERIFY(act);

        e->setText(QStringLiteral("   \n"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        act->trigger();                       // 無選取 → 早退
        e->setSelection(0, 0, 0, 3);          // 選到純空白 → trimmed 後為空，同樣早退
        QVERIFY(e->hasSelectedText());
        act->trigger();
        QCOMPARE(e->text(), QStringLiteral("   \n"));   // 不得有副作用
    }

    // Paste Special：純文字 / HTML / RTF 三個入口
    void pasteSpecialVariants()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        QApplication::clipboard()->setText(QStringLiteral("plain text"));
        e->setText(QString());
        TRIGGER(w, "Edit", "Paste as Plain Text");
        QCOMPARE(e->text(), QStringLiteral("plain text"));

        // 唯讀時不得插入
        e->setText(QString());
        e->setReadOnly(true);
        TRIGGER(w, "Edit", "Paste as Plain Text");
        QCOMPARE(e->text(), QString());
        e->setReadOnly(false);

        auto *html = new QMimeData;
        html->setHtml(QStringLiteral("<b>bold</b> text"));
        html->setText(QStringLiteral("bold text"));
        QApplication::clipboard()->setMimeData(html);
        e->setText(QString());
        TRIGGER(w, "Edit", "Paste HTML Content");
        QVERIFY2(e->text().contains(QStringLiteral("bold")),
                 qPrintable(QStringLiteral("HTML 貼上結果非預期：%1").arg(e->text())));

        auto *rtf = new QMimeData;
        rtf->setData(QStringLiteral("text/rtf"),
                     QByteArrayLiteral("{\\rtf1\\ansi rich text}"));
        rtf->setText(QStringLiteral("rich text"));
        QApplication::clipboard()->setMimeData(rtf);
        e->setText(QString());
        TRIGGER(w, "Edit", "Paste RTF Content");
        QVERIFY2(e->text().contains(QStringLiteral("rich")),
                 qPrintable(QStringLiteral("RTF 貼上結果非預期：%1").arg(e->text())));
    }

    // 每行複製一次的重複行為與插入日期時間
    void insertDateTimeVariants()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.dateFormat = QStringLiteral("custom");
        s.customDateFormat = QStringLiteral("yyyy");
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        const QString year = QString::number(QDate::currentDate().year());

        e->setText(QString());
        TRIGGER(w, "Edit", "Date Time (Short Format)");
        QVERIFY2(!e->text().isEmpty(), "短格式日期未插入");

        e->setText(QString());
        TRIGGER(w, "Edit", "Date Time (Long Format)");
        QVERIFY(!e->text().isEmpty());

        // 偏好格式：dateFormat=custom → 採用 customDateFormat（只有年份）
        e->setText(QString());
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER(w, "Edit", "Date Time (Preference Format)");
        QCOMPARE(e->text(), year);

        // 自訂格式：對話框輸入格式字串
        e->setText(QString());
        {
            ModalPilot pilot({inputText(QStringLiteral("yyyy"))});
            TRIGGER(w, "Edit", "Date Time (Custom…)");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(e->text(), year);

        // 取消 → 不插入
        e->setText(QString());
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Edit", "Date Time (Custom…)");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(e->text(), QString());

        // 唯讀 → 四個入口都不得插入
        e->setReadOnly(true);
        TRIGGER(w, "Edit", "Date Time (Short Format)");
        TRIGGER(w, "Edit", "Date Time (Long Format)");
        TRIGGER(w, "Edit", "Date Time (Preference Format)");
        TRIGGER(w, "Edit", "Date Time (Custom…)");
        QCOMPARE(e->text(), QString());
        e->setReadOnly(false);
    }

    // Column Editor：Text 模式與 Number 模式各插入一欄
    void columnEditorTextAndNumberModes()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // (1) 取消 → 內容不變
        e->setText(QStringLiteral("a\nb\nc"));
        e->setSelection(0, 0, 2, 0);
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Edit", "Column Editor…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(e->text(), QStringLiteral("a\nb\nc"));

        // (2) Text 模式：每行同欄插入固定文字
        {
            ModalPilot pilot({[](QDialog *d) {
                const auto radios = d->findChildren<QRadioButton *>();
                QVERIFY(radios.size() >= 2);
                radios.at(1)->setChecked(true);          // Text 模式
                // QSpinBox 內部也有 QLineEdit，取第一個會拿錯；只認直接掛在頁面上的那個
                QLineEdit *edit = nullptr;
                const auto edits = d->findChildren<QLineEdit *>();
                for (QLineEdit *le : edits)
                    if (!qobject_cast<QAbstractSpinBox *>(le->parentWidget()))
                        edit = le;
                QVERIFY(edit);
                edit->setText(QStringLiteral(">"));
                d->accept();
            }});
            TRIGGER(w, "Edit", "Column Editor…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(e->text(), QStringLiteral(">a\n>b\n>c"));

        // (3) Number 模式（預設 1 起、每次 +1）：無選取時由游標行到檔尾
        e->setText(QStringLiteral("a\nb\nc"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 0);
        {
            ModalPilot pilot({acceptDialog()});
            TRIGGER(w, "Edit", "Column Editor…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(e->text(), QStringLiteral("1a\n2b\n3c"));

        // (4) 唯讀 → 連對話框都不該開
        e->setText(QStringLiteral("ro"));
        e->setReadOnly(true);
        {
            ModalPilot pilot({});
            TRIGGER(w, "Edit", "Column Editor…");
            QCOMPARE(pilot.handled(), 0);
            QVERIFY2(!pilot.unexpected(), "唯讀文件仍開出 Column Editor 對話框");
        }
        QCOMPARE(e->text(), QStringLiteral("ro"));
        e->setReadOnly(false);
    }

    // 行複製/刪除、縮排、Leading Spaces to TAB
    void lineAndIndentActions()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.tabWidth = 4;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        e->setText(QStringLiteral("dup\nkeep"));
        e->setCursorPosition(0, 0);
        TRIGGER(w, "Edit", "Duplicate Current Line");
        QCOMPARE(e->text(), QStringLiteral("dup\ndup\nkeep"));

        TRIGGER(w, "Edit", "Delete Current Line");
        QCOMPARE(e->text(), QStringLiteral("dup\nkeep"));

        // 唯讀 → 不得刪行
        e->setReadOnly(true);
        TRIGGER(w, "Edit", "Delete Current Line");
        QCOMPARE(e->text(), QStringLiteral("dup\nkeep"));
        e->setReadOnly(false);

        // 縮排 / 反縮排。刻意用「跨行選取」：單行且整行被選取時 Scintilla 的 TAB
        // 是取代選取而非縮排，那條路徑測不到縮排本身。
        e->setText(QStringLiteral("x\ny"));
        e->setSelection(0, 0, 1, 1);
        TRIGGER(w, "Edit", "Increase Line Indent");
        const QString indented = e->text();
        QVERIFY2(indented != QStringLiteral("x\ny"), "Increase Line Indent 未縮排");
        QVERIFY(indented.endsWith(QStringLiteral("y")));
        TRIGGER(w, "Edit", "Decrease Line Indent");
        QCOMPARE(e->text(), QStringLiteral("x\ny"));

        // 前導空白轉 TAB（依偏好 tabWidth=4）
        e->setText(QStringLiteral("    y"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER(w, "Edit", "Leading Spaces to TAB");
        QCOMPARE(e->text(), QStringLiteral("\ty"));
    }

    // Copy to Clipboard 三項：完整路徑 / 檔名 / 目錄
    void copyPathActions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("copypath.txt"), "x\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        // 未命名文件 → 不得寫入剪貼簿（守衛分支）
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        TRIGGER(w, "Edit", "Current Full File Path");
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));

        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());

        TRIGGER(w, "Edit", "Current Full File Path");
        QCOMPARE(QFileInfo(QApplication::clipboard()->text()), QFileInfo(path));
        TRIGGER(w, "Edit", "Current Filename");
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("copypath.txt"));
        TRIGGER(w, "Edit", "Current Directory Path");
        QCOMPARE(QFileInfo(QApplication::clipboard()->text()),
                 QFileInfo(QFileInfo(path).absolutePath()));
    }

    // Read-Only 勾選、Character Panel 開啟
    void readOnlyToggleAndCharacterPanel()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        QAction *ro = findAction(w, QStringLiteral("Edit"), QStringLiteral("Read-Only"));
        QVERIFY(ro && ro->isCheckable());
        ro->setChecked(true);
        QVERIFY2(e->isReadOnly(), "Read-Only 勾選後編輯器未進入唯讀");
        ro->setChecked(false);
        QVERIFY(!e->isReadOnly());

        TRIGGER(w, "Edit", "Character Panel");
        // 面板是 QDockWidget，開啟後應存在且被要求顯示
        bool found = false;
        const auto docks = w.findChildren<QDockWidget *>();
        for (QDockWidget *d : docks)
            if (d->windowTitle().contains(QStringLiteral("Character")))
                found = true;
        QVERIFY2(found, "Character Panel 未建立");
    }

    // =====================================================================
    // Encoding 選單
    // =====================================================================

    void encodingAndEolMenus()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("a\nb"));

        TRIGGER(w, "Encoding", "UTF-8 with BOM");
        QCOMPARE(int(e->encoding()), int(macpad::core::Encoding::Utf8Bom));
        TRIGGER(w, "Encoding", "UTF-16 LE");
        QCOMPARE(int(e->encoding()), int(macpad::core::Encoding::Utf16LE));
        TRIGGER(w, "Encoding", "ANSI (Latin-1)");
        QCOMPARE(int(e->encoding()), int(macpad::core::Encoding::Latin1));
        TRIGGER(w, "Encoding", "UTF-8");
        QCOMPARE(int(e->encoding()), int(macpad::core::Encoding::Utf8));

        TRIGGER(w, "Encoding", "Windows (CRLF)");
        QCOMPARE(int(e->eol()), int(macpad::core::Eol::CrLf));
        TRIGGER(w, "Encoding", "Classic Mac (CR)");
        QCOMPARE(int(e->eol()), int(macpad::core::Eol::Cr));
        TRIGGER(w, "Encoding", "Unix (LF)");
        QCOMPARE(int(e->eol()), int(macpad::core::Eol::Lf));
    }

    // Character sets / Reinterpret as：成功重讀 + 檔案不可讀時的警告分支
    void characterSetsAndReinterpret()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Big5 的「中」= 0xA4 0xA4；以 UTF-8 開啟會是亂碼，改以 Big5 重讀才正確
        const QString path = writeFile(dir, QStringLiteral("big5.txt"),
                                       QByteArray("\xA4\xA4\n"));
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());

        TRIGGER(w, "Encoding", "Big5 (Traditional)");
        QCOMPARE(e->text().trimmed(), QStringLiteral("中"));
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("Big5")));

        // Reinterpret as UTF-8：同一份位元組改以 UTF-8 解讀（內容會變，但不得崩潰）
        TRIGGER(w, "Encoding", "Reinterpret as UTF-8");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已重新解讀"));
        TRIGGER(w, "Encoding", "Reinterpret as UTF-16 LE");
        TRIGGER(w, "Encoding", "Reinterpret as UTF-16 BE");

        // 磁碟上的檔案消失 → 兩條重讀路徑都必須明確報錯（IL-4 失敗快失敗明）
        QVERIFY(QFile::remove(path));
        QString warn;
        {
            ModalPilot pilot({checkMessage(&warn)});
            TRIGGER(w, "Encoding", "Big5 (Traditional)");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(!warn.isEmpty(), "檔案不存在時 Character sets 未報錯");
        warn.clear();
        {
            ModalPilot pilot({checkMessage(&warn)});
            TRIGGER(w, "Encoding", "Reinterpret as UTF-8");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(!warn.isEmpty(), "檔案不存在時 Reinterpret as 未報錯");
    }

    // =====================================================================
    // Tools / Settings / Language 選單
    // =====================================================================

    // 雜湊：以已知輸入驗證輸出真的是該演算法的摘要（不是只確認有彈窗）
    void hashToolsShowDigest()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("abc"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);

        QString digest;
        {
            ModalPilot pilot({checkMessage(&digest)});
            TRIGGER(w, "Tools", "MD5 of selection/document");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(digest, QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));

        // 有選取時只雜湊選取內容
        e->setText(QStringLiteral("abcXXX"));
        e->setSelection(0, 0, 0, 3);
        digest.clear();
        {
            ModalPilot pilot({checkMessage(&digest)});
            TRIGGER(w, "Tools", "SHA-1 of selection/document");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(digest, QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d"));

        for (const char *label : {"SHA-256 of selection/document", "SHA-512 of selection/document"}) {
            digest.clear();
            ModalPilot pilot({checkMessage(&digest)});
            QAction *a = findAction(w, QStringLiteral("Tools"), QString::fromLatin1(label));
            QVERIFY(a);
            a->trigger();
            QCOMPARE(pilot.handled(), 1);
            QVERIFY(!digest.isEmpty());
        }
    }

    // Style Configurator（含 Apply Theme 訊號）與 Select Theme…
    void styleConfiguratorAndThemePicker()
    {
        // 內建主題是由 main.cpp 在啟動時植入的，測試行程沒有走那條路徑，故自行種一個
        macpad::persistence::Theme seed;
        seed.name = QStringLiteral("MenusTestTheme");
        seed.dark = true;
        QVERIFY(macpad::persistence::ThemeStore::save(seed));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const QStringList themes = macpad::persistence::ThemeStore::listThemes();
        QVERIFY(!themes.isEmpty());
        const QString themeName = themes.first();

        // (1) Style Configurator：空名稱的 Apply 必須被忽略；具名則套用並回報
        {
            ModalPilot pilot({[themeName](QDialog *d) {
                auto *sc = qobject_cast<macpad::ui::StyleConfiguratorDialog *>(d);
                QVERIFY(sc);
                emit sc->themeSelected(QString());        // 空名稱 → 忽略
                emit sc->themeSelected(themeName);
                d->accept();
            }});
            TRIGGER(w, "Settings", "Style Configurator…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("樣式已更新"));

        // (2) 取消 → 不回報「樣式已更新」
        w.statusBar()->clearMessage();
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Settings", "Style Configurator…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // (3) Select Theme…：取消 → 不套用
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Settings", "Select Theme…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // (3b) 直接 accept（未選取）→ 名稱為空，同樣不套用
        {
            ModalPilot pilot({acceptDialog()});
            TRIGGER(w, "Settings", "Select Theme…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // (4) 選定主題後按 Apply（onApply 會設定 selectedTheme 並 accept）
        {
            ModalPilot pilot({[](QDialog *d) {
                auto *list = d->findChild<QListWidget *>();
                QVERIFY(list);
                QVERIFY(list->count() > 0);
                list->setCurrentRow(0);
                QMetaObject::invokeMethod(d, "onApply");
            }});
            TRIGGER(w, "Settings", "Select Theme…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(w.statusBar()->currentMessage().contains(themeName),
                 qPrintable(QStringLiteral("未回報套用的主題：%1")
                                .arg(w.statusBar()->currentMessage())));

        // (5) Shortcut Mapper：開啟後取消
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Settings", "Shortcut Mapper…");
            QCOMPARE(pilot.handled(), 1);
        }

        macpad::persistence::ThemeStore::remove(QStringLiteral("MenusTestTheme"));
    }

    // 介面語言：選定後寫入偏好並提示需重啟
    void interfaceLanguageMenu()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QString msg;
        {
            ModalPilot pilot({checkMessage(&msg)});
            TRIGGER(w, "Settings", "日本語");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(macpad::persistence::SettingsStore::load().language, QStringLiteral("ja"));
        QVERIFY2(msg.contains(QStringLiteral("Restart")), qPrintable(msg));

        // 「System Default」= 空字串
        {
            ModalPilot pilot({acceptDialog()});
            TRIGGER(w, "Settings", "System Default");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(macpad::persistence::SettingsStore::load().language.isEmpty());

        // 目前語言應在選單中被勾選（新視窗依偏好重建）
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        QAction *sysDefault = findAction(w2, QStringLiteral("Settings"),
                                         QStringLiteral("System Default"));
        QVERIFY(sysDefault);
        QVERIFY2(sysDefault->isChecked(), "目前語言未於選單中勾選");
    }

    // Set Language：可切換 lexer；偏好中被停用的語言不得出現在選單
    void setLanguageMenuRespectsDisabledLanguages()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.disabledLanguages = QStringList{QStringLiteral("Python")};
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY2(!findAction(w, QStringLiteral("Language"), QStringLiteral("Python")),
                 "已停用的語言仍出現在 Set Language 選單");

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QAction *cpp = findAction(w, QStringLiteral("Language"),
                                  QStringLiteral("C / C++ / Obj-C"));
        QVERIFY2(cpp, "Set Language 選單缺少 C / C++ / Obj-C");
        cpp->trigger();
        QVERIFY2(e->lexer(), "選擇語言後未套用 lexer");
        QVERIFY(QString::fromLatin1(e->lexer()->language()).contains(QStringLiteral("C++")));

        s.disabledLanguages.clear();
        QVERIFY(macpad::persistence::SettingsStore::save(s));
    }

    // UDL：Define Your Language…（建立後即時套用）、匯入 JSON、匯入 Notepad++ XML
    void udlMenuDefineImportAndXml()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 先備妥一個副檔名為 mnu 的 UDL，讓「建立後即時套用到目前檔案」那條路徑可走
        macpad::features::UdlDefinition def;
        def.name = QStringLiteral("MenuTestLang");
        def.extensions = {QStringLiteral("mnu")};
        def.keywords = {QStringLiteral("kw")};
        def.lineComment = QStringLiteral("//");
        macpad::features::UdlManager seed;
        QVERIFY(seed.save(def));

        const QString target = writeFile(dir, QStringLiteral("doc.mnu"), "kw\n");
        QVERIFY(!target.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(target);
        QVERIFY(w.activeEditor() && !w.activeEditor()->isUntitled());

        // (1) 取消 → 不回報
        w.statusBar()->clearMessage();
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Language", "Define Your Language…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // (2) 確認 → 回報並對副檔名相符的目前檔案立即套用 UDL lexer
        {
            ModalPilot pilot({acceptDialog()});
            TRIGGER(w, "Language", "Define Your Language…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("UDL")));
        QVERIFY2(w.activeEditor()->lexer(), "UDL 未套用到副檔名相符的檔案");

        // (3) Import UDL…：合法 JSON
        macpad::features::UdlDefinition imported;
        imported.name = QStringLiteral("ImportedMenuLang");
        imported.extensions = {QStringLiteral("mnu")};
        imported.lineComment = QStringLiteral("#");
        const QString jsonPath = writeFile(dir, QStringLiteral("udl.json"),
                                           QJsonDocument(imported.toJson()).toJson());
        QVERIFY(!jsonPath.isEmpty());
        {
            ModalPilot pilot({chooseFile(jsonPath)});
            TRIGGER(w, "Language", "Import UDL…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(w.statusBar()->currentMessage().contains(QStringLiteral("匯入")),
                 qPrintable(w.statusBar()->currentMessage()));

        // (4) Import UDL…：取消 / 非法 JSON（必須明確警告）
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Language", "Import UDL…");
            QCOMPARE(pilot.handled(), 1);
        }
        const QString badPath = writeFile(dir, QStringLiteral("bad.json"), "{ not json ");
        QVERIFY(!badPath.isEmpty());
        QString warn;
        {
            ModalPilot pilot({chooseFile(badPath), checkMessage(&warn)});
            TRIGGER(w, "Language", "Import UDL…");
            QCOMPARE(pilot.handled(), 2);
        }
        QCOMPARE(warn, QStringLiteral("UDL 檔案無效"));

        // (5) Import Notepad++ UDL (XML)…：合法 XML 與非法 XML
        macpad::features::UdlDefinition xmlDef;
        xmlDef.name = QStringLiteral("XmlMenuLang");
        xmlDef.extensions = {QStringLiteral("mnu")};
        xmlDef.lineComment = QStringLiteral(";");
        const QString xmlPath = dir.filePath(QStringLiteral("udl.xml"));
        QVERIFY(macpad::features::UdlXmlIo::exportToXml(xmlDef, xmlPath));
        w.statusBar()->clearMessage();
        {
            ModalPilot pilot({chooseFile(xmlPath)});
            TRIGGER(w, "Language", "Import Notepad++ UDL (XML)…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(w.statusBar()->currentMessage().contains(QStringLiteral("Notepad++ UDL")),
                 qPrintable(w.statusBar()->currentMessage()));

        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Language", "Import Notepad++ UDL (XML)…");
            QCOMPARE(pilot.handled(), 1);
        }
        const QString badXml = writeFile(dir, QStringLiteral("bad.xml"), "<not-udl/>");
        QVERIFY(!badXml.isEmpty());
        warn.clear();
        {
            ModalPilot pilot({chooseFile(badXml), checkMessage(&warn)});
            TRIGGER(w, "Language", "Import Notepad++ UDL (XML)…");
            QCOMPARE(pilot.handled(), 2);
        }
        QVERIFY2(warn.contains(QStringLiteral("XML")), qPrintable(warn));

        // 清掉本測試植入的 UDL，避免影響其他測試的語言判斷
        macpad::features::UdlManager cleanup;
        cleanup.loadAll();
        cleanup.remove(QStringLiteral("MenuTestLang"));
        cleanup.remove(QStringLiteral("ImportedMenuLang"));
        cleanup.remove(QStringLiteral("XmlMenuLang"));
    }

    // =====================================================================
    // Run 選單
    // =====================================================================

    void runMenuActions()
    {
        QFile::remove(cfg(QStringLiteral("run_commands.json")));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("run.txt"), "alpha beta\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) Run…（未命名文件）→ 建立面板，變數留空
        QVERIFY(!w.findChild<macpad::features::RunDock *>());
        TRIGGER(w, "Run", "Run…");
        auto *dock = w.findChild<macpad::features::RunDock *>();
        QVERIFY2(dock, "Run… 未建立面板");
        QVERIFY(dock->isVisibleTo(&w));

        // (2) Run…（已存檔＋有選取）→ $(CURRENT_WORD) 取選取文字
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());
        e->setSelection(0, 0, 0, 5);
        TRIGGER(w, "Run", "Run…");
        // (3) 無選取 → 取游標所在單字
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 7);
        TRIGGER(w, "Run", "Run…");
        QCOMPARE(w.findChildren<macpad::features::RunDock *>().size(), 1);

        // (4) Saved Commands：尚無指令時只有一個停用的佔位項
        QMenu *saved = findSubmenu(topMenu(w, QStringLiteral("Run")),
                                   QStringLiteral("Saved Commands"));
        QVERIFY2(saved, "找不到 Saved Commands 子選單");
        emit saved->aboutToShow();
        QCOMPARE(saved->actions().size(), 1);
        QVERIFY(!saved->actions().first()->isEnabled());

        // (5) Save Current Command…：命令 → 名稱 → 快捷鍵三段對話框
        {
            ModalPilot pilot({inputText(QStringLiteral("echo saved")),
                              inputText(QStringLiteral("MenuEcho")),
                              [](QDialog *d) {
                                  auto *ks = d->findChild<QKeySequenceEdit *>();
                                  QVERIFY(ks);
                                  ks->setKeySequence(
                                      QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F5")));
                                  d->accept();
                              }});
            TRIGGER(w, "Run", "Save Current Command…");
            QCOMPARE(pilot.handled(), 3);
        }
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("MenuEcho")));
        auto entries = macpad::features::RunCommandStore::load();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().command, QStringLiteral("echo saved"));
        QCOMPARE(entries.first().shortcut, QStringLiteral("Ctrl+Alt+Shift+F5"));

        // 有快捷鍵的指令，Saved Commands 選單上要一併顯示（實際綁定另由 rebuildRunShortcuts 處理）
        QMenu *savedWithKey = findSubmenu(topMenu(w, QStringLiteral("Run")),
                                          QStringLiteral("Saved Commands"));
        QVERIFY(savedWithKey);
        emit savedWithKey->aboutToShow();
        QCOMPARE(savedWithKey->actions().size(), 1);
        QCOMPARE(savedWithKey->actions().first()->shortcut(),
                 QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F5")));

        // (6) 同名覆寫（不新增第二筆）
        {
            ModalPilot pilot({inputText(QStringLiteral("echo replaced")),
                              inputText(QStringLiteral("MenuEcho")),
                              rejectDialog()});
            TRIGGER(w, "Run", "Save Current Command…");
            QCOMPARE(pilot.handled(), 3);
        }
        entries = macpad::features::RunCommandStore::load();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().command, QStringLiteral("echo replaced"));

        // (7) 取消命令 / 取消名稱 → 都不得寫入
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Run", "Save Current Command…");
            QCOMPARE(pilot.handled(), 1);
        }
        {
            ModalPilot pilot({inputText(QStringLiteral("echo x")), rejectDialog()});
            TRIGGER(w, "Run", "Save Current Command…");
            QCOMPARE(pilot.handled(), 2);
        }
        QCOMPARE(macpad::features::RunCommandStore::load().size(), 1);

        // (8) Saved Commands 現在應列出該指令，觸發後於 Run 面板執行
        emit saved->aboutToShow();
        QCOMPARE(saved->actions().size(), 1);
        QCOMPARE(saved->actions().first()->text(), QStringLiteral("MenuEcho"));
        saved->actions().first()->trigger();
        QTest::qWait(50);   // 讓 echo 子行程收尾，避免視窗銷毀時仍在執行
        QFile::remove(cfg(QStringLiteral("run_commands.json")));
    }

    // =====================================================================
    // Macro 選單
    // =====================================================================

    void saveMacroAndSavedMacrosMenu()
    {
        QFile::remove(cfg(QStringLiteral("macros.json")));
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QString());

        // (1) 尚無巨集 → 只提示
        TRIGGER(w, "Macro", "Save Current Recorded Macro…");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("尚無已錄製的巨集"));

        // (2) Saved Macros 為空 → 停用的佔位項
        QMenu *saved = findSubmenu(topMenu(w, QStringLiteral("Macro")),
                                   QStringLiteral("Saved Macros"));
        QVERIFY(saved);
        emit saved->aboutToShow();
        QCOMPARE(saved->actions().size(), 1);
        QVERIFY(!saved->actions().first()->isEnabled());

        // (3) 錄一段巨集後存檔
        TRIGGER(w, "Macro", "Start Recording");
        e->SendScintilla(QsciScintilla::SCI_REPLACESEL, "M");
        TRIGGER(w, "Macro", "Stop Recording");

        {   // 取消命名 → 不寫入
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Macro", "Save Current Recorded Macro…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(!QFile::exists(cfg(QStringLiteral("macros.json")))
                || macpad::persistence::JsonFile::load(cfg(QStringLiteral("macros.json"))).isEmpty());

        {
            ModalPilot pilot({inputText(QStringLiteral("MenuMacro"))});
            TRIGGER(w, "Macro", "Save Current Recorded Macro…");
            QCOMPARE(pilot.handled(), 1);
        }
        const QJsonObject stored =
            macpad::persistence::JsonFile::load(cfg(QStringLiteral("macros.json")));
        QVERIFY2(stored.contains(QStringLiteral("MenuMacro")), "巨集未寫入 macros.json");

        // (4) Saved Macros 列出該巨集，觸發即重播
        emit saved->aboutToShow();
        QCOMPARE(saved->actions().size(), 1);
        QCOMPARE(saved->actions().first()->text(), QStringLiteral("MenuMacro"));
        e->setText(QString());
        saved->actions().first()->trigger();
        QCOMPARE(e->text(), QStringLiteral("M"));

        QFile::remove(cfg(QStringLiteral("macros.json")));
    }

    // =====================================================================
    // View 選單
    // =====================================================================

    void zoomActions()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        const int base = int(e->SendScintilla(QsciScintilla::SCI_GETZOOM));
        TRIGGER(w, "View", "Zoom In");
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETZOOM)), base + 1);
        TRIGGER(w, "View", "Zoom Out");
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETZOOM)), base);
        TRIGGER(w, "View", "Zoom In");
        TRIGGER(w, "View", "Reset Zoom");
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETZOOM)), 0);
    }

    // 檢視開關：縮排參考線 / 換行符號 / Smart Highlighting
    void viewToggles()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        QAction *ig = findAction(w, QStringLiteral("View"), QStringLiteral("Show Indent Guide"));
        QVERIFY(ig && ig->isCheckable());
        ig->setChecked(false);
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETINDENTATIONGUIDES)), 0);
        ig->setChecked(true);
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETINDENTATIONGUIDES)) != 0);

        QAction *wrapSym = findAction(w, QStringLiteral("View"), QStringLiteral("Show Wrap Symbol"));
        QVERIFY(wrapSym && wrapSym->isCheckable());
        wrapSym->setChecked(true);
        QVERIFY(int(e->SendScintilla(QsciScintilla::SCI_GETWRAPVISUALFLAGS)) != 0);
        wrapSym->setChecked(false);
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETWRAPVISUALFLAGS)), 0);

        QAction *sh = findAction(w, QStringLiteral("View"), QStringLiteral("Smart Highlighting"));
        QVERIFY(sh && sh->isCheckable());
        sh->setChecked(true);
        sh->setChecked(false);
    }

    // 分頁導覽：First/Last/Next/Previous 與移動分頁
    void tabNavigationActions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        for (int i = 0; i < 3; ++i) {
            const QString p = writeFile(dir, QStringLiteral("t%1.txt").arg(i), "x\n");
            QVERIFY(!p.isEmpty());
            w.openFile(p);
        }
        auto *tabs = tabsOf(w.activeEditor());
        QVERIFY(tabs);

        TRIGGER(w, "View", "First Tab");
        QCOMPARE(tabs->currentIndex(), 0);
        TRIGGER(w, "View", "Last Tab");
        QCOMPARE(tabs->currentIndex(), tabs->count() - 1);
        TRIGGER(w, "View", "Previous Tab");
        QCOMPARE(tabs->currentIndex(), tabs->count() - 2);
        TRIGGER(w, "View", "Next Tab");
        QCOMPARE(tabs->currentIndex(), tabs->count() - 1);

        const QString last = tabs->tabText(tabs->count() - 1);
        TRIGGER(w, "View", "Move Tab Backward");
        QCOMPARE(tabs->tabText(tabs->count() - 2), last);
        TRIGGER(w, "View", "Move Tab Forward");
        QCOMPARE(tabs->tabText(tabs->count() - 1), last);
    }

    // Document Summary：字元/單字/行數/選取字元四項統計
    void documentSummaryDialog()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("one two\nthree"));
        e->setSelection(0, 0, 0, 3);

        QString text;
        {
            ModalPilot pilot({checkMessage(&text)});
            TRIGGER(w, "View", "Document Summary…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY2(text.contains(QStringLiteral("字元數：13")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("單字數：3")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("行數：2")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("選取字元：3")), qPrintable(text));
    }

    // 分割檢視相關：旋轉方向、同步捲動開關
    void splitViewActions()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        EditorPane *pane = paneOf(e);
        QVERIFY(pane);

        TRIGGER(w, "View", "Rotate Split Orientation");   // 只旋轉容器方向，不應崩潰
        TRIGGER(w, "View", "Rotate Split Orientation");

        // 同步捲動：單一檢視時選單項是停用的（工具列同一個 QAction），
        // 但勾選狀態變更仍須傳到目前 pane——這正是使用者切到雙檢視後所期待的行為。
        QAction *syncV = findAction(w, QStringLiteral("View"),
                                    QStringLiteral("Synchronize Vertical Scrolling"));
        QAction *syncH = findAction(w, QStringLiteral("View"),
                                    QStringLiteral("Synchronize Horizontal Scrolling"));
        QVERIFY(syncV && syncH);
        syncV->setChecked(true);
        QVERIFY2(pane->syncVerticalScroll(), "垂直同步捲動未套用到 pane");
        syncH->setChecked(true);
        QVERIFY(pane->syncHorizontalScroll());
        syncV->setChecked(false);
        syncH->setChecked(false);
        QVERIFY(!pane->syncVerticalScroll());
        QVERIFY(!pane->syncHorizontalScroll());
    }

    // 停靠面板開關（Function List / Clipboard History / Document Map）：
    // 顯示時會即時刷新面板內容
    void dockToggleActions()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("void f() {}\n"));

        QDockWidget *fl = nullptr;
        const auto docks = w.findChildren<QDockWidget *>();
        for (QDockWidget *d : docks)
            if (d->windowTitle() == QStringLiteral("Function List"))
                fl = d;
        QVERIFY2(fl, "找不到 Function List 面板");

        QAction *toggle = fl->toggleViewAction();
        const bool before = fl->isVisible();
        toggle->trigger();
        QTest::qWait(20);
        QCOMPARE(fl->isVisible(), !before);
        toggle->trigger();
        QTest::qWait(20);
        QCOMPARE(fl->isVisible(), before);
        w.hide();
    }

    void fullScreenToggle()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(!w.isFullScreen());
        TRIGGER(w, "View", "Toggle Full Screen");
        QVERIFY2(w.isFullScreen(), "未進入全螢幕");
        TRIGGER(w, "View", "Toggle Full Screen");
        QVERIFY(!w.isFullScreen());
        w.hide();
    }

    // View Current File In ▸ Default Browser：未存檔時只提示。
    // 已存檔的分支會啟動外部瀏覽器，測試環境不觸發。
    void viewInBrowserRequiresSavedFile()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(w.activeEditor() && w.activeEditor()->isUntitled());
        w.statusBar()->clearMessage();
        TRIGGER(w, "View", "Default Browser");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("請先存檔再於瀏覽器開啟"));
    }

    // =====================================================================
    // Search 選單
    // =====================================================================

    // Volatile Find：不改動對話框中記住的查詢字串，直接以選取內容前後搜尋
    void volatileFindNextAndPrevious()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("cat dog cat dog cat"));

        // 第一次觸發時對話框尚未建立——這條 lazy-init 路徑正是要測的（不可假設先開過 Find）
        QVERIFY(!w.findChild<macpad::features::FindReplaceDialog *>());
        TRIGGER(w, "Search", "Find (Volatile) Next");
        auto *dlg = w.findChild<macpad::features::FindReplaceDialog *>();
        QVERIFY2(dlg, "Volatile Find 未建立 Find/Replace 對話框");

        // 查詢字串取自對話框的 Find 欄位（volatile 只是不覆寫「最近一次命中」記錄）。
        // 子 widget 依建立順序取得——原始碼沒有設 objectName（同 test_findreplacedialog 的慣例）。
        const auto combos = dlg->findChildren<QComboBox *>();
        QVERIFY(!combos.isEmpty());
        combos.first()->setCurrentText(QStringLiteral("cat"));

        // 逐次前進：每次都命中一個 cat，且位置嚴格遞增；Previous 則往回退。
        // 用相對關係而非絕對位置——搜尋起點取自「上一次命中之後」，寫死座標會綁死實作細節。
        int lf = 0, if_ = 0, lt = 0, it = 0;
        TRIGGER(w, "Search", "Find (Volatile) Next");
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(e->selectedText(), QStringLiteral("cat"));
        const int first = if_;

        TRIGGER(w, "Search", "Find (Volatile) Next");
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(e->selectedText(), QStringLiteral("cat"));
        QVERIFY2(if_ > first, "Volatile Next 沒有往後前進");
        const int second = if_;

        // 反向搜尋前先把游標收到這一筆的起點——否則反向搜尋會再次命中目前這一筆
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION,
                         e->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART));
        TRIGGER(w, "Search", "Find (Volatile) Previous");
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(e->selectedText(), QStringLiteral("cat"));
        QVERIFY2(if_ < second, "Volatile Previous 沒有往前回退");

        // 反向那一支也有自己的 lazy-init：先按 Previous 的使用者同樣要拿得到對話框
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(!w2.findChild<macpad::features::FindReplaceDialog *>());
        TRIGGER(w2, "Search", "Find (Volatile) Previous");
        QVERIFY(w2.findChild<macpad::features::FindReplaceDialog *>());
    }

    // Replace All in All Opened Documents：跨分頁取代並回報總數
    void replaceAllInAllOpenedDocuments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const QString p1 = writeFile(dir, QStringLiteral("r1.txt"), "aa bb aa\n");
        const QString p2 = writeFile(dir, QStringLiteral("r2.txt"), "aa cc\n");
        QVERIFY(!p1.isEmpty() && !p2.isEmpty());
        w.openFile(p1);
        w.openFile(p2);

        // 取消 Find what → 什麼都不做
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Search", "Replace All in All Opened Documents…");
            QCOMPARE(pilot.handled(), 1);
        }
        // 取消 Replace with → 同樣不做
        {
            ModalPilot pilot({inputText(QStringLiteral("aa")), rejectDialog()});
            TRIGGER(w, "Search", "Replace All in All Opened Documents…");
            QCOMPARE(pilot.handled(), 2);
        }

        {
            ModalPilot pilot({inputText(QStringLiteral("aa")), inputText(QStringLiteral("ZZ"))});
            TRIGGER(w, "Search", "Replace All in All Opened Documents…");
            QCOMPARE(pilot.handled(), 2);
        }
        QCOMPARE(w.statusBar()->currentMessage(),
                 QStringLiteral("已在所有開啟文件中取代 3 處"));

        // 兩份文件都要被改到（不是只改作用中的那一份）
        const auto editors = w.findChildren<EditorWidget *>();
        int replaced = 0;
        for (EditorWidget *ed : editors)
            replaced += ed->text().count(QStringLiteral("ZZ"));
        QCOMPARE(replaced, 3);

        // 唯讀文件必須被跳過——否則會對使用者明確鎖住的檔案動手
        EditorWidget *active = w.activeEditor();
        QVERIFY(active);
        const QString lockedBefore = active->text();
        active->setReadOnly(true);
        {
            ModalPilot pilot({inputText(QStringLiteral("ZZ")), inputText(QStringLiteral("QQ"))});
            TRIGGER(w, "Search", "Replace All in All Opened Documents…");
            QCOMPARE(pilot.handled(), 2);
        }
        QCOMPARE(w.statusBar()->currentMessage(),
                 QStringLiteral("已在所有開啟文件中取代 2 處"));
        QCOMPARE(active->text(), lockedBefore);
        active->setReadOnly(false);
    }

    // Find All in Opened Documents：彙整結果到面板，並可由結果跳回原位置
    void findAllInOpenedDocuments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const QString p1 = writeFile(dir, QStringLiteral("fa1.txt"), "needle here\nsecond needle\n");
        const QString p2 = writeFile(dir, QStringLiteral("fa2.txt"), "no match\n");
        QVERIFY(!p1.isEmpty() && !p2.isEmpty());
        w.openFile(p1);
        w.openFile(p2);

        {   // 取消 → 不建立面板
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Search", "Find All in Opened Documents…");
            QCOMPARE(pilot.handled(), 1);
        }
        QVERIFY(!w.findChild<macpad::features::FindAllDock *>());

        {
            ModalPilot pilot({inputText(QStringLiteral("needle"))});
            TRIGGER(w, "Search", "Find All in Opened Documents…");
            QCOMPARE(pilot.handled(), 1);
        }
        auto *dock = w.findChild<macpad::features::FindAllDock *>();
        QVERIFY2(dock, "Find All 未建立結果面板");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("找到 2 處符合"));

        // 由結果面板跳轉：應切到對應分頁並把游標移到該行
        auto *tabs = tabsOf(w.activeEditor());
        QVERIFY(tabs);
        const int docIdx = tabs->count() - 2;   // fa1.txt（fa2.txt 在最後）
        emit dock->openLocation(docIdx, 2, 8);
        QCOMPARE(tabs->currentIndex(), docIdx);
        int line = -1, col = -1;
        w.activeEditor()->getCursorPosition(&line, &col);
        QCOMPARE(line, 1);
        QCOMPARE(col, 7);

        // 越界的 docId 必須安全忽略（解碼後索引不存在）
        emit dock->openLocation(99999, 1, 1);
        QCOMPARE(tabs->currentIndex(), docIdx);

        // 再次搜尋須重用同一個面板
        {
            ModalPilot pilot({inputText(QStringLiteral("needle"))});
            TRIGGER(w, "Search", "Find All in Opened Documents…");
            QCOMPARE(pilot.handled(), 1);
        }
        QCOMPARE(w.findChildren<macpad::features::FindAllDock *>().size(), 1);
    }

    // =====================================================================
    // Window 選單
    // =====================================================================

    // Window 選單是每次展開才重建的：列出所有開啟文件、勾選目前分頁、可切換
    void windowMenuListsAndSwitchesDocuments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        const QString p1 = writeFile(dir, QStringLiteral("w1.txt"), "one\n");
        const QString p2 = writeFile(dir, QStringLiteral("w2.txt"), "two\n");
        QVERIFY(!p1.isEmpty() && !p2.isEmpty());
        w.openFile(p1);
        w.openFile(p2);

        QMenu *windowMenu = topMenu(w, QStringLiteral("Window"));
        QVERIFY(windowMenu);
        QMetaObject::invokeMethod(&w, "buildWindowMenu");

        QAction *w1 = findIn(windowMenu, QStringLiteral("w1.txt"));
        QAction *w2 = findIn(windowMenu, QStringLiteral("w2.txt"));
        QVERIFY2(w1 && w2, "Window 選單未列出已開啟文件");
        QVERIFY2(w2->isChecked(), "Window 選單未勾選目前分頁");
        QVERIFY(!w1->isChecked());

        w1->trigger();
        QVERIFY(w.activeEditor());
        QCOMPARE(QFileInfo(w.activeEditor()->filePath()), QFileInfo(p1));

        // Next / Previous Document
        TRIGGER(w, "Window", "Next Document");
        QCOMPARE(QFileInfo(w.activeEditor()->filePath()), QFileInfo(p2));
        TRIGGER(w, "Window", "Previous Document");
        QCOMPARE(QFileInfo(w.activeEditor()->filePath()), QFileInfo(p1));

        // Windows…（文件管理對話框）：開啟後取消
        QMetaObject::invokeMethod(&w, "buildWindowMenu");
        {
            ModalPilot pilot({rejectDialog()});
            TRIGGER(w, "Window", "Windows…");
            QCOMPARE(pilot.handled(), 1);
        }
    }

private:
    macpad::persistence::Settings m_savedSettings;
};

// 自訂 main：除了 ModalPilot 的逾時保護，再加一層行程層級的硬看門狗。
// offscreen 下若真有某條路徑開出無法關閉的 modal，測試必須直接死掉而不是永遠掛住。
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
#ifdef Q_OS_UNIX
    alarm(300);
#endif
    TestMainWindowMenus tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_mainwindow_menus.moc"
