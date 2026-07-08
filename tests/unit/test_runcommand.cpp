// 單元測試：RunCommand 變數展開與 tokenize（FR-031, CON-006）
#include <QtTest>

#include "features/run/RunCommand.h"
#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace macpad::features;
using macpad::persistence::AppPaths;
using macpad::persistence::JsonFile;

class TestRunCommand : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        // 每個測試使用獨立的暫存設定目錄，避免互相汙染、避免碰觸真實使用者設定
        m_tempDir = new QTemporaryDir();
        QVERIFY(m_tempDir->isValid());
        AppPaths::setConfigDirOverride(m_tempDir->path());
    }

    void cleanup()
    {
        AppPaths::setConfigDirOverride(QString());
        delete m_tempDir;
        m_tempDir = nullptr;
    }

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

    void expandsCurrentWordVariable()
    {
        RunVars v;
        v.currentWord = "foobar";
        const QString r = RunCommand::expand("grep $(CURRENT_WORD) file.txt", v);
        QCOMPARE(r, QStringLiteral("grep foobar file.txt"));
    }

    void expandsNameNoExtVariable()
    {
        RunVars v;
        v.nameNoExt = "report";
        const QString r = RunCommand::expand("echo $(NAME_PART)", v);
        QCOMPARE(r, QStringLiteral("echo report"));
    }

    // ---- RunCommandStore ----

    void storeLoadWhenFileMissingReturnsEmpty()
    {
        QVERIFY(RunCommandStore::load().isEmpty());
    }

    void storeSaveAndLoadRoundTripsAllFields()
    {
        QList<RunCommandEntry> entries;
        RunCommandEntry e;
        e.name = "Run Python";
        e.command = "python $(FULL_CURRENT_PATH)";
        e.shortcut = "Ctrl+Shift+F5";
        entries << e;

        QVERIFY(RunCommandStore::save(entries));

        const QList<RunCommandEntry> loaded = RunCommandStore::load();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].name, QStringLiteral("Run Python"));
        QCOMPARE(loaded[0].command, QStringLiteral("python $(FULL_CURRENT_PATH)"));
        QCOMPARE(loaded[0].shortcut, QStringLiteral("Ctrl+Shift+F5"));
    }

    void storeMultipleCommandsPreserveOrder()
    {
        QList<RunCommandEntry> entries;
        RunCommandEntry a;
        a.name = "First";
        a.command = "echo first";
        a.shortcut = "";
        RunCommandEntry b;
        b.name = "Second";
        b.command = "echo second";
        b.shortcut = "Ctrl+2";
        RunCommandEntry c;
        c.name = "Third";
        c.command = "echo third";
        c.shortcut = "";
        entries << a << b << c;

        QVERIFY(RunCommandStore::save(entries));

        const QList<RunCommandEntry> loaded = RunCommandStore::load();
        QCOMPARE(loaded.size(), 3);
        QCOMPARE(loaded[0].name, QStringLiteral("First"));
        QCOMPARE(loaded[1].name, QStringLiteral("Second"));
        QCOMPARE(loaded[1].shortcut, QStringLiteral("Ctrl+2"));
        QCOMPARE(loaded[2].name, QStringLiteral("Third"));
    }

    void storeOverwriteBySameNameReplacesEntry()
    {
        QList<RunCommandEntry> entries;
        RunCommandEntry e;
        e.name = "Build";
        e.command = "make";
        e.shortcut = "Ctrl+B";
        entries << e;
        QVERIFY(RunCommandStore::save(entries));

        // 模擬呼叫端以同名項目覆寫（先 load、修改、再 save 回去）
        QList<RunCommandEntry> reloaded = RunCommandStore::load();
        QCOMPARE(reloaded.size(), 1);
        reloaded[0].command = "make all";
        reloaded[0].shortcut = "Ctrl+Shift+B";
        QVERIFY(RunCommandStore::save(reloaded));

        const QList<RunCommandEntry> final_ = RunCommandStore::load();
        QCOMPARE(final_.size(), 1);
        QCOMPARE(final_[0].name, QStringLiteral("Build"));
        QCOMPARE(final_[0].command, QStringLiteral("make all"));
        QCOMPARE(final_[0].shortcut, QStringLiteral("Ctrl+Shift+B"));
    }

    void storeLoadBackwardCompatMissingShortcutDefaultsEmpty()
    {
        // 手動寫入一筆缺少 shortcut 鍵的舊版 JSON，驗證向下相容
        QJsonObject item;
        item.insert(QStringLiteral("name"), QStringLiteral("Legacy"));
        item.insert(QStringLiteral("command"), QStringLiteral("legacy_cmd.sh"));
        // 故意不寫入 "shortcut" 鍵

        QJsonArray items;
        items.append(item);
        QJsonObject root;
        root.insert(QStringLiteral("schema_version"), 1);
        root.insert(QStringLiteral("items"), items);

        const QString path = AppPaths::filePath(QStringLiteral("run_commands.json"));
        QVERIFY(JsonFile::save(path, root));

        const QList<RunCommandEntry> loaded = RunCommandStore::load();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].name, QStringLiteral("Legacy"));
        QCOMPARE(loaded[0].command, QStringLiteral("legacy_cmd.sh"));
        QCOMPARE(loaded[0].shortcut, QString());  // 舊檔缺鍵 → 預設空字串
    }

    void storeLoadSkipsEntryWithEmptyName()
    {
        QJsonObject validItem;
        validItem.insert(QStringLiteral("name"), QStringLiteral("Valid"));
        validItem.insert(QStringLiteral("command"), QStringLiteral("echo ok"));
        validItem.insert(QStringLiteral("shortcut"), QString());

        QJsonObject emptyNameItem;
        emptyNameItem.insert(QStringLiteral("name"), QString());
        emptyNameItem.insert(QStringLiteral("command"), QStringLiteral("echo skipped"));
        emptyNameItem.insert(QStringLiteral("shortcut"), QString());

        QJsonArray items;
        items.append(emptyNameItem);
        items.append(validItem);
        QJsonObject root;
        root.insert(QStringLiteral("schema_version"), 1);
        root.insert(QStringLiteral("items"), items);

        const QString path = AppPaths::filePath(QStringLiteral("run_commands.json"));
        QVERIFY(JsonFile::save(path, root));

        const QList<RunCommandEntry> loaded = RunCommandStore::load();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].name, QStringLiteral("Valid"));
    }

private:
    QTemporaryDir *m_tempDir = nullptr;
};

QTEST_APPLESS_MAIN(TestRunCommand)
#include "test_runcommand.moc"
