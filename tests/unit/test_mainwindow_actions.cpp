// 單元測試：MainWindow_Actions.cpp——MainWindow 的命令實作層
// （巨集錄製/播放、文字操作、行搬移、增量搜尋、搜尋命令、Plugins Admin、Macro Manager、
//  Go to…、已儲存 Run 指令快捷鍵…）。
//
// 測試策略：
//  1. 這一層幾乎都是 private / private slot，測試類別不是 MainWindow 的 friend，
//     因此一律走「使用者實際走的路徑」——選單 QAction 觸發，或對 private slot 用
//     QMetaObject::invokeMethod（moc 會為 private slot 產生 invoke 入口）。
//     這樣測到的是真正接在 UI 上的那條線，而不是繞過 UI 的內部捷徑。
//  2. 斷言看「編輯器內容/游標/持久化檔案」的實際變化，而不是只確認呼叫沒崩潰。
//  3. 會 exec() 的自建 QDialog（Run a Macro Multiple Times / Plugins Admin /
//     Macro Manager / Go to…）以 driveNextModal() 在事件迴圈中接手：找到 modal
//     widget → 填值 → accept/reject。逾時會強制關閉，任何情況下都不會把測試卡住。
//  4. 刻意不測的動作（會開系統級 modal 或真的送印/開外部程式），於各處註解說明。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QRadioButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <Qsci/qscimacro.h>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "features/run/RunCommand.h"
#include "features/run/RunDock.h"
#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"
#include "persistence/PluginStore.h"
#include "persistence/SettingsStore.h"
#include "ui/MacroManagerDialog.h"

using macpad::core::EditorWidget;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

// 遞迴尋找選單列中文字為 text 的動作（不含子選單標題本身以外的比對差異）。
// 用文字而非 objectName：這些選單項沒有設 objectName，而測試不載入翻譯，
// tr() 會原樣回傳原始字串。
static QAction *findMenuAction(QMenu *menu, const QString &text)
{
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        if (a->text() == text)
            return a;
        if (a->menu()) {
            if (QAction *sub = findMenuAction(a->menu(), text))
                return sub;
        }
    }
    return nullptr;
}

static QAction *findMenuAction(MainWindow &w, const QString &text)
{
    const auto tops = w.menuBar()->actions();
    for (QAction *top : tops) {
        if (top->menu()) {
            if (QAction *a = findMenuAction(top->menu(), text))
                return a;
        }
    }
    return nullptr;
}

// 觸發選單動作；找不到就讓測試失敗（選單改名時要立刻知道，而不是靜靜地少測一項）
#define TRIGGER_MENU(w, text)                                                        \
    do {                                                                             \
        QAction *a__ = findMenuAction((w), QStringLiteral(text));                     \
        QVERIFY2(a__, "找不到選單項：" text);                                          \
        a__->trigger();                                                               \
    } while (false)

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

// 以 QsciMacro 直接錄一段可播放的巨集字串（供「已儲存巨集」相關測試種資料用）。
// 不透過 MainWindow，因為那裡的 m_savedMacro 是 private。
static QString recordMacroString(EditorWidget *e, unsigned int msg, const char *arg)
{
    QsciMacro m(e);
    m.startRecording();
    if (arg)
        e->SendScintilla(msg, arg);
    else
        e->SendScintilla(msg);
    m.endRecording();
    return m.save();
}

// 設定目錄下的檔案路徑（測試模式已由 initTestCase 啟用）
static QString cfg(const QString &name)
{
    return macpad::persistence::AppPaths::filePath(name);
}

static void removeCfg(const QString &name)
{
    QFile::remove(cfg(name));
}

class TestMainWindowActions : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 隔離設定/快取，避免動到使用者真實的 ~/Library/Application Support/macpad++
        QStandardPaths::setTestModeEnabled(true);
        // 這幾支測試會讀寫設定目錄下的檔案，先清乾淨以確保與前次執行無關
        removeCfg(QStringLiteral("macros.json"));
        removeCfg(QStringLiteral("macro_shortcuts.json"));
        removeCfg(QStringLiteral("run_commands.json"));
        removeCfg(QStringLiteral("plugins.json"));
        macpad::persistence::PluginStore::setDisabledIds({});
    }

    void cleanupTestCase()
    {
        removeCfg(QStringLiteral("macros.json"));
        removeCfg(QStringLiteral("macro_shortcuts.json"));
        removeCfg(QStringLiteral("run_commands.json"));
        macpad::persistence::PluginStore::setDisabledIds({});
    }

    // ---------------------------------------------------------------
    // 巨集錄製 / 停止 / 播放
    // ---------------------------------------------------------------

    // 錄製→停止→播放整條路徑：播放後編輯器必須真的出現錄製時打的內容，
    // 只斷言「按鈕 enable 狀態」不足以證明巨集有被保存下來。
    void macroRecordStopPlay()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QString());

        QAction *rec = findMenuAction(w, QStringLiteral("Start Recording"));
        QAction *stop = findMenuAction(w, QStringLiteral("Stop Recording"));
        QAction *play = findMenuAction(w, QStringLiteral("Playback"));
        QVERIFY(rec && stop && play);
        QVERIFY(rec->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(!play->isEnabled());

        rec->trigger();
        // 錄製中：Start 停用、Stop 啟用，狀態列顯示錄製訊息
        QVERIFY(!rec->isEnabled());
        QVERIFY(stop->isEnabled());
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("巨集錄製中…"));

        // 錄製一段會改動內容的操作
        e->SendScintilla(QsciScintilla::SCI_REPLACESEL, "AB");
        QCOMPARE(e->text(), QStringLiteral("AB"));

        // 錄製中再呼叫一次 Start 應為 no-op（不可把 m_recordingMacro 覆寫掉，
        // 否則已錄的內容會憑空消失）。選單項此時是停用的，故直接呼叫 slot 驗證守衛本身。
        QMetaObject::invokeMethod(&w, "startMacroRecording");
        QVERIFY(!rec->isEnabled());

        stop->trigger();
        QVERIFY(rec->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY2(play->isEnabled(), "停止錄製後 Playback 仍為停用，代表巨集沒被保存");

        // 播放：清空後重播，內容必須被重現
        e->setText(QString());
        play->trigger();
        QCOMPARE(e->text(), QStringLiteral("AB"));

        // 再停止一次（沒有在錄）應為 no-op，不可崩潰或改動狀態
        QMetaObject::invokeMethod(&w, "stopMacroRecording");
        QVERIFY(rec->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(play->isEnabled());
    }

    // 尚未錄製任何巨集時，Playback 不得動到文件（早退分支）
    void playMacroWithoutRecordingIsNoOp()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("keep"));
        QMetaObject::invokeMethod(&w, "playMacro");
        QCOMPARE(e->text(), QStringLiteral("keep"));
    }

    // ---------------------------------------------------------------
    // Run a Macro Multiple Times…
    // ---------------------------------------------------------------

    // 沒有已錄製巨集時只提示、不開對話框
    void runMacroMultipleTimesWithoutMacro()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("abc"));
        TRIGGER_MENU(w, "Run a Macro Multiple Times…");
        QCOMPARE(e->text(), QStringLiteral("abc"));
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("尚無已錄製的巨集"));
    }

    // 次數模式：設定 3 次 → 錄製的插入動作必須恰好重演 3 次
    void runMacroMultipleTimesNTimes()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QString());

        findMenuAction(w, QStringLiteral("Start Recording"))->trigger();
        e->SendScintilla(QsciScintilla::SCI_REPLACESEL, "x");
        findMenuAction(w, QStringLiteral("Stop Recording"))->trigger();
        e->setText(QString());

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *spin = dlg->findChild<QSpinBox *>();
            QVERIFY(spin);
            spin->setValue(3);
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Run a Macro Multiple Times…");
        QVERIFY2(fired, "Run a Macro Multiple Times… 沒有開出對話框");
        QCOMPARE(e->text(), QStringLiteral("xxx"));
    }

    // 取消對話框：一次都不能執行
    void runMacroMultipleTimesCancelled()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QString());
        findMenuAction(w, QStringLiteral("Start Recording"))->trigger();
        e->SendScintilla(QsciScintilla::SCI_REPLACESEL, "y");
        findMenuAction(w, QStringLiteral("Stop Recording"))->trigger();
        e->setText(QString());

        bool fired = false;
        driveNextModal([](QDialog *dlg) { dlg->reject(); }, &fired);
        TRIGGER_MENU(w, "Run a Macro Multiple Times…");
        QVERIFY(fired);
        QCOMPARE(e->text(), QString());
    }

    // 「執行到檔案結尾」模式：以「游標右移」巨集驗證迴圈會在抵達結尾時停止，
    // 並回報實際執行次數（這正是防無限迴圈的兩個終止條件所在）。
    void runMacroUntilEndOfFile()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("abcde"));

        // 游標定位必須在開始錄製「之前」——否則定位本身也會被錄進巨集，
        // 每次重播都把游標拉回原點，永遠推進不了（正是那條「沒有推進就停」的保護會擋下）。
        e->setCursorPosition(0, 0);
        findMenuAction(w, QStringLiteral("Start Recording"))->trigger();
        e->SendScintilla(QsciScintilla::SCI_CHARRIGHT);
        findMenuAction(w, QStringLiteral("Stop Recording"))->trigger();

        e->setCursorPosition(0, 0);
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            const auto radios = dlg->findChildren<QRadioButton *>();
            QVERIFY(radios.size() >= 2);
            radios.at(1)->setChecked(true);   // 「Run until the end of file」
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Run a Macro Multiple Times…");
        QVERIFY(fired);

        // 內容不變（只是移動游標），游標停在文件結尾，且有回報執行次數
        QCOMPARE(e->text(), QStringLiteral("abcde"));
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)), 5);
        QVERIFY2(w.statusBar()->currentMessage().startsWith(QStringLiteral("Macro ran")),
                 "未回報「執行到檔尾」的實際次數");
    }

    // 「執行到檔案結尾」的無限迴圈保護：巨集不會推進游標時必須主動停止。
    // 用「游標移到行首」的巨集——它永遠到不了檔尾，只有「這一輪前後位置與長度都沒變」
    // 這條終止條件能救場；少了它，這個測試會直接把 UI 卡死。
    void runMacroUntilEofStopsWhenMacroMakesNoProgress()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("abcde"));
        e->setCursorPosition(0, 2);

        findMenuAction(w, QStringLiteral("Start Recording"))->trigger();
        e->SendScintilla(QsciScintilla::SCI_HOME);   // 游標回到行首——永遠不會抵達檔尾
        findMenuAction(w, QStringLiteral("Stop Recording"))->trigger();

        e->setCursorPosition(0, 2);
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            const auto radios = dlg->findChildren<QRadioButton *>();
            QVERIFY(radios.size() >= 2);
            radios.at(1)->setChecked(true);
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Run a Macro Multiple Times…");
        QVERIFY(fired);

        QCOMPARE(e->text(), QStringLiteral("abcde"));
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)), 0);
        // 只跑了兩輪就判定「不會再推進」而停止（而不是硬跑到 10 萬次上限）
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("Macro ran 2 time(s)"));
    }

    // ---------------------------------------------------------------
    // Plugins Admin…
    // ---------------------------------------------------------------

    // 取消不得寫入；確認後停用狀態要持久化（Notepad++ 語意：重啟後生效，故只驗證持久化）
    void pluginsAdminPersistsDisabledIds()
    {
        macpad::persistence::PluginStore::setDisabledIds({});
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        // (1) 取消 → 不寫入
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *list = dlg->findChild<QListWidget *>();
            QVERIFY(list);
            QCOMPARE(list->count(), 2);            // 兩個內建擴充
            list->item(0)->setCheckState(Qt::Unchecked);
            dlg->reject();
        }, &fired);
        TRIGGER_MENU(w, "Plugins Admin…");
        QVERIFY(fired);
        QVERIFY2(macpad::persistence::PluginStore::disabledIds().isEmpty(),
                 "取消 Plugins Admin 後仍寫入了停用清單");

        // (2) 取消勾選 Word Count 並確認 → 持久化為停用
        fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *list = dlg->findChild<QListWidget *>();
            QVERIFY(list);
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->data(Qt::UserRole).toString()
                    == QLatin1String("builtin.wordcount"))
                    list->item(i)->setCheckState(Qt::Unchecked);
            }
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Plugins Admin…");
        QVERIFY(fired);
        QVERIFY(macpad::persistence::PluginStore::disabledIds()
                    .contains(QStringLiteral("builtin.wordcount")));
        QVERIFY2(w.statusBar()->currentMessage().contains(QStringLiteral("restart")),
                 "停用擴充後未提示需重新啟動");

        // (3) 重新開啟時，已停用項目必須以「未勾選」呈現，否則使用者無從復原
        fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *list = dlg->findChild<QListWidget *>();
            QVERIFY(list);
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->data(Qt::UserRole).toString()
                    == QLatin1String("builtin.wordcount")) {
                    QCOMPARE(list->item(i)->checkState(), Qt::Unchecked);
                    list->item(i)->setCheckState(Qt::Checked);   // 還原
                }
            }
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Plugins Admin…");
        QVERIFY(fired);
        QVERIFY(macpad::persistence::PluginStore::disabledIds().isEmpty());
    }

    // ---------------------------------------------------------------
    // Macro Manager…（含 macro_shortcuts.json 重建）
    // ---------------------------------------------------------------

    // 具名巨集的全域快捷鍵：啟動時依 macros.json + macro_shortcuts.json 重建，
    // 觸發後應在目前編輯器重播該巨集。
    void namedMacroShortcutIsRebuiltAndPlays()
    {
        EditorWidget scratch;
        const QString macroStr = recordMacroString(&scratch, QsciScintilla::SCI_REPLACESEL, "Z");
        QVERIFY2(!macroStr.isEmpty(), "無法錄出可用的巨集字串");

        QJsonObject macros;
        macros.insert(QStringLiteral("m1"), macroStr);
        QVERIFY(macpad::persistence::JsonFile::save(cfg(QStringLiteral("macros.json")), macros));
        QJsonObject sc;
        sc.insert(QStringLiteral("m1"), QStringLiteral("Ctrl+Alt+Shift+F9"));
        sc.insert(QStringLiteral("m1"), QStringLiteral("Ctrl+Alt+Shift+F9"));
        // 沒有對應巨集內容、或快捷鍵為空字串的項目必須被略過（否則會註冊出無效快捷鍵）
        sc.insert(QStringLiteral("ghost"), QStringLiteral("Ctrl+Alt+Shift+F10"));
        sc.insert(QStringLiteral("m1_nokey"), QString());
        QVERIFY(macpad::persistence::JsonFile::save(cfg(QStringLiteral("macro_shortcuts.json")), sc));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QString());

        QShortcut *target = nullptr;
        const auto shortcuts = w.findChildren<QShortcut *>();
        for (QShortcut *s : shortcuts) {
            if (s->key() == QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F9")))
                target = s;
            QVERIFY2(s->key() != QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F10")),
                     "為不存在的巨集註冊了快捷鍵");
        }
        QVERIFY2(target, "macro_shortcuts.json 中的快捷鍵沒有被註冊");

        QMetaObject::invokeMethod(target, "activated");
        QCOMPARE(e->text(), QStringLiteral("Z"));
    }

    // Macro Manager 對話框回報的三種變更（改快捷鍵 / 改名 / 刪除）都必須落到 json 檔。
    // 直接由對話框發出 signal，比模擬點擊表格更穩定，且測的正是 MainWindow 端的處理邏輯。
    void macroManagerAppliesShortcutRenameAndDelete()
    {
        EditorWidget scratch;
        const QString macroStr = recordMacroString(&scratch, QsciScintilla::SCI_REPLACESEL, "Q");
        QJsonObject macros;
        macros.insert(QStringLiteral("mm"), macroStr);
        QVERIFY(macpad::persistence::JsonFile::save(cfg(QStringLiteral("macros.json")), macros));
        QJsonObject sc;
        sc.insert(QStringLiteral("mm"), QStringLiteral("Ctrl+Alt+Shift+F8"));
        QVERIFY(macpad::persistence::JsonFile::save(cfg(QStringLiteral("macro_shortcuts.json")), sc));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *mgr = qobject_cast<macpad::ui::MacroManagerDialog *>(dlg);
            QVERIFY(mgr);
            // 清除快捷鍵（空 QKeySequence）→ 重新指定 → 改名（鍵值需跟著搬）
            emit mgr->macroShortcutChanged(QStringLiteral("mm"), QKeySequence());
            emit mgr->macroShortcutChanged(QStringLiteral("mm"),
                                           QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F7")));
            emit mgr->macroRenamed(QStringLiteral("mm"), QStringLiteral("renamed"));
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Macro Manager…");
        QVERIFY2(fired, "Macro Manager… 沒有開出對話框");

        QJsonObject m = macpad::persistence::JsonFile::load(cfg(QStringLiteral("macros.json")));
        QJsonObject s = macpad::persistence::JsonFile::load(cfg(QStringLiteral("macro_shortcuts.json")));
        QVERIFY2(!m.contains(QStringLiteral("mm")), "改名後舊名稱仍留在 macros.json");
        QVERIFY(m.contains(QStringLiteral("renamed")));
        QCOMPARE(s.value(QStringLiteral("renamed")).toString(),
                 QStringLiteral("Ctrl+Alt+Shift+F7"));

        // 刪除：巨集與其快捷鍵都要一併消失（不能只刪巨集而留下孤兒快捷鍵）
        fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *mgr = qobject_cast<macpad::ui::MacroManagerDialog *>(dlg);
            QVERIFY(mgr);
            emit mgr->macroDeleted(QStringLiteral("renamed"));
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Macro Manager…");
        QVERIFY(fired);

        m = macpad::persistence::JsonFile::load(cfg(QStringLiteral("macros.json")));
        s = macpad::persistence::JsonFile::load(cfg(QStringLiteral("macro_shortcuts.json")));
        QVERIFY(!m.contains(QStringLiteral("renamed")));
        QVERIFY(!s.contains(QStringLiteral("renamed")));

        removeCfg(QStringLiteral("macros.json"));
        removeCfg(QStringLiteral("macro_shortcuts.json"));
    }
    // 註：macros.json 為空時 openMacroManager() 會彈 QMessageBox::information，
    //     依測試規範不觸發系統級 modal，故該分支不測。

    // ---------------------------------------------------------------
    // 已儲存 Run 指令的全域快捷鍵 → runSavedCommand()
    // ---------------------------------------------------------------

    // 快捷鍵重建 + 執行：未命名文件與已存檔文件兩條變數展開路徑都要走到。
    void savedRunCommandShortcutRunsInDock()
    {
        QList<macpad::features::RunCommandEntry> entries;
        macpad::features::RunCommandEntry withKey;
        withKey.name = QStringLiteral("echo");
        withKey.command = QStringLiteral("echo hi");
        withKey.shortcut = QStringLiteral("Ctrl+Alt+Shift+F6");
        macpad::features::RunCommandEntry noKey;
        noKey.name = QStringLiteral("nokey");
        noKey.command = QStringLiteral("echo nope");
        entries << withKey << noKey;   // 無快捷鍵者不得被註冊
        QVERIFY(macpad::features::RunCommandStore::save(entries));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("run_target.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("alpha beta\n");
        }

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);

        QShortcut *runSc = nullptr;
        const auto shortcuts = w.findChildren<QShortcut *>();
        for (QShortcut *s : shortcuts) {
            if (s->key() == QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F6")))
                runSc = s;
        }
        QVERIFY2(runSc, "run_commands.json 中的快捷鍵沒有被註冊");

        // (1) 目前是未命名文件 → 變數留空的路徑
        QVERIFY(!w.findChild<macpad::features::RunDock *>());
        QMetaObject::invokeMethod(runSc, "activated");
        auto *dock = w.findChild<macpad::features::RunDock *>();
        QVERIFY2(dock, "執行已儲存指令後未建立 Run 面板");
        QVERIFY(dock->isVisibleTo(&w));

        // (2) 已存檔且有選取 → 以選取文字作為 $(CURRENT_WORD)
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QVERIFY(!e->isUntitled());
        e->setSelection(0, 0, 0, 5);
        QVERIFY(e->hasSelectedText());
        QMetaObject::invokeMethod(runSc, "activated");

        // (3) 已存檔但無選取 → 取游標所在單字
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 2);
        QMetaObject::invokeMethod(runSc, "activated");

        // 面板重用（不應每次都重建一個）
        QCOMPARE(w.findChildren<macpad::features::RunDock *>().size(), 1);
        QTest::qWait(50);   // 讓 echo 子行程收尾，避免視窗銷毀時仍在執行
        removeCfg(QStringLiteral("run_commands.json"));
    }

    // ---------------------------------------------------------------
    // Find / Replace / Go to…
    // ---------------------------------------------------------------

    // Find/Replace 對話框為非 modal（show()），可安全建立；兩次呼叫必須重用同一個實例
    void showFindAndReplaceReuseOneDialog()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("needle in haystack"));

        QMetaObject::invokeMethod(&w, "showFind");
        auto dialogs = w.findChildren<QDialog *>();
        int count = 0;
        for (QDialog *d : dialogs)
            if (d->isVisible())
                ++count;
        QVERIFY2(count >= 1, "showFind() 未顯示尋找對話框");

        QMetaObject::invokeMethod(&w, "showReplace");
        QCOMPARE(w.findChildren<QDialog *>().size(), dialogs.size());  // 沒有多開一個

        // 反向順序：先開 Replace 也必須能自行建立對話框（兩個入口各自 lazy-init）
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        const int before = w2.findChildren<QDialog *>().size();
        QMetaObject::invokeMethod(&w2, "showReplace");
        QVERIFY(w2.findChildren<QDialog *>().size() > before);
    }

    // Go to…：行模式與字元位移模式各自定位到正確位置，取消則不動游標
    void gotoLineAndOffset()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("l0\nl1\nl2\nl3\nl4"));
        e->setCursorPosition(0, 0);

        // (1) 行模式 → 第 4 行（1-based）＝ index 3
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *spin = dlg->findChild<QSpinBox *>();
            QVERIFY(spin);
            spin->setValue(4);
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Go to Line…");
        QVERIFY2(fired, "Go to Line… 沒有開出對話框");
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 3);

        // (2) 字元位移模式 → 位置 7（"l0\nl1\nl2" 的第 8 個位元組）
        e->setCursorPosition(0, 0);
        fired = false;
        driveNextModal([](QDialog *dlg) {
            const auto radios = dlg->findChildren<QRadioButton *>();
            QVERIFY(radios.size() >= 2);
            radios.at(1)->setChecked(true);        // Character offset
            auto *spin = dlg->findChild<QSpinBox *>();
            QVERIFY(spin);
            // 切到位移模式後上限應放大到全文長度（否則 setValue 會被夾住）
            QCOMPARE(spin->maximum(), qMax(1, 14));
            spin->setValue(7);
            dlg->accept();
        }, &fired);
        TRIGGER_MENU(w, "Go to Line…");
        QVERIFY(fired);
        QCOMPARE(int(e->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)), 7);

        // (3) 取消 → 游標不得移動
        e->setCursorPosition(1, 1);
        fired = false;
        driveNextModal([](QDialog *dlg) { dlg->reject(); }, &fired);
        TRIGGER_MENU(w, "Go to Line…");
        QVERIFY(fired);
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 1);
        QCOMPARE(col, 1);
    }

    // ---------------------------------------------------------------
    // 搜尋命令：Find Next/Previous、Select and Find、Mark、Clear Marks
    // ---------------------------------------------------------------

    void findNextPreviousAndSelectAndFind()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("foo bar foo bar foo"));

        // 沒有選取也沒有前次查詢 → 什麼都不做
        e->setCursorPosition(0, 0);
        QMetaObject::invokeMethod(&w, "findNextDir", Q_ARG(bool, true));
        QVERIFY2(!e->hasSelectedText(), "無查詢字串時不應選取任何內容");

        // 以選取內容作為查詢：找下一個 "foo"（位置 8）
        e->setSelection(0, 0, 0, 3);            // 選到第一個 "foo"
        QMetaObject::invokeMethod(&w, "selectAndFind", Q_ARG(bool, true));
        int lf = 0, if_ = 0, lt = 0, it = 0;
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 8);
        QCOMPARE(e->selectedText(), QStringLiteral("foo"));

        // 記住了查詢字串 → 之後不必再選取也能繼續找
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 9);
        QMetaObject::invokeMethod(&w, "findNextDir", Q_ARG(bool, true));
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 16);
        QCOMPARE(e->selectedText(), QStringLiteral("foo"));

        // 反向搜尋：先把游標移到第三筆之前（否則反向搜尋會再次命中目前這一筆）
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 12);
        QMetaObject::invokeMethod(&w, "findNextDir", Q_ARG(bool, false));
        e->getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 8);

        // Select and Find 在沒有選取時不應改動選取狀態
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        QMetaObject::invokeMethod(&w, "selectAndFind", Q_ARG(bool, true));
        QVERIFY(!e->hasSelectedText());
    }

    void markAndClearOccurrences()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("aa bb aa bb aa"));

        // 無選取 → 不標記、不顯示訊息
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "markSelectionOccurrences");
        QVERIFY(w.statusBar()->currentMessage().isEmpty());

        // 選取 "aa" → 三處被標記，狀態列回報數量
        e->setSelection(0, 0, 0, 2);
        QMetaObject::invokeMethod(&w, "markSelectionOccurrences");
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("已標記 3 處"));

        // 清除標記不得改動文件內容
        const QString before = e->text();
        QMetaObject::invokeMethod(&w, "clearAllMarks");
        QCOMPARE(e->text(), before);
    }

    // ---------------------------------------------------------------
    // 文字操作（applyTextOp）——大小寫 / 排序 / 去重 / 空白 / 註解
    // ---------------------------------------------------------------

    // 無選取時作用於全文；有選取時只作用於選取範圍——兩條分支都必須驗證
    void convertCaseWholeDocumentAndSelection()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        e->setText(QStringLiteral("hello world"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "UPPERCASE");
        QCOMPARE(e->text(), QStringLiteral("HELLO WORLD"));

        TRIGGER_MENU(w, "lowercase");
        QCOMPARE(e->text(), QStringLiteral("hello world"));

        // 只轉換選取的前 5 個字元
        e->setSelection(0, 0, 0, 5);
        TRIGGER_MENU(w, "UPPERCASE");
        QCOMPARE(e->text(), QStringLiteral("HELLO world"));

        e->setText(QStringLiteral("hello world"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "Title Case");
        QCOMPARE(e->text(), QStringLiteral("Hello World"));

        e->setText(QStringLiteral("aBc"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "iNVERT cASE");
        QCOMPARE(e->text(), QStringLiteral("AbC"));

        // 唯讀文件不得被任何文字操作改動
        e->setText(QStringLiteral("locked"));
        e->setReadOnly(true);
        TRIGGER_MENU(w, "UPPERCASE");
        QCOMPARE(e->text(), QStringLiteral("locked"));
        e->setReadOnly(false);
    }

    void lineOperationsSortDedupReverse()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        e->setText(QStringLiteral("c\na\nb"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "Sort Ascending");
        QCOMPARE(e->text(), QStringLiteral("a\nb\nc"));

        TRIGGER_MENU(w, "Sort Descending");
        QCOMPARE(e->text(), QStringLiteral("c\nb\na"));

        TRIGGER_MENU(w, "Reverse Line Order");
        QCOMPARE(e->text(), QStringLiteral("a\nb\nc"));

        e->setText(QStringLiteral("a\nb\na\nb"));
        TRIGGER_MENU(w, "Remove Duplicate Lines");
        QCOMPARE(e->text(), QStringLiteral("a\nb"));

        e->setText(QStringLiteral("a\na\nb"));
        TRIGGER_MENU(w, "Remove Consecutive Duplicate Lines");
        QCOMPARE(e->text(), QStringLiteral("a\nb"));

        e->setText(QStringLiteral("a\n\nb"));
        TRIGGER_MENU(w, "Remove Empty Lines");
        QCOMPARE(e->text(), QStringLiteral("a\nb"));

        e->setText(QStringLiteral("a\nb"));
        TRIGGER_MENU(w, "Duplicate Lines");
        QCOMPARE(e->text(), QStringLiteral("a\na\nb\nb"));

        e->setText(QStringLiteral("10\n9\n2"));
        TRIGGER_MENU(w, "Sort Numeric");
        QCOMPARE(e->text(), QStringLiteral("2\n9\n10"));

        e->setText(QStringLiteral("bbb\na\ncc"));
        TRIGGER_MENU(w, "Sort Lines by Length (Ascending)");
        QCOMPARE(e->text(), QStringLiteral("a\ncc\nbbb"));
    }

    void blankOperationsAndComment()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        e->setText(QStringLiteral("a   \nb\t\n"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "Trim Trailing Whitespace");
        QCOMPARE(e->text(), QStringLiteral("a\nb\n"));

        e->setText(QStringLiteral("   a\n\tb"));
        TRIGGER_MENU(w, "Trim Leading Whitespace");
        QCOMPARE(e->text(), QStringLiteral("a\nb"));

        e->setText(QStringLiteral("\ta"));
        TRIGGER_MENU(w, "TAB to Space");
        QCOMPARE(e->text(), QStringLiteral("    a"));

        e->setText(QStringLiteral("a\nb"));
        TRIGGER_MENU(w, "EOL to Space");
        QCOMPARE(e->text(), QStringLiteral("a b"));

        // 未命名文件的行註解標記固定為 "//"（標記後帶一個空白）；再切一次應完全還原
        e->setText(QStringLiteral("int a;\nint b;"));
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        TRIGGER_MENU(w, "Toggle Line Comment");
        QCOMPARE(e->text(), QStringLiteral("// int a;\n// int b;"));
        TRIGGER_MENU(w, "Toggle Line Comment");
        QCOMPARE(e->text(), QStringLiteral("int a;\nint b;"));
    }

    // ---------------------------------------------------------------
    // 行搬移（moveCurrentLines）
    // ---------------------------------------------------------------

    void moveLinesUpAndDown()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // 無選取：以游標所在行為單位上移
        e->setText(QStringLiteral("a\nb\nc"));
        e->setCursorPosition(1, 0);
        TRIGGER_MENU(w, "Move Lines Up");
        QCOMPARE(e->text(), QStringLiteral("b\na\nc"));
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, 0);   // 游標跟著搬到新位置

        // 下移回原位
        TRIGGER_MENU(w, "Move Lines Down");
        QCOMPARE(e->text(), QStringLiteral("a\nb\nc"));

        // 已在第一行再上移 → 內容不變（早退分支）
        e->setCursorPosition(0, 0);
        TRIGGER_MENU(w, "Move Lines Up");
        QCOMPARE(e->text(), QStringLiteral("a\nb\nc"));

        // 有選取：整段一起搬
        e->setText(QStringLiteral("a\nb\nc\nd"));
        e->setSelection(1, 0, 2, 1);   // 選到 b、c 兩行
        TRIGGER_MENU(w, "Move Lines Down");
        QCOMPARE(e->text(), QStringLiteral("a\nd\nb\nc"));

        // 唯讀時不得搬動
        e->setText(QStringLiteral("a\nb"));
        e->setCursorPosition(1, 0);
        e->setReadOnly(true);
        TRIGGER_MENU(w, "Move Lines Up");
        QCOMPARE(e->text(), QStringLiteral("a\nb"));
        e->setReadOnly(false);
    }

    // ---------------------------------------------------------------
    // 增量搜尋工具列與「第 n / 共 m 筆」計數
    // ---------------------------------------------------------------

    void incrementalSearchBarAndCount()
    {
        auto settings = macpad::persistence::SettingsStore::load();
        const bool savedFlag = settings.incrementalSearchCount;
        settings.incrementalSearchCount = true;
        QVERIFY(macpad::persistence::SettingsStore::save(settings));

        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("alpha beta alpha gamma"));
        e->setCursorPosition(0, 0);

        // 工具列還沒建立時就更新計數必須安全早退（工具列是 lazy-init 的）
        QMetaObject::invokeMethod(&w, "updateIncrementalSearchCount",
                                  Q_ARG(QString, QStringLiteral("alpha")));

        QMetaObject::invokeMethod(&w, "showIncrementalSearch");

        QToolBar *bar = nullptr;
        const auto bars = w.findChildren<QToolBar *>();
        for (QToolBar *tb : bars)
            if (tb->windowTitle() == QStringLiteral("Incremental Search"))
                bar = tb;
        QVERIFY2(bar, "沒有建立增量搜尋工具列");
        QVERIFY(bar->isVisibleTo(&w));

        auto *edit = bar->findChild<QLineEdit *>();
        QVERIFY(edit);
        const auto labels = bar->findChildren<QLabel *>();
        QVERIFY(labels.size() >= 2);
        QLabel *countLabel = labels.last();

        // 邊打邊找：第一筆命中，計數顯示「第 1 / 共 2 筆」
        edit->setText(QStringLiteral("alpha"));
        QCOMPARE(e->selectedText(), QStringLiteral("alpha"));
        QVERIFY2(countLabel->text().contains(QStringLiteral("共 2 筆")),
                 qPrintable(QStringLiteral("計數標籤內容非預期：%1").arg(countLabel->text())));
        QVERIFY(countLabel->text().contains(QStringLiteral("第 1")));

        // Enter → 找下一個，計數改為第 2 筆
        QMetaObject::invokeMethod(edit, "returnPressed");
        QVERIFY(countLabel->text().contains(QStringLiteral("第 2")));

        // 查無結果
        QMetaObject::invokeMethod(&w, "updateIncrementalSearchCount",
                                  Q_ARG(QString, QStringLiteral("zzzz")));
        QCOMPARE(countLabel->text().trimmed(), QStringLiteral("找不到"));

        // 空字串 → 清空標籤
        QMetaObject::invokeMethod(&w, "updateIncrementalSearchCount",
                                  Q_ARG(QString, QString()));
        QVERIFY(countLabel->text().isEmpty());

        // 命中位置與游標不一致時，只顯示總數（不顯示「第 n」）
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        e->setCursorPosition(0, 3);
        QMetaObject::invokeMethod(&w, "updateIncrementalSearchCount",
                                  Q_ARG(QString, QStringLiteral("alpha")));
        QCOMPARE(countLabel->text().trimmed(), QStringLiteral("共 2 筆"));

        // 清空輸入框 → lambda 內的早退路徑同樣要清掉標籤
        edit->setText(QString());
        QVERIFY(countLabel->text().isEmpty());

        // 偏好關閉時不做全文掃描，直接清空
        settings.incrementalSearchCount = false;
        QVERIFY(macpad::persistence::SettingsStore::save(settings));
        QMetaObject::invokeMethod(&w, "updateIncrementalSearchCount",
                                  Q_ARG(QString, QStringLiteral("alpha")));
        QVERIFY(countLabel->text().isEmpty());

        // 關閉鈕（✕）收起工具列
        QAction *closeAct = nullptr;
        const auto acts = bar->actions();
        for (QAction *a : acts)
            if (a->text() == QStringLiteral("✕"))
                closeAct = a;
        QVERIFY(closeAct);
        closeAct->trigger();
        QVERIFY(!bar->isVisibleTo(&w));

        // 再次呼叫必須重用同一個工具列，而不是疊出第二條
        QMetaObject::invokeMethod(&w, "showIncrementalSearch");
        QVERIFY(bar->isVisibleTo(&w));
        int barCount = 0;
        const auto bars2 = w.findChildren<QToolBar *>();
        for (QToolBar *tb : bars2)
            if (tb->windowTitle() == QStringLiteral("Incremental Search"))
                ++barCount;
        QCOMPARE(barCount, 1);

        settings.incrementalSearchCount = savedFlag;
        QVERIFY(macpad::persistence::SettingsStore::save(settings));
    }

    // ---------------------------------------------------------------
    // 其他視窗/檢視命令
    // ---------------------------------------------------------------

    // 未存檔文件不可能在瀏覽器中開啟 —— 只提示，不呼叫平台整合。
    // （已存檔的分支會實際啟動外部程式，測試環境不觸發。）
    void viewInBrowserRequiresSavedFile()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QVERIFY(e->isUntitled());
        w.statusBar()->clearMessage();
        QMetaObject::invokeMethod(&w, "viewCurrentFileInBrowser",
                                  Q_ARG(QString, QString()));
        QCOMPARE(w.statusBar()->currentMessage(),
                 QStringLiteral("請先存檔再於瀏覽器開啟"));
    }

    void alwaysOnTopTogglesWindowFlag()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QVERIFY(!w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QMetaObject::invokeMethod(&w, "toggleAlwaysOnTop", Q_ARG(bool, true));
        QVERIFY(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QMetaObject::invokeMethod(&w, "toggleAlwaysOnTop", Q_ARG(bool, false));
        QVERIFY(!w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    }

    // 註：以下同檔案內的動作刻意不測，因為在 offscreen 測試環境中會造成阻塞或副作用：
    //   - printCurrentDocument()：開啟 QPrintDialog（系統列印對話框）。
    //   - quickPrintCurrentDocument() / preparePrinter() / sendToPrinter()：
    //     會真的把文件送到系統預設印表機。
    //   - showFindInProjects()：Project Panel 無檔案時彈 QMessageBox，
    //     有檔案時走 QInputDialog + 背景搜尋執行緒。
    //   - openMacroManager() 的「尚無巨集」分支：QMessageBox::information。
};

QTEST_MAIN(TestMainWindowActions)
#include "test_mainwindow_actions.moc"
