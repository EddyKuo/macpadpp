// 單元測試：CliArgs path:line 解析（FR-033）
#include <QtTest>

#include "features/cli/CliArgs.h"

using namespace macpad::features;

class TestCliArgs : public QObject {
    Q_OBJECT
private slots:
    void plainPath()
    {
        const auto a = CliArgs::parseFileArg("/home/u/file.txt");
        QCOMPARE(a.path, QStringLiteral("/home/u/file.txt"));
        QCOMPARE(a.line, 0);
    }

    void pathWithLine()
    {
        const auto a = CliArgs::parseFileArg("/home/u/file.txt:120");
        QCOMPARE(a.path, QStringLiteral("/home/u/file.txt"));
        QCOMPARE(a.line, 120);
    }

    void trailingColonNonNumberIsPath()
    {
        const auto a = CliArgs::parseFileArg("/home/u/weird:name");
        QCOMPARE(a.path, QStringLiteral("/home/u/weird:name"));
        QCOMPARE(a.line, 0);
    }

    void relativePathWithLine()
    {
        const auto a = CliArgs::parseFileArg("src/main.cpp:42");
        QCOMPARE(a.path, QStringLiteral("src/main.cpp"));
        QCOMPARE(a.line, 42);
    }

    void zeroLineTreatedAsNoLine()
    {
        const auto a = CliArgs::parseFileArg("f.txt:0");
        // 0 非正整數 → 視為路徑一部分
        QCOMPARE(a.line, 0);
        QCOMPARE(a.path, QStringLiteral("f.txt:0"));
    }
};

QTEST_APPLESS_MAIN(TestCliArgs)
#include "test_cliargs.moc"
