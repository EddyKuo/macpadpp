// 單元測試：FunctionListConfig — 內建預設規則、使用者 JSON 覆寫、overrideMap 副檔名對照
#include <QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

#include "features/functionlist/FunctionListConfig.h"

using namespace macpad::features;

class TestFunctionListConfig : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 清空 functionlist/ 覆寫檔，確保每個測試從乾淨狀態出發
        QDir(FunctionListConfig::configDirPath()).removeRecursively();
        FunctionListConfig::reloadCache();
    }

    void cleanup()
    {
        QDir(FunctionListConfig::configDirPath()).removeRecursively();
        FunctionListConfig::reloadCache();
    }

    // 內建語言（python/cpp/javascript/java）具有效規則
    void builtinRulesValid()
    {
        QVERIFY(FunctionListConfig::builtinRule(QStringLiteral("python")).isValid());
        QVERIFY(FunctionListConfig::builtinRule(QStringLiteral("cpp")).isValid());
        QVERIFY(FunctionListConfig::builtinRule(QStringLiteral("javascript")).isValid());
        QVERIFY(FunctionListConfig::builtinRule(QStringLiteral("java")).isValid());
        // 未知語言 → 無效
        QVERIFY(!FunctionListConfig::builtinRule(QStringLiteral("nosuchlang")).isValid());
    }

    // ruleForLanguage 在無使用者檔時退回內建（等同 builtinRule）
    void ruleFallsBackToBuiltin()
    {
        const FunctionListRule r = FunctionListConfig::ruleForLanguage(QStringLiteral("python"));
        QVERIFY(r.isValid());
        QCOMPARE(r.functionExprs,
                 FunctionListConfig::builtinRule(QStringLiteral("python")).functionExprs);
    }

    // 使用者 JSON 檔覆寫語言規則
    void userJsonOverrides()
    {
        const QString dir = FunctionListConfig::configDirPath();
        QVERIFY(QDir().mkpath(dir));
        QFile f(dir + QStringLiteral("/kotlin.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"json({
            "functionExprs": ["\\bfun\\s+(\\w+)"],
            "classExpr": "\\bclass\\s+(\\w+)",
            "trackScope": true,
            "keywordExclusions": ["if", "for"]
        })json");
        f.close();
        FunctionListConfig::reloadCache();

        const FunctionListRule r = FunctionListConfig::ruleForLanguage(QStringLiteral("kotlin"));
        QVERIFY(r.isValid());
        QCOMPARE(r.functionExprs.size(), 1);
        QCOMPARE(r.classExpr, QStringLiteral("\\bclass\\s+(\\w+)"));
        QVERIFY(r.trackScope);
        QVERIFY(r.keywordExclusions.contains(QStringLiteral("if")));
    }

    // overrideMap.json：副檔名 → 語言鍵
    void overrideMapSuffix()
    {
        const QString dir = FunctionListConfig::configDirPath();
        QVERIFY(QDir().mkpath(dir));
        QFile f(dir + QStringLiteral("/overrideMap.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"json({ "kt": "kotlin", "cxx": "cpp" })json");
        f.close();
        FunctionListConfig::reloadCache();

        QCOMPARE(FunctionListConfig::languageOverrideForSuffix(QStringLiteral("kt")),
                 QStringLiteral("kotlin"));
        QCOMPARE(FunctionListConfig::languageOverrideForSuffix(QStringLiteral("cxx")),
                 QStringLiteral("cpp"));
        // 未列出者 → 空字串
        QVERIFY(FunctionListConfig::languageOverrideForSuffix(QStringLiteral("zzz")).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestFunctionListConfig)
#include "test_functionlistconfig.moc"
