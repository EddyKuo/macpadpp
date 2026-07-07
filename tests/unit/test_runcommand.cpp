// 單元測試：RunCommand 變數展開與 tokenize（FR-031, CON-006）
#include <QtTest>

#include "features/run/RunCommand.h"

using namespace macpad::features;

class TestRunCommand : public QObject {
    Q_OBJECT
private slots:
    void expandsVariables()
    {
        RunVars v;
        v.fullCurrentPath = "/home/u/a.py";
        v.currentDirectory = "/home/u";
        v.fileName = "a.py";
        v.nameNoExt = "a";
        const QString r = RunCommand::expand(
            "python \"$(FULL_CURRENT_PATH)\" --dir=$(CURRENT_DIRECTORY)", v);
        QCOMPARE(r, QStringLiteral("python \"/home/u/a.py\" --dir=/home/u"));
    }

    void tokenizeRespectsQuotes()
    {
        const QStringList t = RunCommand::tokenize("python \"/path with space/a.py\" -v");
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[0], QStringLiteral("python"));
        QCOMPARE(t[1], QStringLiteral("/path with space/a.py"));  // 引號內空白保留
        QCOMPARE(t[2], QStringLiteral("-v"));
    }

    void tokenizePlain()
    {
        const QStringList t = RunCommand::tokenize("ls -la /tmp");
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[0], QStringLiteral("ls"));
    }

    void tokenizeEmpty()
    {
        QVERIFY(RunCommand::tokenize("").isEmpty());
        QVERIFY(RunCommand::tokenize("   ").isEmpty());
    }

    void noShellMetacharExpansion()
    {
        // tokenize 不解讀 shell 元字元；;、| 等留在 token 內（交由 QProcess 陣列，不經 shell）
        const QStringList t = RunCommand::tokenize("echo hello;rm -rf /");
        QCOMPARE(t[0], QStringLiteral("echo"));
        QCOMPARE(t[1], QStringLiteral("hello;rm"));  // 分號未被當作命令分隔
    }
};

QTEST_APPLESS_MAIN(TestRunCommand)
#include "test_runcommand.moc"
