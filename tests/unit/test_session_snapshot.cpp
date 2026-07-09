// 單元測試：Notepad++ session 快照——未存內容跨重啟保留（含 untitled 緩衝與 dirty 已命名檔）。
// 以真實 MainWindow（offscreen + QStandardPaths 測試模式）驗證 buildCurrentSession → openSessionState
// 端到端還原：untitled 未存內容重開後仍在、標題維持 untitled、且保持 dirty。
#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabWidget>

#include <QTimer>
#include <QListWidget>
#include <QApplication>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "persistence/SessionStore.h"
#include "features/backup/BackupService.h"
#include "ui/SnapshotRecoveryDialog.h"

using macpad::core::EditorWidget;
using macpad::persistence::SessionState;

class TestSessionSnapshot : public QObject {
    Q_OBJECT
private:
    // 在某視窗的主檢視中，尋找第一個符合述詞的編輯器
    template <typename Pred>
    static EditorWidget *findEditor(MainWindow &w, Pred pred)
    {
        QTabWidget *tabs = w.m_tabs;
        for (int i = 0; i < tabs->count(); ++i) {
            if (EditorWidget *e = w.editorIn(tabs, i); e && pred(e))
                return e;
        }
        return nullptr;
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // 核心情境：開新檔（untitled）打字未存 → 建 session → 於新視窗還原 → 內容還在、untitled、dirty
    void untitledUnsavedSurvivesRestart()
    {
        const QString draft = QStringLiteral("開新檔還沒存的內容\nsecond line");

        SessionState st;
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            EditorWidget *ed = w.currentEditor();
            QVERIFY(ed);
            QVERIFY(ed->isUntitled());
            ed->setText(draft);              // 未命名 + 有內容 → dirty
            QVERIFY(ed->isDirty());

            st = w.buildCurrentSession();
            // 應捕捉到這個 untitled 未存緩衝
            bool captured = false;
            for (const auto &t : st.tabs)
                if (t.untitled && t.dirty && t.unsavedContent == draft)
                    captured = true;
            QVERIFY2(captured, "buildCurrentSession 未捕捉 untitled 未存內容");
        }

        // 模擬「重開」：新視窗還原 session
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        w2.openSessionState(st);
        EditorWidget *restored = findEditor(w2, [&](EditorWidget *e) {
            return e->isUntitled() && e->text() == draft;
        });
        QVERIFY2(restored, "未存的 untitled 內容重開後遺失");
        QVERIFY(restored->isDirty());        // 維持未存標記
        QVERIFY(restored->isUntitled());     // 標題仍為 untitled（無檔路徑）
    }

    // 未命名分頁以 untitled(1)/(2)/(3) 編號區分；關閉後號碼回收
    void untitledTabsAreNumbered()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e0 = w.currentEditor();
        EditorWidget *e1 = w.addEditorTab();
        EditorWidget *e2 = w.addEditorTab();
        QCOMPARE(e0->untitledNumber(), 1);
        QCOMPARE(e1->untitledNumber(), 2);
        QCOMPARE(e2->untitledNumber(), 3);
        // displayName 各自不同（複刻 Notepad++ new N）
        QVERIFY(e0->displayName().contains(QStringLiteral("untitled(1)")));
        QVERIFY(e1->displayName().contains(QStringLiteral("untitled(2)")));
        QVERIFY(e2->displayName().contains(QStringLiteral("untitled(3)")));
        QVERIFY(e0->displayName() != e1->displayName());

        // 關閉 untitled(2) → 下一個新分頁回收 2 號（最小未用）
        w.closeTabIn(w.m_tabs, w.m_tabs->indexOf(w.m_tabs->widget(1)));
        EditorWidget *e3 = w.addEditorTab();
        QCOMPARE(e3->untitledNumber(), 2);
    }

    // 多個 untitled 分頁各自不同內容 → 走完整 saveSession/restoreSession（JSON）後仍各自獨立
    void multipleUntitledDistinctAfterRestart()
    {
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            EditorWidget *e0 = w.currentEditor();
            QVERIFY(e0 && e0->isUntitled());
            e0->setText(QStringLiteral("AAA content"));
            EditorWidget *e1 = w.addEditorTab();
            e1->setText(QStringLiteral("BBB content"));
            EditorWidget *e2 = w.addEditorTab();
            e2->setText(QStringLiteral("CCC content"));
            w.saveSession();   // 走 SessionStore::save（JSON）
        }

        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        w2.restoreSession();   // 走 SessionStore::load（JSON）

        QStringList untitledTexts;
        QTabWidget *tabs = w2.m_tabs;
        for (int i = 0; i < tabs->count(); ++i) {
            if (EditorWidget *e = w2.editorIn(tabs, i); e && e->isUntitled()) {
                const QString t = e->text();
                if (!t.isEmpty())
                    untitledTexts << t;
            }
        }
        QVERIFY2(untitledTexts.contains(QStringLiteral("AAA content")), "AAA 遺失");
        QVERIFY2(untitledTexts.contains(QStringLiteral("BBB content")), "BBB 遺失");
        QVERIFY2(untitledTexts.contains(QStringLiteral("CCC content")), "CCC 遺失");
        // 三份內容必須各自獨立，不能被去重成同一份
        QCOMPARE(untitledTexts.size(), 3);
    }

    // 走「真實建構子啟動路徑」（restoreSessionOnLaunch=true，含 crash 快照還原區塊）重現多 untitled
    void realStartupMultipleUntitled()
    {
        macpad::features::BackupService::clearSnapshots();
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.currentEditor()->setText(QStringLiteral("AAA real"));
            w.addEditorTab()->setText(QStringLiteral("BBB real"));
            w.addEditorTab()->setText(QStringLiteral("CCC real"));
            w.close();   // 觸發真正的 closeEvent（saveSession + clearSnapshots），最忠實重現
        }

        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/true);   // 真實啟動路徑
        QStringList texts;
        QTabWidget *tabs = w2.m_tabs;
        for (int i = 0; i < tabs->count(); ++i)
            if (EditorWidget *e = w2.editorIn(tabs, i); e && e->isUntitled() && !e->text().isEmpty())
                texts << e->text();
        QVERIFY2(texts.contains(QStringLiteral("AAA real")), qPrintable(texts.join('|')));
        QVERIFY2(texts.contains(QStringLiteral("BBB real")), qPrintable(texts.join('|')));
        QVERIFY2(texts.contains(QStringLiteral("CCC real")), qPrintable(texts.join('|')));
        QCOMPARE(texts.size(), 3);
    }

    // dirty 已命名檔：還原後顯示未存的編輯內容（覆蓋磁碟版），且維持 dirty
    void dirtyNamedFileRestoresUnsavedEdit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("edited.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("DISK VERSION");
            f.close();
        }

        SessionState st;
        {
            MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
            w.openFile(path);
            EditorWidget *ed = findEditor(w, [&](EditorWidget *e) { return !e->isUntitled(); });
            QVERIFY(ed);
            ed->setText(QStringLiteral("EDITED IN MEMORY"));   // 產生未存變更
            QVERIFY(ed->isDirty());
            st = w.buildCurrentSession();
        }

        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/false);
        w2.openSessionState(st);
        EditorWidget *restored = findEditor(w2, [&](EditorWidget *e) {
            return !e->isUntitled() && e->filePath() == QFileInfo(path).absoluteFilePath();
        });
        QVERIFY2(restored, "已命名檔未還原");
        QCOMPARE(restored->text(), QStringLiteral("EDITED IN MEMORY"));  // 未存編輯而非磁碟版
        QVERIFY(restored->isDirty());
    }

    // closeEvent：啟用快照時 untitled dirty 分頁關閉「不跳存檔提示」（若跳 modal 本測試會卡住）
    void closeEventSilentForUntitledWhenSnapshotEnabled()
    {
        // enableSessionSnapshot 預設為 true
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        w.currentEditor()->setText(QStringLiteral("dirty untitled draft"));
        QVERIFY(w.currentEditor()->isDirty());
        w.show();
        const bool closed = w.close();   // untitled → 略過 maybeSave，不出現 modal
        QVERIFY(closed);
        QVERIFY(!w.isVisible());
    }

    // crash 快照與 session 還原內容相同時，不重複開啟同一份草稿（去重）
    void crashSnapshotDedupWithSession()
    {
        macpad::features::BackupService::clearSnapshots();
        const QString draft = QStringLiteral("SAME DRAFT CONTENT");
        // session.json：一個 untitled dirty 分頁
        SessionState st;
        macpad::persistence::TabState t;
        t.untitled = true; t.dirty = true; t.unsavedContent = draft;
        st.tabs = { t };
        QVERIFY(macpad::persistence::SessionStore::save(st));
        // crash 快照：同一份內容（模擬 timer 已寫入、之後非正常結束）
        QVERIFY(macpad::features::BackupService::writeSnapshot(
            QStringLiteral("session"), QStringLiteral("untitled-0"), draft.toUtf8()));

        // 於當機復原對話框出現時（exec 阻塞建構子期間）自動選第 0 項並「還原」
        QTimer killer;
        killer.setInterval(5);
        QObject::connect(&killer, &QTimer::timeout, [&killer]() {
            for (QWidget *tw : QApplication::topLevelWidgets()) {
                if (auto *dlg = qobject_cast<macpad::ui::SnapshotRecoveryDialog *>(tw)) {
                    if (auto *list = dlg->findChild<QListWidget *>())
                        list->setCurrentRow(0);
                    dlg->accept();   // Accepted + !discardAll → 走還原分支（含去重）
                    killer.stop();
                    return;
                }
            }
        });
        killer.start();
        MainWindow w2(nullptr, /*restoreSessionOnLaunch=*/true);
        killer.stop();
        macpad::features::BackupService::clearSnapshots();

        // 該草稿內容應只出現一次（session 還原 1 份，crash 快照因去重被略過）
        int occurrences = 0;
        QTabWidget *tabs = w2.m_tabs;
        for (int i = 0; i < tabs->count(); ++i)
            if (EditorWidget *e = w2.editorIn(tabs, i); e && e->text() == draft)
                ++occurrences;
        QCOMPARE(occurrences, 1);
    }

    // 空白未動的 untitled 分頁不應被持久化（避免每次啟動堆積空白分頁）
    void emptyUntitledNotPersisted()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *ed = w.currentEditor();
        QVERIFY(ed && ed->isUntitled() && !ed->isDirty());
        const SessionState st = w.buildCurrentSession();
        QCOMPARE(st.tabs.size(), 0);   // 沒有值得保存的內容
    }
};

QTEST_MAIN(TestSessionSnapshot)
#include "test_session_snapshot.moc"
