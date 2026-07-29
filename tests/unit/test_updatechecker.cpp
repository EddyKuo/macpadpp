// 單元測試：版本比較（更新檢查的核心判定）。
// 網路查詢本身不在單元測試範圍——測試不應依賴外部服務。
#include <QtTest>

#include "features/update/UpdateChecker.h"

using macpad::features::compareVersions;

class TestUpdateChecker : public QObject {
    Q_OBJECT
private slots:
    void compare_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");

        QTest::newRow("equal")            << "1.2.3"  << "1.2.3"  << 0;
        QTest::newRow("patch newer")      << "1.2.3"  << "1.2.4"  << -1;
        QTest::newRow("patch older")      << "1.2.5"  << "1.2.4"  << 1;
        QTest::newRow("minor")            << "1.2.9"  << "1.3.0"  << -1;
        QTest::newRow("major")            << "1.9.9"  << "2.0.0"  << -1;
        // 字串比較會誤判 "1.2.10" < "1.2.9"，數值比較不會
        QTest::newRow("double digit")     << "1.2.9"  << "1.2.10" << -1;
        // 'v' 前綴（GitHub tag 慣例）
        QTest::newRow("v prefix")         << "0.5.2"  << "v0.5.3" << -1;
        QTest::newRow("both v prefix")    << "v1.0.0" << "v1.0.0" << 0;
        // 段數不同：缺少的段視為 0
        QTest::newRow("shorter equal")    << "1.2"    << "1.2.0"  << 0;
        QTest::newRow("shorter older")    << "1.2"    << "1.2.1"  << -1;
        // 預發布後綴取前導數字
        QTest::newRow("prerelease suffix")<< "1.2.3"  << "1.2.3-rc1" << 0;
        // 完全非數字段落視為 0，不得崩潰
        QTest::newRow("garbage")          << "abc"    << "0.0.0"  << 0;
    }
    void compare()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(compareVersions(a, b), expected);
        QCOMPARE(compareVersions(b, a), -expected);   // 反向必須對稱
    }
};

QTEST_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
