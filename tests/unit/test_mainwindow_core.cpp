// 單元測試：MainWindow.cpp（建構子接線層）＋ MainWindow_Session.cpp（session 建立/還原、
// 最近檔案選單、外部檔案異動處理、快照計時器）。
//
// 測試策略：
//  1. 本測試類別「不是」MainWindow 的 friend（friend 名單寫在 src/，不得修改），
//     因此一律走公開 API、可 invoke 的 private slot（moc 會產生入口），或
//     以 findChild()/emit 驅動建構子接上的那些停靠面板訊號——測到的正是真正接在 UI 上的那條線。
//  2. 兩個檢視容器由 centralWidget()（QSplitter）取得；編輯器由 EditorPane 的 findChild 取得。
//  3. session 相關以「SessionStore::save → openSessionFile(公開) → 檢查分頁」端到端驗證，
//     並用 close() 觸發真正的 closeEvent → saveSession 來回驗 buildCurrentSession。
//  4. 會 exec() 的 modal（當機復原對話框、外部異動詢問 QMessageBox）以 driveNextModal()
//     在事件迴圈中接手；一律帶看門狗逾時，任何情況下都不會把測試卡住。
//  5. 已由 test_session_snapshot / test_pintab / test_toolbar 覆蓋的區域不重複測。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <Qsci/qscilexer.h>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "features/backup/BackupService.h"
#include "features/findinfiles/FindInFilesDock.h"
#include "persistence/AppPaths.h"
#include "persistence/RecentFiles.h"
#include "persistence/SessionStore.h"
#include "persistence/SettingsStore.h"
#include "ui/CharacterPanel.h"
#include "ui/DocumentListDock.h"
#include "ui/Panels.h"
#include "ui/SnapshotRecoveryDialog.h"
#include "ui/WorkspaceDock.h"

using macpad::core::EditorWidget;
using macpad::persistence::SessionState;
using macpad::persistence::SettingsStore;
using macpad::persistence::TabState;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

// 兩個檢視容器（m_tabs / m_tabs2）——中央 QSplitter 的第 0/1 個子 widget
static QTabWidget *viewAt(MainWindow &w, int i)
{
    auto *split = qobject_cast<QSplitter *>(w.centralWidget());
    return split ? qobject_cast<QTabWidget *>(split->widget(i)) : nullptr;
}

// 某分頁的 primary 編輯器（EditorPane 的第一個 EditorWidget 子物件）
static EditorWidget *editorOf(QTabWidget *tabs, int index)
{
    QWidget *page = tabs ? tabs->widget(index) : nullptr;
    return page ? page->findChild<EditorWidget *>() : nullptr;
}

// 在某檢視中尋找第一個符合述詞的編輯器
template <typename Pred>
static EditorWidget *findEditor(QTabWidget *tabs, Pred pred)
{
    for (int i = 0; i < tabs->count(); ++i) {
        if (EditorWidget *e = editorOf(tabs, i); e && pred(e))
            return e;
    }
    return nullptr;
}

static QMenu *findMenuByObjectName(MainWindow &w, const QString &name)
{
    const auto menus = w.menuBar()->findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (m->objectName() == name)
            return m;
    }
    return nullptr;
}

static QMenu *findMenuByTitle(MainWindow &w, const QString &title)
{
    const auto menus = w.menuBar()->findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (m->title() == title)
            return m;
    }
    return nullptr;
}

static QAction *findActionByText(QMenu *menu, const QString &text)
{
    if (!menu)
        return nullptr;
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

// 依間隔毫秒數找出建構子建立的計時器（自動存檔／當機快照各有自己的間隔）
static QTimer *timerWithInterval(MainWindow &w, int ms)
{
    const auto timers = w.findChildren<QTimer *>();
    for (QTimer *t : timers) {
        if (t->interval() == ms)
            return t;
    }
    return nullptr;
}

// 在下一個 modal 對話框出現時接手處理，避免 exec() 讓測試永久阻塞。
// fn 通常會 accept()/reject() 讓 exec() 返回；若沒關，這裡強制 reject()。
// 逾時（預設 3 秒）則放棄輪詢——此時代表根本沒有對話框出現，測試不會因此卡住。
static void driveNextModal(const std::function<void(QDialog *)> &fn,
                           bool *fired = nullptr, int timeoutMs = 3000)
{
    auto *timer = new QTimer;
    timer->setInterval(5);
    const QDeadlineTimer deadline(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, fn, fired, deadline] {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            timer->stop();
            timer->deleteLater();
            if (auto *dlg = qobject_cast<QDialog *>(modal)) {
                if (fired)
                    *fired = true;
                fn(dlg);
                if (dlg->isVisible())
                    dlg->reject();   // 保險：handler 沒關掉時強制收尾
            } else {
                modal->close();
            }
            return;
        }
        if (deadline.hasExpired()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

// 常駐 modal 回應器：在存活期間持續把出現的 QMessageBox 按掉指定按鈕。
// 之所以不用一次性的 driveNextModal：MainWindow 內建的 QFileSystemWatcher 也會對同一次
// 檔案寫入發出 fileChanged，而該訊號可能在 QMessageBox::exec() 的巢狀事件迴圈中送達，
// 進而疊出第二個對話框——只處理一次的驅動器會讓測試永久卡住。
class ModalResponder {
public:
    explicit ModalResponder(QMessageBox::StandardButton button) : m_button(button)
    {
        m_timer.setInterval(5);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            ++m_count;
            if (QAbstractButton *b = box->button(m_button))
                b->click();
            else
                box->close();
        });
        m_timer.start();
    }
    ModalResponder(const ModalResponder &) = delete;
    ModalResponder &operator=(const ModalResponder &) = delete;

    int count() const { return m_count; }

private:
    QTimer m_timer;
    QMessageBox::StandardButton m_button;
    int m_count = 0;
};

// 建立一個內容已寫入的暫存檔，回傳絕對路徑
static QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(content);
    f.close();
    return QFileInfo(path).absoluteFilePath();
}

// 設定守衛：測試結束時把偏好設定原樣還原，避免污染後續測試
class SettingsGuard {
public:
    SettingsGuard() : m_saved(SettingsStore::load()) {}
    ~SettingsGuard() { SettingsStore::save(m_saved); }
    SettingsGuard(const SettingsGuard &) = delete;
    SettingsGuard &operator=(const SettingsGuard &) = delete;

    macpad::persistence::Settings settings() const { return m_saved; }

private:
    macpad::persistence::Settings m_saved;
};

class TestMainWindowCore : public QObject {
    Q_OBJECT

private:
    // 專屬設定目錄：設定、session、當機快照全部導向這裡，與使用者真實資料完全隔離
    QTemporaryDir m_cfg;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // QFileDialog/QMessageBox 走 Qt widget 而非 macOS 原生 panel，才能用 timer 驅動
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // 設定/快照/session 全部導向專屬暫存目錄，與使用者真實設定完全隔離
        QVERIFY(m_cfg.isValid());
        macpad::persistence::AppPaths::setConfigDirOverride(m_cfg.path());

        // 預設關閉「外部檔案異動偵測」：多數測試會在編輯器開著檔案時寫入磁碟，
        // 真實的 QFileSystemWatcher 會因此送出 fileChanged 並彈出「是否重新載入」對話框，
        // 讓與該功能無關的測試意外卡住。需要測這條路徑的測試會自行開回來。
        auto s = SettingsStore::load();
        s.autoDetectFileStatus = false;
        s.fileStatusAutoDetect = macpad::persistence::FileStatusAutoDetectMode::Disabled;
        QVERIFY(SettingsStore::save(s));
    }

    void cleanupTestCase()
    {
        macpad::persistence::AppPaths::setConfigDirOverride(QString());
    }

    // 每個測試後清掉殘留快照與 session，否則下一個 MainWindow 建構子會彈出當機復原對話框
    void cleanup()
    {
        macpad::features::BackupService::clearSnapshots();
        macpad::persistence::SessionStore::save(SessionState());
    }

    // ===============================================================
    // MainWindow.cpp — 建構子把停靠面板訊號接到編輯器上的那一整串 lambda
    // ===============================================================

    // 文件清單面板：點選切換分頁、關閉請求關閉分頁，越界索引安全早退
    void documentListSignalsSwitchAndCloseTabs()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = viewAt(w, 0);
        QVERIFY(tabs);
        QMetaObject::invokeMethod(&w, "newFile");
        QMetaObject::invokeMethod(&w, "newFile");
        QCOMPARE(tabs->count(), 3);
        tabs->setCurrentIndex(0);

        auto *docList = w.findChild<macpad::ui::DocumentListDock *>();
        QVERIFY2(docList, "建構子未建立文件清單面板");

        // 越界索引不得動到任何分頁（清單與分頁列可能短暫不同步，這條守衛必須成立）
        emit docList->activated(-1);
        emit docList->activated(999);
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(tabs->count(), 3);

        // 合併索引 2 → 主檢視第 3 個分頁
        emit docList->activated(2);
        QCOMPARE(tabs->currentIndex(), 2);

        // 關閉請求同樣以合併索引解碼；越界時不得關掉任何分頁
        emit docList->closeRequested(-1);
        emit docList->closeRequested(999);
        QCOMPARE(tabs->count(), 3);
        emit docList->closeRequested(1);
        QCOMPARE(tabs->count(), 2);
    }

    // Function List / Clipboard History / Document Map / Character Panel
    // 四個面板的訊號都必須真的作用到目前編輯器
    void panelSignalsActOnCurrentEditor()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("l0\nl1\nl2\nl3\nl4"));
        e->setCursorPosition(0, 0);

        // Function List：雙擊符號 → 游標跳到該行（面板行號為 1-based）
        auto *funcList = w.findChild<macpad::ui::FunctionListDock *>();
        QVERIFY(funcList);
        emit funcList->symbolActivated(3);
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 2);
        QCOMPARE(col, 0);

        // Document Map：點選某行 → 該行必須成為可視（不得改動內容）
        auto *docMap = w.findChild<macpad::ui::DocumentMapDock *>();
        QVERIFY(docMap);
        const QString before = e->text();
        emit docMap->lineClicked(4);
        QCOMPARE(e->text(), before);

        // Clipboard History：雙擊某筆 → 於游標處插入
        auto *clip = w.findChild<macpad::ui::ClipboardHistoryDock *>();
        QVERIFY(clip);
        e->setText(QString());
        e->setCursorPosition(0, 0);
        emit clip->pasteRequested(QStringLiteral("PASTED"));
        QCOMPARE(e->text(), QStringLiteral("PASTED"));

        // Character Panel：選字 → 插入且游標移到插入字之後（否則連續插入會倒序）
        auto *chars = w.findChild<macpad::ui::CharacterPanel *>();
        QVERIFY(chars);
        e->setText(QString());
        e->setCursorPosition(0, 0);
        emit chars->charChosen(QStringLiteral("AB"));
        emit chars->charChosen(QStringLiteral("CD"));
        QCOMPARE(e->text(), QStringLiteral("ABCD"));
        e->getCursorPosition(&line, &col);
        QCOMPARE(col, 4);
    }

    // 工作區右鍵「在此資料夾中尋找」→ 開出範圍限定的 Find in Files 面板，且重複觸發時重用同一個
    void workspaceFindInFolderOpensDockOnce()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto *workspace = w.findChild<macpad::ui::WorkspaceDock *>();
        QVERIFY(workspace);
        QVERIFY2(!w.findChild<macpad::features::FindInFilesDock *>(),
                 "Find in Files 面板應為 lazy-init，尚未觸發時不該存在");

        emit workspace->findInFolderRequested(dir.path());
        auto *dock = w.findChild<macpad::features::FindInFilesDock *>();
        QVERIFY2(dock, "「在此資料夾中尋找」未建立 Find in Files 面板");
        QVERIFY(dock->isVisibleTo(&w));

        emit workspace->findInFolderRequested(dir.path());
        QCOMPARE(w.findChildren<macpad::features::FindInFilesDock *>().size(), 1);
    }

    // 焦點落在第二檢視內 → 該檢視成為作用中（activeEditor 隨之改變）
    void focusTracksActiveView()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *v1 = viewAt(w, 0);
        QTabWidget *v2 = viewAt(w, 1);
        QVERIFY(v1 && v2);

        EditorWidget *first = w.activeEditor();
        QVERIFY(first);
        first->setText(QStringLiteral("IN VIEW ONE"));
        QMetaObject::invokeMethod(&w, "newFile");
        EditorWidget *second = w.activeEditor();
        QVERIFY(second && second != first);
        second->setText(QStringLiteral("IN VIEW TWO"));
        QMetaObject::invokeMethod(&w, "moveToOtherView");
        QCOMPARE(v2->count(), 1);

        // 焦點回到主檢視 → 作用中編輯器換成主檢視的
        emit qApp->focusChanged(nullptr, first);
        QCOMPARE(w.activeEditor(), first);

        // 焦點進入第二檢視 → 作用中編輯器換成第二檢視的
        emit qApp->focusChanged(nullptr, second);
        QCOMPARE(w.activeEditor(), second);

        // 焦點消失（now==nullptr）不得改動作用中檢視
        emit qApp->focusChanged(second, nullptr);
        QCOMPARE(w.activeEditor(), second);
    }

    // Ctrl+Tab / Ctrl+Shift+Tab 文件切換器：偏好開啟時前後切換分頁，關閉時完全不動作
    void documentSwitcherShortcutsRespectPreference()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.docSwitcherEnabled = true;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = viewAt(w, 0);
        QVERIFY(tabs);
        QMetaObject::invokeMethod(&w, "newFile");
        QMetaObject::invokeMethod(&w, "newFile");
        QCOMPARE(tabs->count(), 3);
        tabs->setCurrentIndex(0);

        QShortcut *next = nullptr;
        QShortcut *prev = nullptr;
        const auto shortcuts = w.findChildren<QShortcut *>();
        for (QShortcut *sc : shortcuts) {
            if (sc->key() == QKeySequence(Qt::CTRL | Qt::Key_Tab))
                next = sc;
            else if (sc->key() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab))
                prev = sc;
        }
        QVERIFY2(next && prev, "未註冊 Ctrl+Tab / Ctrl+Shift+Tab 文件切換快捷鍵");

        QMetaObject::invokeMethod(next, "activated");
        QCOMPARE(tabs->currentIndex(), 1);
        QMetaObject::invokeMethod(prev, "activated");
        QCOMPARE(tabs->currentIndex(), 0);

        // 偏好關閉後（設定於觸發當下即時讀取）不得再切換
        s.docSwitcherEnabled = false;
        QVERIFY(SettingsStore::save(s));
        QMetaObject::invokeMethod(next, "activated");
        QCOMPARE(tabs->currentIndex(), 0);
    }

    // ===============================================================
    // MainWindow.cpp — IHostServices（擴充協定）
    // ===============================================================

    // addMenuAction 的三條尋找路徑：objectName 命中 / title 命中 / 都沒有則新建選單
    void addMenuActionResolvesOrCreatesMenu()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) objectName 命中既有選單（翻譯後 title 會變，故以英文鍵為主）
        QMenu *tools = findMenuByObjectName(w, QStringLiteral("Tools"));
        QVERIFY(tools);
        const int toolsBefore = tools->actions().size();
        int calls = 0;
        w.addMenuAction(QStringLiteral("Tools"), QStringLiteral("Plug A"), [&calls] { ++calls; });
        QCOMPARE(tools->actions().size(), toolsBefore + 1);
        QAction *added = findActionByText(tools, QStringLiteral("Plug A"));
        QVERIFY(added);
        added->trigger();
        QCOMPARE(calls, 1);

        // (2) objectName 不符但 title 相符 → 退回 title 比對（不得新建重複選單）
        QMenu *custom = w.menuBar()->addMenu(QStringLiteral("Legacy"));
        QVERIFY(custom->objectName().isEmpty());
        w.addMenuAction(QStringLiteral("Legacy"), QStringLiteral("Plug B"), [&calls] { ++calls; });
        QVERIFY2(findActionByText(custom, QStringLiteral("Plug B")),
                 "title 比對失敗，動作沒有加進既有的 Legacy 選單");
        int legacyMenus = 0;
        const auto menus = w.menuBar()->findChildren<QMenu *>();
        for (QMenu *m : menus)
            if (m->title() == QStringLiteral("Legacy"))
                ++legacyMenus;
        QCOMPARE(legacyMenus, 1);

        // (3) 完全找不到 → 新建選單並設 objectName（第二次呼叫才能命中同一個）
        w.addMenuAction(QStringLiteral("BrandNew"), QStringLiteral("Plug C"), [&calls] { ++calls; });
        QMenu *fresh = findMenuByObjectName(w, QStringLiteral("BrandNew"));
        QVERIFY2(fresh, "找不到既有選單時未新建");
        QVERIFY(findActionByText(fresh, QStringLiteral("Plug C")));
        w.addMenuAction(QStringLiteral("BrandNew"), QStringLiteral("Plug D"), [&calls] { ++calls; });
        QCOMPARE(fresh->actions().size(), 2);
        findActionByText(fresh, QStringLiteral("Plug D"))->trigger();
        QCOMPARE(calls, 2);
    }

    void showStatusMessageGoesToStatusBar()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.statusBar()->clearMessage();
        w.showStatusMessage(QStringLiteral("來自擴充的訊息"), 5000);
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("來自擴充的訊息"));
    }

    // ===============================================================
    // MainWindow.cpp — 自動儲存 / 失焦儲存 / 當機快照計時器
    // ===============================================================

    // 啟用自動儲存 → 建構子建立對應間隔的計時器；觸發後已命名且有未存變更的分頁真的被寫回磁碟
    void autosaveTimerWritesDirtyNamedFiles()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.autosaveEnabled = true;
        s.autosaveIntervalSec = 7;      // 用非預設值，才能由 interval 明確認出這個計時器
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("auto.txt"), "ON DISK");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTimer *timer = timerWithInterval(w, 7 * 1000);
        QVERIFY2(timer, "啟用自動儲存後未建立對應間隔的計時器");
        QVERIFY(timer->isActive());

        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());
        e->setText(QStringLiteral("EDITED"));
        QVERIFY(e->isDirty());

        QMetaObject::invokeMethod(timer, "timeout");

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("EDITED"));
        QVERIFY2(!e->isDirty(), "自動儲存後仍標記為未存");
    }

    // 未啟用自動儲存時不得建立該計時器（否則會無謂地定期掃描全部分頁）
    void autosaveTimerAbsentWhenDisabled()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.autosaveEnabled = false;
        s.autosaveIntervalSec = 7;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(!timerWithInterval(w, 7 * 1000));
    }

    // 失焦自動儲存（FR-053）：視窗非作用中時寫回已命名的未存分頁；偏好關閉時不動作
    void focusLossAutosaveRespectsPreference()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.autosaveOnFocusLoss = false;
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("focus.txt"), "ON DISK");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());
        e->setText(QStringLiteral("FIRST EDIT"));

        // (1) 偏好關閉 → 失焦不得寫檔
        emit qApp->applicationStateChanged(Qt::ApplicationInactive);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("ON DISK"));
        }

        // (2) 偏好開啟 → 失焦即寫回磁碟
        s.autosaveOnFocusLoss = true;
        QVERIFY(SettingsStore::save(s));
        emit qApp->applicationStateChanged(Qt::ApplicationInactive);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("FIRST EDIT"));
        }
        QVERIFY(!e->isDirty());

        // (3) 重新成為作用視窗不是「失焦」，不得觸發儲存
        e->setText(QStringLiteral("SECOND EDIT"));
        emit qApp->applicationStateChanged(Qt::ApplicationActive);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("FIRST EDIT"));
        }
    }

    // 當機復原快照計時器：只寫出有未存變更的分頁；untitled 以序號、已命名以檔名作為快照識別碼
    void snapshotTimerWritesDirtyDocumentsOnly()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.enableSessionSnapshot = true;
        s.snapshotIntervalSec = 11;     // 非預設值，供 interval 辨識
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("snap.txt"), "ON DISK");
        QVERIFY(!path.isEmpty());

        macpad::features::BackupService::clearSnapshots();
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTimer *timer = timerWithInterval(w, 11 * 1000);
        QVERIFY2(timer, "啟用快照後未建立對應間隔的計時器");
        QVERIFY(timer->isActive());

        EditorWidget *untitled = w.activeEditor();
        QVERIFY(untitled && untitled->isUntitled());
        untitled->setText(QStringLiteral("UNSAVED DRAFT"));

        w.openFile(path);
        EditorWidget *named = w.activeEditor();
        QVERIFY(named && !named->isUntitled());
        named->setText(QStringLiteral("DIRTY NAMED"));

        // 第三個分頁乾淨無變更 → 不該產生快照
        QMetaObject::invokeMethod(&w, "newFile");

        QMetaObject::invokeMethod(timer, "timeout");

        const QStringList pending = macpad::features::BackupService::pendingSnapshots();
        QCOMPARE(pending.size(), 2);
        QVERIFY2(pending.contains(QStringLiteral("session/untitled-0")),
                 qPrintable(QStringLiteral("未以序號記錄 untitled 快照：%1").arg(pending.join('|'))));
        QVERIFY2(pending.contains(QStringLiteral("session/snap.txt")),
                 qPrintable(QStringLiteral("未以檔名記錄已命名檔快照：%1").arg(pending.join('|'))));
        QCOMPARE(QString::fromUtf8(macpad::features::BackupService::readSnapshot(
                     QStringLiteral("session/untitled-0"))),
                 QStringLiteral("UNSAVED DRAFT"));

        macpad::features::BackupService::clearSnapshots();
    }

    // 關閉快照偏好 → 建構子即停用計時器（applySnapshotTimerSettings 的 else 分支）
    void snapshotTimerStoppedWhenDisabled()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.enableSessionSnapshot = false;
        s.snapshotIntervalSec = 13;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY2(!timerWithInterval(w, 13 * 1000),
                 "停用快照時仍套用了間隔（計時器不該被啟動）");
        // 計時器物件本身仍在（供之後於 Preferences 即時啟用），但必須是停止狀態。
        // 自動存檔/快照的間隔一律被夾限在 [5,3600] 秒，因此「沒有任何 >=5 秒的計時器在跑」
        // 即代表這兩個週期性工作都沒被啟動（面板節流等 UI 短間隔計時器不在此列）。
        const auto timers = w.findChildren<QTimer *>();
        for (QTimer *t : timers) {
            QVERIFY2(!(t->isActive() && t->interval() >= 5000),
                     "停用快照後仍有週期性計時器在跑");
        }
    }

    // ===============================================================
    // MainWindow.cpp — 當機復原對話框（建構子中 exec）
    // ===============================================================

    // 「還原所選」：快照內容與現有分頁都不同 → 開成新分頁，且還原後清除快照避免重覆提示
    void crashRecoveryRestoresSnapshotIntoNewTab()
    {
        macpad::features::BackupService::clearSnapshots();
        macpad::persistence::SessionStore::save(SessionState());   // 無 session，僅有當機快照
        const QString draft = QStringLiteral("RECOVERED CRASH DRAFT");
        QVERIFY(macpad::features::BackupService::writeSnapshot(
            QStringLiteral("session"), QStringLiteral("untitled-0"), draft.toUtf8()));

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *rec = qobject_cast<macpad::ui::SnapshotRecoveryDialog *>(dlg);
            QVERIFY(rec);
            auto *list = rec->findChild<QListWidget *>();
            QVERIFY(list);
            list->setCurrentRow(0);
            const auto buttons = rec->findChildren<QPushButton *>();
            for (QPushButton *b : buttons) {
                if (b->text() == QStringLiteral("Restore Selected")) {
                    b->click();
                    return;
                }
            }
            QFAIL("當機復原對話框沒有「Restore Selected」按鈕");
        }, &fired);

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/true);
        QVERIFY2(fired, "有殘留快照時未彈出當機復原對話框");

        QTabWidget *tabs = viewAt(w, 0);
        QVERIFY(tabs);
        QVERIFY2(findEditor(tabs, [&](EditorWidget *e) { return e->text() == draft; }),
                 "選擇還原後快照內容未開成分頁");
        QVERIFY2(macpad::features::BackupService::pendingSnapshots().isEmpty(),
                 "還原後未清除快照，下次啟動會重覆提示");
    }

    // 「全部捨棄」：不開分頁，且快照必須被清空
    void crashRecoveryDiscardAllClearsSnapshots()
    {
        macpad::features::BackupService::clearSnapshots();
        macpad::persistence::SessionStore::save(SessionState());
        const QString draft = QStringLiteral("DISCARD ME");
        QVERIFY(macpad::features::BackupService::writeSnapshot(
            QStringLiteral("session"), QStringLiteral("untitled-9"), draft.toUtf8()));

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            const auto buttons = dlg->findChildren<QPushButton *>();
            for (QPushButton *b : buttons) {
                if (b->text() == QStringLiteral("Discard All")) {
                    b->click();
                    return;
                }
            }
            QFAIL("當機復原對話框沒有「Discard All」按鈕");
        }, &fired);

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/true);
        QVERIFY(fired);

        QTabWidget *tabs = viewAt(w, 0);
        QVERIFY(tabs);
        QVERIFY2(!findEditor(tabs, [&](EditorWidget *e) { return e->text() == draft; }),
                 "選擇全部捨棄後仍把快照開成分頁");
        QVERIFY(macpad::features::BackupService::pendingSnapshots().isEmpty());
    }

    // ===============================================================
    // MainWindow_Session.cpp — buildCurrentSession
    // ===============================================================

    // 完整欄位往返：語言鍵、選取範圍、書籤、游標、釘選、clone 不重覆持久化。
    // 以 close() 觸發真正的 closeEvent → saveSession，確保測到的是實際會被寫出的內容。
    void buildCurrentSessionCapturesFullTabState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("sample.cpp"),
                                       "int a;\nint b;\nint c;\nint d;\n");
        QVERIFY(!path.isEmpty());

        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.openFile(path);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e && !e->isUntitled());
            QVERIFY2(e->lexer(), "開 .cpp 檔後未套用 lexer，無法驗證語言鍵反查");

            e->setCursorPosition(2, 0);
            e->toggleBookmark();               // 第 2 行加書籤
            e->setSelection(1, 0, 1, 5);       // 選取第 1 行前 5 個字元（游標即落在 1,5）

            // clone 到第二檢視：與來源共享文件，不得被獨立持久化成第二個分頁
            QMetaObject::invokeMethod(&w, "cloneToOtherView");
            QCOMPARE(viewAt(w, 1)->count(), 1);

            w.close();   // closeEvent → saveSession
        }

        const SessionState st = macpad::persistence::SessionStore::load();
        QCOMPARE(st.tabs.size(), 1);           // clone 已被略過
        const TabState &t = st.tabs.at(0);
        QCOMPARE(t.path, path);
        QVERIFY(!t.untitled);
        QCOMPARE(t.languageOverride, QStringLiteral("cpp"));
        QCOMPARE(t.selection, QStringLiteral("1,0,1,5"));
        QCOMPARE(t.bookmarks, QList<int>{2});
        QCOMPARE(t.line, 1);
        QCOMPARE(t.index, 5);
        QVERIFY(!t.pinned);
    }

    // ===============================================================
    // MainWindow_Session.cpp — openSessionState（經由公開的 openSessionFile）
    // ===============================================================

    // 逐一還原 FR-052 的每個欄位：書籤、選取、語言覆寫、釘選，且釘選狀態能再次寫回 session
    void openSessionRestoresBookmarksSelectionLanguageAndPin()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("restore.txt"),
                                       "line0\nline1\nline2\nline3\nline4\n");
        QVERIFY(!path.isEmpty());

        SessionState st;
        TabState t;
        t.path = path;
        t.line = 3;
        t.index = 2;
        t.firstVisibleLine = 1;
        t.selection = QStringLiteral("1,1,2,3");
        t.bookmarks = QList<int>{1, 3};
        t.languageOverride = QStringLiteral("python");   // 與 .txt 副檔名不同，證明覆寫確實生效
        t.pinned = true;
        st.tabs = {t};
        QVERIFY(macpad::persistence::SessionStore::save(st));
        const QString sessionPath = macpad::persistence::AppPaths::filePath(
            QStringLiteral("session.json"));

        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.openSessionFile(QString());        // 空路徑為 no-op，不得動到分頁
            QTabWidget *tabs = viewAt(w, 0);
            const int before = tabs->count();
            w.openSessionFile(sessionPath);
            QCOMPARE(tabs->count(), before + 1);

            EditorWidget *e = findEditor(tabs, [&](EditorWidget *x) { return !x->isUntitled(); });
            QVERIFY2(e, "session 中的已命名檔未還原");
            QCOMPARE(e->filePath(), path);

            // 書籤
            QCOMPARE(e->bookmarkedLines(), (QList<int>{1, 3}));
            // 選取範圍
            int aLine = -1, aIdx = -1, cLine = -1, cIdx = -1;
            e->getSelection(&aLine, &aIdx, &cLine, &cIdx);
            QCOMPARE(aLine, 1);
            QCOMPARE(aIdx, 1);
            QCOMPARE(cLine, 2);
            QCOMPARE(cIdx, 3);
            // 語言覆寫：.txt 本來不會有 Python lexer
            QVERIFY(e->lexer());
            QCOMPARE(QString::fromLatin1(e->lexer()->language()), QStringLiteral("Python"));
            // 註：firstVisibleLine 於還原後會再被 setSelection 的捲動調整，故不對它斷言。

            w.close();   // 再存一次 session，驗證釘選旗標真的被設起來
        }

        const SessionState round = macpad::persistence::SessionStore::load();
        QCOMPARE(round.tabs.size(), 1);
        QVERIFY2(round.tabs.at(0).pinned, "釘選狀態未於還原時設回，跨 session 會遺失");
    }

    // 磁碟上已不存在的分頁：無未存快照者直接略過；有快照者以快照內容救回並提示原始檔名
    void openSessionSkipsMissingFilesButRescuesUnsaved()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        SessionState st;
        TabState gone;                                   // 已刪除且無未存內容 → 略過
        gone.path = dir.filePath(QStringLiteral("vanished.txt"));
        TabState rescued;                                // 已刪除但有未存內容 → 救回
        rescued.path = dir.filePath(QStringLiteral("rescued.txt"));
        rescued.dirty = true;
        rescued.unsavedContent = QStringLiteral("WORK IN PROGRESS");
        st.tabs = {gone, rescued};
        QVERIFY(macpad::persistence::SessionStore::save(st));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = viewAt(w, 0);
        const int before = tabs->count();
        w.openSessionFile(macpad::persistence::AppPaths::filePath(QStringLiteral("session.json")));

        QCOMPARE(tabs->count(), before + 1);             // 只還原了「有未存內容」的那一個
        EditorWidget *e = findEditor(tabs, [](EditorWidget *x) {
            return x->text() == QStringLiteral("WORK IN PROGRESS");
        });
        QVERIFY2(e, "已刪除檔案的未存快照沒有被救回");
        QVERIFY2(e->isUntitled(), "救回的內容應退化為 untitled 緩衝（原檔已不存在）");
        QVERIFY2(w.statusBar()->currentMessage().contains(QStringLiteral("rescued.txt")),
                 "未提示使用者哪些內容是從已刪除的檔案救回的");
    }

    // 讀檔失敗（此處以「路徑其實是資料夾」重現）不得中斷還原，也不得留下半殘的空分頁
    void openSessionSkipsUnreadableFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subdir = dir.filePath(QStringLiteral("adirectory"));
        QVERIFY(QDir().mkpath(subdir));
        const QString good = writeFile(dir, QStringLiteral("good.txt"), "GOOD CONTENT");
        QVERIFY(!good.isEmpty());

        SessionState st;
        TabState bad;
        bad.path = QFileInfo(subdir).absoluteFilePath();   // 存在，但 loadFile 必定失敗
        TabState ok;
        ok.path = good;
        st.tabs = {bad, ok};
        QVERIFY(macpad::persistence::SessionStore::save(st));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = viewAt(w, 0);
        const int before = tabs->count();
        w.openSessionFile(macpad::persistence::AppPaths::filePath(QStringLiteral("session.json")));

        QCOMPARE(tabs->count(), before + 1);               // 失敗那個沒有留下分頁
        QVERIFY2(findEditor(tabs, [&](EditorWidget *e) { return e->filePath() == good; }),
                 "讀檔失敗中斷了後續分頁的還原");
    }

    // Folder as Workspace 還原：已不存在的根資料夾自動略過，有效者加回並顯示側欄
    void openSessionRestoresWorkspaceRoots()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString real = QFileInfo(dir.path()).absoluteFilePath();
        const QString bogus = QFileInfo(dir.filePath(QStringLiteral("nope"))).absoluteFilePath();

        SessionState st;
        st.workspaceRoots = {bogus, real};      // 失效路徑排前面，確保它被略過而非中斷
        st.workspaceExpanded = {real};
        QVERIFY(macpad::persistence::SessionStore::save(st));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto *workspace = w.findChild<macpad::ui::WorkspaceDock *>();
        QVERIFY(workspace);
        QVERIFY(!workspace->isVisibleTo(&w));

        w.openSessionFile(macpad::persistence::AppPaths::filePath(QStringLiteral("session.json")));

        QVERIFY2(workspace->roots().contains(real), "有效的工作區根資料夾未還原");
        QVERIFY2(!workspace->roots().contains(bogus), "已不存在的資料夾不應塞進側欄");
        QVERIFY2(workspace->isVisibleTo(&w), "還原工作區後側欄未顯示");
    }

    // ===============================================================
    // MainWindow_Session.cpp — focusExistingPath
    // ===============================================================

    // 已開啟的檔案不重覆開啟，而是聚焦既有分頁；clone 分頁不算「本檔的正本」
    void openFileFocusesExistingTabAcrossViews()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("once.txt"), "ONLY ONCE");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *v1 = viewAt(w, 0);
        QTabWidget *v2 = viewAt(w, 1);
        QVERIFY(v1 && v2);

        w.openFile(path);
        const int afterFirst = v1->count();
        QMetaObject::invokeMethod(&w, "newFile");
        QVERIFY(v1->currentIndex() != v1->indexOf(v1->widget(0)) || v1->count() > afterFirst);

        // 再開一次同一個檔 → 不新增分頁，只切回既有分頁
        w.openFile(path);
        QCOMPARE(v1->count(), afterFirst + 1);   // 只多了剛才那張空白分頁
        EditorWidget *cur = w.activeEditor();
        QVERIFY(cur);
        QCOMPARE(cur->filePath(), path);

        // 把正本搬到第二檢視、再 clone 回主檢視：主檢視先掃到的是 clone，
        // 必須跳過它繼續找到第二檢視的正本，而不是把 clone 當成本檔而聚焦錯分頁。
        QMetaObject::invokeMethod(&w, "moveToOtherView");
        QCOMPARE(v2->count(), 1);
        QMetaObject::invokeMethod(&w, "cloneToOtherView");
        const int v1Count = v1->count();
        const int v2Count = v2->count();

        w.openFile(path);
        QCOMPARE(v1->count(), v1Count);          // 沒有新增任何分頁
        QCOMPARE(v2->count(), v2Count);
        QCOMPARE(w.activeEditor()->filePath(), path);
    }

    // 空路徑不可能命中既有分頁（且不得誤把某個 untitled 分頁當成命中）
    void openEmptyPathReportsFailureWithoutMatching()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = viewAt(w, 0);
        QVERIFY(tabs);
        const int before = tabs->count();

        // openFile("") 最終會以「無法開啟」警告收尾——接手關掉它，不讓測試卡住
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *box = qobject_cast<QMessageBox *>(dlg);
            QVERIFY(box);
            box->close();
        }, &fired);
        w.openFile(QString());
        QVERIFY2(fired, "開啟空路徑時未出現錯誤提示");
        QCOMPARE(tabs->count(), before);
    }

    // ===============================================================
    // MainWindow_Session.cpp — rebuildRecentMenu
    // ===============================================================

    // recentFilesMaxEntries=0 → 停用追蹤，子選單只留一個停用中的說明項
    void recentMenuDisabledWhenMaxEntriesZero()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.recentFilesMaxEntries = 0;
        QVERIFY(SettingsStore::save(s));
        macpad::persistence::RecentFiles::clear();

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QMenu *recent = findMenuByTitle(w, QStringLiteral("Open Recent"));
        QVERIFY(recent);
        QVERIFY(recent->menuAction()->isVisible());
        QCOMPARE(recent->actions().size(), 1);
        QVERIFY2(!recent->actions().at(0)->isEnabled(), "停用說明項不該可點");
    }

    // 子選單模式：無最近檔案時顯示停用說明；有檔案時逐項列出並附「Clear Menu」
    void recentMenuSubmenuModeListsAndClears()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.recentFilesMaxEntries = 2;         // 上限裁切也一併驗證
        s.recentFilesInSubmenu = true;
        s.recentFilesShowFullPath = false;
        QVERIFY(SettingsStore::save(s));
        macpad::persistence::RecentFiles::clear();

        // (1) 無最近檔案
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            QMenu *recent = findMenuByTitle(w, QStringLiteral("Open Recent"));
            QVERIFY(recent);
            QVERIFY(recent->menuAction()->isVisible());
            QCOMPARE(recent->actions().size(), 1);
            QVERIFY(!recent->actions().at(0)->isEnabled());
        }

        // (2) 有三筆最近檔案 → 只列出前 2 筆（上限），顯示檔名而非完整路徑，並附 Clear Menu
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = writeFile(dir, QStringLiteral("r1.txt"), "1");
        const QString p2 = writeFile(dir, QStringLiteral("r2.txt"), "2");
        const QString p3 = writeFile(dir, QStringLiteral("r3.txt"), "3");
        QVERIFY(!p1.isEmpty() && !p2.isEmpty() && !p3.isEmpty());
        macpad::persistence::RecentFiles::add(p1);
        macpad::persistence::RecentFiles::add(p2);
        macpad::persistence::RecentFiles::add(p3);

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QMenu *recent = findMenuByTitle(w, QStringLiteral("Open Recent"));
        QVERIFY(recent);
        // 2 筆檔案 + 分隔線 + Clear Menu
        QCOMPARE(recent->actions().size(), 4);
        QCOMPARE(recent->actions().at(0)->text(), QStringLiteral("r3.txt"));  // 最新的在最前
        QCOMPARE(recent->actions().at(1)->text(), QStringLiteral("r2.txt"));

        // 點某一筆 → 真的開起該檔
        recent->actions().at(1)->trigger();
        QVERIFY(w.activeEditor());
        QCOMPARE(w.activeEditor()->filePath(), p2);

        // Clear Menu → 清空紀錄並就地重建選單
        QAction *clear = findActionByText(recent, QStringLiteral("Clear Menu"));
        QVERIFY(clear);
        clear->trigger();
        QVERIFY(macpad::persistence::RecentFiles::load().isEmpty());
        QCOMPARE(recent->actions().size(), 1);
        QVERIFY(!recent->actions().at(0)->isEnabled());
    }

    // 直接列於 File 選單模式：子選單入口隱藏，項目插在錨點前，並附「Clear Recent Files」
    void recentMenuDirectModeListsInFileMenu()
    {
        SettingsGuard guard;
        auto s = guard.settings();
        s.recentFilesMaxEntries = 10;
        s.recentFilesInSubmenu = false;
        s.recentFilesShowFullPath = true;     // 直接模式順便驗證完整路徑標籤
        QVERIFY(SettingsStore::save(s));
        macpad::persistence::RecentFiles::clear();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = writeFile(dir, QStringLiteral("d1.txt"), "1");
        QVERIFY(!p1.isEmpty());
        macpad::persistence::RecentFiles::add(p1);

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QMenu *recent = findMenuByTitle(w, QStringLiteral("Open Recent"));
        QVERIFY(recent);
        QVERIFY2(!recent->menuAction()->isVisible(),
                 "直接列於 File 選單時，子選單入口應隱藏（僅作為插入錨點）");

        QMenu *fileMenu = findMenuByObjectName(w, QStringLiteral("File"));
        QVERIFY(fileMenu);
        QAction *entry = findActionByText(fileMenu, p1);   // 完整路徑作為標籤
        QVERIFY2(entry, "最近檔案未直接列於 File 選單");
        QVERIFY(findActionByText(fileMenu, QStringLiteral("Clear Recent Files")));

        entry->trigger();
        QVERIFY(w.activeEditor());
        QCOMPARE(w.activeEditor()->filePath(), p1);

        // 開檔會就地重建最近檔案項目（舊的 QAction 已被 delete），因此必須重新查一次，
        // 不能沿用觸發前拿到的指標。
        QAction *clear = findActionByText(fileMenu, QStringLiteral("Clear Recent Files"));
        QVERIFY(clear);
        clear->trigger();
        QVERIFY(macpad::persistence::RecentFiles::load().isEmpty());
        QVERIFY2(!findActionByText(fileMenu, p1),
                 "清除後舊的最近檔案項目仍留在 File 選單");
    }

    // ===============================================================
    // MainWindow_Session.cpp — onFileChangedOnDisk（外部檔案異動）
    // ===============================================================

    // 未開啟的檔案發生異動時安全早退；檔案被刪除時只提示不重載
    void fileChangedIgnoresUnknownAndReportsDeletion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("watched.txt"), "ORIGINAL");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());

        // (1) 不屬於任何分頁的路徑 → 什麼都不做
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk",
                                  Q_ARG(QString, dir.filePath(QStringLiteral("stranger.txt"))));
        QVERIFY(w.statusBar()->currentMessage().isEmpty());
        QCOMPARE(e->text(), QStringLiteral("ORIGINAL"));

        // (2) 檔案在磁碟上被刪除 → 提示，不清空編輯器內容
        QVERIFY(QFile::remove(path));
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
        QVERIFY2(w.statusBar()->currentMessage().contains(QStringLiteral("被刪除")),
                 qPrintable(w.statusBar()->currentMessage()));
        QCOMPARE(e->text(), QStringLiteral("ORIGINAL"));
    }

    // 偵測模式三態：Disabled 不處理、EnabledSilent 靜默重載、Enabled 詢問後才重載
    void fileChangedHonoursAutoDetectModes()
    {
        using macpad::persistence::FileStatusAutoDetectMode;
        SettingsGuard guard;
        auto s = guard.settings();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("modes.txt"), "V1");
        QVERIFY(!path.isEmpty());

        // (1) New Document 頁的總開關關閉 → 完全不處理
        s.autoDetectFileStatus = false;
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Enabled;
        QVERIFY(SettingsStore::save(s));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QCOMPARE(e->text(), QStringLiteral("V1"));

        QVERIFY(!writeFile(dir, QStringLiteral("modes.txt"), "V2").isEmpty());
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
        QCOMPARE(e->text(), QStringLiteral("V1"));   // 沒有重載

        // (2) MISC 頁設為 Disabled → 同樣不處理
        s.autoDetectFileStatus = true;
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Disabled;
        QVERIFY(SettingsStore::save(s));
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
        QCOMPARE(e->text(), QStringLiteral("V1"));

        // (3) EnabledSilent → 靜默重載，不彈任何對話框（若彈了，這個測試會卡住）
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::EnabledSilent;
        QVERIFY(SettingsStore::save(s));
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
        QCOMPARE(e->text(), QStringLiteral("V2"));

        // (4) Enabled → 詢問；回答 No 不重載
        QVERIFY(!writeFile(dir, QStringLiteral("modes.txt"), "V3").isEmpty());
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Enabled;
        QVERIFY(SettingsStore::save(s));
        {
            ModalResponder no(QMessageBox::No);
            QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
            QVERIFY2(no.count() > 0, "Enabled 模式未詢問使用者");
        }
        QCOMPARE(e->text(), QStringLiteral("V2"));

        // (5) Enabled → 回答 Yes 才重載
        {
            ModalResponder yes(QMessageBox::Yes);
            QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));
            QVERIFY(yes.count() > 0);
            QCOMPARE(e->text(), QStringLiteral("V3"));
        }

        // 收尾：關掉偵測並排空事件佇列，讓真實 watcher 可能殘留的 fileChanged 成為 no-op，
        // 不會在下一個測試裡冒出對話框。
        s.autoDetectFileStatus = false;
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Disabled;
        QVERIFY(SettingsStore::save(s));
        QTest::qWait(100);
    }

    // Monitoring（tail -f）：無論偵測模式為何都靜默重載，並把游標帶到檔尾
    void monitoredFileReloadsSilentlyAndTails()
    {
        using macpad::persistence::FileStatusAutoDetectMode;
        SettingsGuard guard;
        auto s = guard.settings();
        // 刻意設成「會詢問」的模式——監控中仍必須靜默處理（彈對話框則本測試會逾時失敗）
        s.autoDetectFileStatus = true;
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Enabled;
        QVERIFY(SettingsStore::save(s));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("tail.log"), "line1\n");
        QVERIFY(!path.isEmpty());

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());

        QMetaObject::invokeMethod(&w, "toggleMonitoring");
        QVERIFY2(e->isReadOnly(), "開始監控後應為唯讀（tail -f 語意）");

        QVERIFY(!writeFile(dir, QStringLiteral("tail.log"), "line1\nline2\nline3\n").isEmpty());
        QMetaObject::invokeMethod(&w, "onFileChangedOnDisk", Q_ARG(QString, path));

        QVERIFY2(e->text().contains(QStringLiteral("line3")), "監控中未自動重載新內容");
        QVERIFY2(e->isReadOnly(), "重載後未還原唯讀狀態");
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, e->lines() - 1);   // 捲到檔尾

        QMetaObject::invokeMethod(&w, "toggleMonitoring");   // 收尾，解除監控

        s.autoDetectFileStatus = false;
        s.fileStatusAutoDetect = FileStatusAutoDetectMode::Disabled;
        QVERIFY(SettingsStore::save(s));
        QTest::qWait(100);
    }
};

QTEST_MAIN(TestMainWindowCore)
#include "test_mainwindow_core.moc"
