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
