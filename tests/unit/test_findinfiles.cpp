// 單元測試：FindInFilesEngine 目錄搜尋（FR-013）
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "features/findinfiles/FindInFilesEngine.h"

using namespace macpad::features;

class TestFindInFiles : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    void writeFile(const QString &rel, const QByteArray &content)
    {
        const QString path = m_dir.path() + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
        f.close();
    }

private slots:
    void initTestCase()
    {
        writeFile("a.txt", "hello world\nTODO: fix this\nanother line");
        writeFile("b.cpp", "int main() { return 0; } // TODO later");
        writeFile("sub/c.txt", "nested TODO here");
        writeFile("bin.dat", QByteArray("binary\0data", 11));
    }

    void findsPlainAcrossDir()
    {
        FindInFilesOptions o;
        o.pattern = "TODO";
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        // a.txt, b.cpp, sub/c.txt 各一 → 3
        QCOMPARE(r.size(), 3);
    }

    void fileFilterLimitsToExtension()
    {
        FindInFilesOptions o;
        o.pattern = "TODO";
        o.fileFilters = {"*.cpp"};
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        QCOMPARE(r.size(), 1);
        QVERIFY(r.first().filePath.endsWith(".cpp"));
    }

    void nonRecursiveExcludesSubdir()
    {
        FindInFilesOptions o;
        o.pattern = "TODO";
        o.recursive = false;
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        // 只有 a.txt, b.cpp（不含 sub/c.txt）
        QCOMPARE(r.size(), 2);
    }

    void regexMatch()
    {
        FindInFilesOptions o;
        o.pattern = "TODO:?\\s*\\w+";
        o.regex = true;
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        QVERIFY(r.size() >= 2);
    }

    void reportsLineAndColumn()
    {
        FindInFilesOptions o;
        o.pattern = "fix";
        o.fileFilters = {"a.txt"};
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.first().line, 2);      // 第 2 行
        QVERIFY(r.first().column > 0);
    }

    void skipsBinaryFiles()
    {
        FindInFilesOptions o;
        o.pattern = "binary";
        const auto r = FindInFilesEngine::search(m_dir.path(), o);
        QCOMPARE(r.size(), 0);  // bin.dat 含 NUL → 跳過
    }

    void cancelStopsSearch()
    {
        FindInFilesOptions o;
        o.pattern = "TODO";
        std::atomic<bool> cancel{true};  // 立即取消
        const auto r = FindInFilesEngine::search(m_dir.path(), o, &cancel);
        QCOMPARE(r.size(), 0);
    }

    void replaceInTextPlain()
    {
        FindInFilesOptions o;
        o.pattern = "TODO";
        int n = 0;
        const QString out = FindInFilesEngine::replaceInText("a TODO b TODO", o, "DONE", &n);
        QCOMPARE(n, 2);
        QCOMPARE(out, QStringLiteral("a DONE b DONE"));
    }

    void replaceInTextRegex()
    {
        FindInFilesOptions o;
        o.pattern = "(\\w+)=(\\d+)";
        o.regex = true;
        int n = 0;
        const QString out = FindInFilesEngine::replaceInText("x=1 y=22", o, "\\1:\\2", &n);
        QCOMPARE(n, 2);
        QCOMPARE(out, QStringLiteral("x:1 y:22"));
    }

    void replaceInFilesWritesBack()
    {
        writeFile("rep.txt", "alpha beta alpha");
        FindInFilesOptions o;
        o.pattern = "alpha";
        o.fileFilters = {"rep.txt"};
        const auto r = FindInFilesEngine::replaceInFiles(m_dir.path(), o, "X");
        QCOMPARE(r.filesChanged, 1);
        QCOMPARE(r.replacements, 2);
        QFile f(m_dir.path() + "/rep.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("X beta X"));
    }

    // FR-045：includeHidden + excludeFilters
    void hiddenAndExcludeFilters()
    {
        writeFile(".hidden/secret.txt", "TODO hidden");
        writeFile("node_modules/pkg/index.js", "TODO in node_modules");
        writeFile("keep.min.js", "TODO minified");

        // 預設：隱藏目錄不納入
        {
            FindInFilesOptions o;
            o.pattern = "TODO";
            o.fileFilters = {"*.txt"};
            const auto r = FindInFilesEngine::search(m_dir.path(), o);
            for (const auto &m : r)
                QVERIFY(!m.filePath.contains(".hidden"));
        }

        // includeHidden=true：隱藏目錄的檔案應被找到
        {
            FindInFilesOptions o;
            o.pattern = "TODO";
            o.includeHidden = true;
            o.fileFilters = {"*.txt"};
            const auto r = FindInFilesEngine::search(m_dir.path(), o);
            bool foundHidden = false;
            for (const auto &m : r)
                if (m.filePath.contains(".hidden"))
                    foundHidden = true;
            QVERIFY(foundHidden);
        }

        // excludeFilters：排除整個 node_modules 子目錄
        {
            FindInFilesOptions o;
            o.pattern = "TODO";
            o.excludeFilters = {"!+\\node_modules"};
            const auto r = FindInFilesEngine::search(m_dir.path(), o);
            for (const auto &m : r)
                QVERIFY(!m.filePath.contains("node_modules"));
        }

        // excludeFilters：萬用字元排除特定檔名樣式
        {
            FindInFilesOptions o;
            o.pattern = "TODO";
            o.excludeFilters = {"!*.min.js"};
            const auto r = FindInFilesEngine::search(m_dir.path(), o);
            for (const auto &m : r)
                QVERIFY(!m.filePath.endsWith("keep.min.js"));
        }

        // isExcluded 直接單元測試
        QVERIFY(FindInFilesEngine::isExcluded("node_modules/pkg/index.js", "index.js",
                                              {"!+\\node_modules"}));
        QVERIFY(FindInFilesEngine::isExcluded("keep.min.js", "keep.min.js", {"!*.min.js"}));
        QVERIFY(!FindInFilesEngine::isExcluded("a.txt", "a.txt", {"!*.min.js"}));
    }

    // searchInFiles：以明確檔案清單搜尋（Find in Projects 引擎），不遞迴目錄
    void searchInExplicitFileList()
    {
        FindInFilesOptions opts;
        opts.pattern = QStringLiteral("TODO");
        const QStringList files = {
            m_dir.path() + QStringLiteral("/a.txt"),
            m_dir.path() + QStringLiteral("/b.cpp"),
            m_dir.path() + QStringLiteral("/sub/c.txt"),
        };
        const QVector<FindMatch> hits = FindInFilesEngine::searchInFiles(files, opts);
        // a.txt(1) + b.cpp(1) + sub/c.txt(1) = 3
        QCOMPARE(hits.size(), 3);
        // 只搜清單內的檔，不存在的路徑安全略過
        const QStringList withMissing = files + QStringList{m_dir.path() + QStringLiteral("/nope.txt")};
        QCOMPARE(FindInFilesEngine::searchInFiles(withMissing, opts).size(), 3);
    }
};

QTEST_APPLESS_MAIN(TestFindInFiles)
#include "test_findinfiles.moc"
