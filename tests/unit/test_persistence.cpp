// 單元測試：持久化（Session/Recent/Json）round-trip 與 corruption-safe（FR-016/017）
#include <QtTest>
#include <QStandardPaths>
#include <QFile>

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"
#include "persistence/SessionStore.h"
#include "persistence/RecentFiles.h"

using namespace macpad::persistence;

class TestPersistence : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // 導向暫存目錄，避免污染真實設定
        QStandardPaths::setTestModeEnabled(true);
    }

    void sessionRoundtrip()
    {
        SessionState in;
        in.activeIndex = 1;
        in.tabs = {{QStringLiteral("/tmp/a.txt"), 3, 5, 2},
                   {QStringLiteral("/tmp/b.cpp"), 10, 0, 8}};
        QVERIFY(SessionStore::save(in));

        const SessionState out = SessionStore::load();
        QCOMPARE(out.activeIndex, 1);
        QCOMPARE(out.tabs.size(), 2);
        QCOMPARE(out.tabs[0].path, QStringLiteral("/tmp/a.txt"));
        QCOMPARE(out.tabs[0].line, 3);
        QCOMPARE(out.tabs[1].firstVisibleLine, 8);
    }

    void sessionRoundtripNewFields()
    {
        // FR-052：selection/bookmarks/languageOverride round-trip
        SessionState in;
        in.activeIndex = 0;
        TabState t;
        t.path = QStringLiteral("/tmp/c.cpp");
        t.line = 1;
        t.index = 2;
        t.firstVisibleLine = 0;
        t.selection = QStringLiteral("1,0,3,5");
        t.bookmarks = {2, 7, 9};
        t.languageOverride = QStringLiteral("python");
        t.view = 1;   // FR-062：分頁所屬檢視
        in.tabs = {t};
        QVERIFY(SessionStore::save(in));

        const SessionState out = SessionStore::load();
        QCOMPARE(out.tabs.size(), 1);
        QCOMPARE(out.tabs[0].selection, QStringLiteral("1,0,3,5"));
        QCOMPARE(out.tabs[0].bookmarks, (QList<int>{2, 7, 9}));
        QCOMPARE(out.tabs[0].languageOverride, QStringLiteral("python"));
        QCOMPARE(out.tabs[0].view, 1);   // FR-062 round-trip
    }

    void sessionMissingNewFieldsDefaultEmpty()
    {
        // 舊格式（缺新欄位）載入時應預設為空，back-compat
        SessionState in;
        in.activeIndex = 0;
        in.tabs = {{QStringLiteral("/tmp/old.txt"), 1, 2, 0}};
        QVERIFY(SessionStore::save(in));

        const SessionState out = SessionStore::load();
        QCOMPARE(out.tabs.size(), 1);
        QVERIFY(out.tabs[0].selection.isEmpty());
        QVERIFY(out.tabs[0].bookmarks.isEmpty());
        QVERIFY(out.tabs[0].languageOverride.isEmpty());
    }

    void sessionSnapshotUnsavedRoundtrip()
    {
        // Notepad++ session 快照：untitled 未存緩衝 + dirty 已命名檔的未存內容 round-trip。
        SessionState in;
        in.activeIndex = 1;
        TabState untitled;              // 未命名未存緩衝（無 path）
        untitled.untitled = true;
        untitled.dirty = true;
        untitled.unsavedContent = QStringLiteral("draft not yet saved\nline2");
        TabState named;                 // 已命名但有未存變更
        named.path = QStringLiteral("/tmp/edited.cpp");
        named.dirty = true;
        named.unsavedContent = QStringLiteral("int main() { /* unsaved edit */ }");
        in.tabs = {untitled, named};
        QVERIFY(SessionStore::save(in));

        const SessionState out = SessionStore::load();
        // 關鍵：untitled（空 path）分頁不再被丟棄
        QCOMPARE(out.tabs.size(), 2);
        QVERIFY(out.tabs[0].untitled);
        QVERIFY(out.tabs[0].path.isEmpty());
        QVERIFY(out.tabs[0].dirty);
        QCOMPARE(out.tabs[0].unsavedContent, QStringLiteral("draft not yet saved\nline2"));
        QVERIFY(!out.tabs[1].untitled);
        QCOMPARE(out.tabs[1].path, QStringLiteral("/tmp/edited.cpp"));
        QVERIFY(out.tabs[1].dirty);
        QCOMPARE(out.tabs[1].unsavedContent,
                 QStringLiteral("int main() { /* unsaved edit */ }"));
        QCOMPARE(out.activeIndex, 1);
    }

    void sessionCleanTabHasNoUnsavedPayload()
    {
        // 非 dirty 的已命名檔不應寫出 unsaved/dirty（避免膨脹 session.json）
        SessionState in;
        TabState clean;
        clean.path = QStringLiteral("/tmp/clean.txt");
        clean.dirty = false;
        clean.unsavedContent = QStringLiteral("should not persist");
        in.tabs = {clean};
        QVERIFY(SessionStore::save(in));

        const SessionState out = SessionStore::load();
        QCOMPARE(out.tabs.size(), 1);
        QVERIFY(!out.tabs[0].dirty);
        QVERIFY(out.tabs[0].unsavedContent.isEmpty());
    }

    void corruptSessionIsSafe()
    {
        // 寫入損毀 JSON → load 應回空 session 不崩潰（FR-016 AC3）
        const QString path = AppPaths::filePath(QStringLiteral("session.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not valid json ]]]");
        f.close();

        const SessionState out = SessionStore::load();
        QCOMPARE(out.tabs.size(), 0);
    }

    void recentDedupAndCap()
    {
        RecentFiles::clear();
        for (int i = 0; i < RecentFiles::kMaxItems + 5; ++i)
            RecentFiles::add(QStringLiteral("/tmp/file%1.txt").arg(i));
        // 重複加入既有項應置頂且不增加數量
        RecentFiles::add(QStringLiteral("/tmp/file3.txt"));

        const QStringList list = RecentFiles::load();
        QVERIFY(list.size() <= RecentFiles::kMaxItems);
        QCOMPARE(list.first(), QStringLiteral("/tmp/file3.txt"));  // 置頂
        // 去重：file3 只出現一次
        QCOMPARE(list.count(QStringLiteral("/tmp/file3.txt")), 1);
    }

    void jsonMissingFileReturnsEmpty()
    {
        const QJsonObject o = JsonFile::load(AppPaths::filePath(QStringLiteral("nonexistent.json")));
        QVERIFY(o.isEmpty());
    }

    void namedSessionRoundtrip()
    {
        SessionState in;
        in.activeIndex = 0;
        in.tabs = {{QStringLiteral("/tmp/x.txt"), 1, 2, 0}};
        QVERIFY(SessionStore::saveNamed("My Session", in));
        const SessionState out = SessionStore::loadNamed("My Session");
        QCOMPARE(out.tabs.size(), 1);
        QCOMPARE(out.tabs[0].path, QStringLiteral("/tmp/x.txt"));
        QVERIFY(SessionStore::listNames().contains(QStringLiteral("My Session")));
    }
};

QTEST_APPLESS_MAIN(TestPersistence)
#include "test_persistence.moc"
