// 單元測試：UDL 定義解析/round-trip 與 Manager 查找（FR-032）
#include <QtTest>
#include <QStandardPaths>
#include <QJsonObject>
#include <QJsonArray>

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"

using namespace macpad::features;

class TestUdl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void fromJsonParses()
    {
        QJsonObject o;
        o.insert("name", "MyLang");
        o.insert("extensions", QJsonArray{"ml", "mylang"});
        o.insert("keywords", QJsonArray{"if", "then", "end"});
        o.insert("line_comment", "#");
        o.insert("case_sensitive", false);
        const auto d = UdlDefinition::fromJson(o);
        QVERIFY(d.isValid());
        QCOMPARE(d.name, QStringLiteral("MyLang"));
        QVERIFY(d.extensions.contains("ml"));
        QVERIFY(d.keywords.contains("then"));
        QCOMPARE(d.lineComment, QStringLiteral("#"));
        QVERIFY(!d.caseSensitive);
    }

    void roundtrip()
    {
        UdlDefinition d;
        d.name = "Lang2";
        d.extensions = {"l2"};
        d.keywords = {"foo", "bar"};
        d.lineComment = "//";
        d.blockCommentStart = "/*";
        d.blockCommentEnd = "*/";
        const auto back = UdlDefinition::fromJson(d.toJson());
        QCOMPARE(back.name, d.name);
        QCOMPARE(back.extensions, d.extensions);
        QCOMPARE(back.keywords, d.keywords);
        QCOMPARE(back.blockCommentStart, d.blockCommentStart);
    }

    void invalidWhenNoName()
    {
        QVERIFY(!UdlDefinition::fromJson(QJsonObject{}).isValid());
    }

    void managerSaveAndFind()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "TestSaveLang";
        d.extensions = {"tsl"};
        d.keywords = {"kw"};
        QVERIFY(mgr.save(d));
        QVERIFY(mgr.findForExtension("tsl") != nullptr);
        QCOMPARE(mgr.findForExtension("tsl")->name, QStringLiteral("TestSaveLang"));
        QVERIFY(mgr.findForExtension("nope") == nullptr);

        // 重新 loadAll 應仍找得到（已持久化）
        UdlManager mgr2;
        mgr2.loadAll();
        QVERIFY(mgr2.findForExtension("tsl") != nullptr);
    }
};

QTEST_APPLESS_MAIN(TestUdl)
#include "test_udl.moc"
