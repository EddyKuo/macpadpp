// 單元測試：ApiDatabase 自動完成資料庫（FR-055）
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "features/autocomplete/ApiDatabase.h"

using A = macpad::features::ApiDatabase;

class TestApiDatabase : public QObject {
    Q_OBJECT
private slots:
    void entriesForKnownLanguages()
    {
        const QStringList py = A::entriesFor("python");
        QVERIFY(py.contains("def"));
        QVERIFY(py.contains("print"));

        const QStringList cpp = A::entriesFor("cpp");
        QVERIFY(cpp.contains("class"));
        QVERIFY(!cpp.isEmpty());

        const QStringList js = A::entriesFor("javascript");
        QVERIFY(js.contains("function"));

        const QStringList css = A::entriesFor("css");
        QVERIFY(css.contains("color"));

        const QStringList html = A::entriesFor("html");
        QVERIFY(html.contains("div"));

        const QStringList sql = A::entriesFor("sql");
        QVERIFY(sql.contains("SELECT"));

        const QStringList bash = A::entriesFor("bash");
        QVERIFY(bash.contains("echo"));

        const QStringList json = A::entriesFor("json");
        QVERIFY(json.contains("null"));
    }

    void entriesForUnknownLanguageIsEmpty()
    {
        QVERIFY(A::entriesFor("unknown").isEmpty());
        QVERIFY(A::entriesFor("").isEmpty());
    }

    void callTipForKnownWords()
    {
        const QString tip = A::callTipFor("print", "python");
        QVERIFY(!tip.isEmpty());
        QVERIFY(tip.contains("print("));

        QVERIFY(!A::callTipFor("printf", "cpp").isEmpty());
        QVERIFY(!A::callTipFor("console.log", "javascript").isEmpty());
    }

    void callTipForUnknownWordIsEmpty()
    {
        QVERIFY(A::callTipFor("no_such_function", "python").isEmpty());
        QVERIFY(A::callTipFor("print", "unknown_lang").isEmpty());
    }

    void completePathListsMatchingFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QFile f1(dir.filePath("apple.txt"));
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.close();

        QFile f2(dir.filePath("application.log"));
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.close();

        QFile f3(dir.filePath("banana.txt"));
        QVERIFY(f3.open(QIODevice::WriteOnly));
        f3.close();

        const QString fragment = dir.filePath("app");
        const QStringList results = A::completePath(fragment);

        QVERIFY(results.contains("apple.txt"));
        QVERIFY(results.contains("application.log"));
        QVERIFY(!results.contains("banana.txt"));
    }

    void completePathOnEmptyFragmentIsEmpty()
    {
        QVERIFY(A::completePath("").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestApiDatabase)
#include "test_apidb.moc"
