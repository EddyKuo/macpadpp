// 單元測試：DesktopIntegration — 跨平台桌面整合的純函式（不啟動任何行程）。
// 驗證各平台產生正確的 program/args 與等寬字型選擇，避免真的開視窗。
#include <QtTest>
#include <QFontDatabase>

#include "platform/DesktopIntegration.h"

using namespace macpad::platform;

class TestDesktopIntegration : public QObject {
    Q_OBJECT
private slots:

    // revealArgsFor：各平台回傳對應的檔案管理器指令
    void revealArgsForPlatform()
    {
        const ShellCommand cmd = revealArgsFor(QStringLiteral("C:/dir/file.txt"));
#if defined(Q_OS_MACOS)
        QCOMPARE(cmd.program, QStringLiteral("open"));
        QCOMPARE(cmd.args.size(), 2);
        QCOMPARE(cmd.args.at(0), QStringLiteral("-R"));
        QCOMPARE(cmd.args.at(1), QStringLiteral("C:/dir/file.txt"));
        QVERIFY(cmd.isValid());
#elif defined(Q_OS_WIN)
        QCOMPARE(cmd.program, QStringLiteral("explorer.exe"));
        QCOMPARE(cmd.args.size(), 1);
        // /select, 緊接路徑、無空格；路徑部分須為 native（反斜線、無正斜線）
        const QString prefix = QStringLiteral("/select,");
        QVERIFY(cmd.args.at(0).startsWith(prefix));
        const QString pathPart = cmd.args.at(0).mid(prefix.size());
        QVERIFY(pathPart.contains(QLatin1Char('\\')));
        QVERIFY(!pathPart.contains(QLatin1Char('/')));  // 路徑部分不應殘留正斜線
        QVERIFY(cmd.isValid());
#else
        // 其他平台：無對應指令（呼叫端改開所在目錄）
        QVERIFY(!cmd.isValid());
#endif
    }

    // terminalCommandFor：各平台回傳對應的終端機指令
    void terminalCommandForPlatform()
    {
        const ShellCommand cmd = terminalCommandFor(QStringLiteral("C:/some/dir"));
#if defined(Q_OS_MACOS)
        QCOMPARE(cmd.program, QStringLiteral("open"));
        QVERIFY(cmd.args.contains(QStringLiteral("Terminal")));
        QVERIFY(cmd.isValid());
#elif defined(Q_OS_WIN)
        QCOMPARE(cmd.program, QStringLiteral("wt.exe"));
        QCOMPARE(cmd.args.at(0), QStringLiteral("-d"));
        QVERIFY(cmd.args.at(1).contains(QLatin1Char('\\')));  // native 路徑
        QVERIFY(cmd.isValid());
#else
        QVERIFY(!cmd.isValid());
#endif
    }

    // defaultMonospaceFamily：回傳非空字型名，且屬於該平台的預期集合
    void defaultMonospaceFamilyIsSensible()
    {
        const QString fam = defaultMonospaceFamily();
        QVERIFY(!fam.isEmpty());
#if defined(Q_OS_MACOS)
        QCOMPARE(fam, QStringLiteral("Menlo"));
#elif defined(Q_OS_WIN)
        const QStringList expected = {QStringLiteral("Cascadia Mono"),
                                      QStringLiteral("Consolas"),
                                      QStringLiteral("Courier New")};
        QVERIFY2(expected.contains(fam), qPrintable(fam));
#else
        QCOMPARE(fam, QStringLiteral("Monospace"));
#endif
    }

    // 空路徑：純函式仍回傳結構（動作函式會早退，但純函式不需防呆到崩潰）
    void emptyPathDoesNotCrash()
    {
        const ShellCommand r = revealArgsFor(QString());
        const ShellCommand t = terminalCommandFor(QString());
        Q_UNUSED(r);
        Q_UNUSED(t);
        QVERIFY(true);
    }
};

QTEST_MAIN(TestDesktopIntegration)
#include "test_desktopintegration.moc"
