// 單元測試：FunctionListParser + ClipboardHistory（FR-029）
#include <QtTest>

#include "features/functionlist/FunctionListParser.h"
#include "features/clipboard/ClipboardHistory.h"
#include "ui/Panels.h"

using namespace macpad::features;

class TestPanels : public QObject {
    Q_OBJECT
private slots:
    void pythonSymbols()
    {
        const QString code =
            "import os\n"
            "def foo(x):\n"
            "    return x\n"
            "class Bar:\n"
            "    def method(self):\n"
            "        pass\n";
        const auto syms = FunctionListParser::parse(code, "python");
        QCOMPARE(syms.size(), 3);
        QCOMPARE(syms[0].name, QStringLiteral("foo"));
        QCOMPARE(syms[0].line, 2);
        QCOMPARE(syms[1].name, QStringLiteral("Bar"));
        QCOMPARE(syms[2].name, QStringLiteral("method"));
    }

    void cppSymbols()
    {
        const QString code =
            "#include <x>\n"
            "class Widget {\n"
            "};\n"
            "int add(int a, int b) {\n"
            "    return a + b;\n"
            "}\n"
            "void MainWindow::doThing()\n"
            "{\n"
            "}\n";
        const auto syms = FunctionListParser::parse(code, "cpp");
        QVector<QString> names;
        for (const auto &s : syms) names.push_back(s.name);
        QVERIFY(names.contains(QStringLiteral("Widget")));
        QVERIFY(names.contains(QStringLiteral("add")));
        QVERIFY(names.contains(QStringLiteral("doThing")));
    }

    void cppIgnoresControlKeywords()
    {
        const QString code = "int f() {\n    if (x) {\n    }\n    while (y) {}\n}\n";
        const auto syms = FunctionListParser::parse(code, "cpp");
        for (const auto &s : syms)
            QVERIFY(s.name != QStringLiteral("if") && s.name != QStringLiteral("while"));
    }

    void javascriptSymbols()
    {
        const QString code =
            "function alpha() {}\n"
            "const beta = (x) => x;\n"
            "export class Gamma {}\n";
        const auto syms = FunctionListParser::parse(code, "javascript");
        QVector<QString> names;
        for (const auto &s : syms) names.push_back(s.name);
        QVERIFY(names.contains(QStringLiteral("alpha")));
        QVERIFY(names.contains(QStringLiteral("beta")));
        QVERIFY(names.contains(QStringLiteral("Gamma")));
    }

    void cppScopeForClassMethods()
    {
        const QString code =
            "class Foo {\n"
            "public:\n"
            "    void bar() {\n"
            "    }\n"
            "    int baz(int x) {\n"
            "        return x;\n"
            "    }\n"
            "};\n"
            "void freeFn() {\n"
            "}\n";
        const auto syms = FunctionListParser::parse(code, "cpp");
        bool foundBar = false, foundBaz = false, foundFree = false, foundFoo = false;
        for (const auto &s : syms) {
            if (s.name == QStringLiteral("Foo")) {
                foundFoo = true;
                QCOMPARE(s.scope, QString());  // 類別本身頂層，無所屬範疇
            }
            if (s.name == QStringLiteral("bar")) {
                foundBar = true;
                QCOMPARE(s.scope, QStringLiteral("Foo"));
            }
            if (s.name == QStringLiteral("baz")) {
                foundBaz = true;
                QCOMPARE(s.scope, QStringLiteral("Foo"));
            }
            if (s.name == QStringLiteral("freeFn")) {
                foundFree = true;
                QCOMPARE(s.scope, QString());  // 頂層自由函式，無所屬範疇
            }
        }
        QVERIFY(foundFoo);
        QVERIFY(foundBar);
        QVERIFY(foundBaz);
        QVERIFY(foundFree);
    }

    void unknownLanguageReturnsEmpty()
    {
        QVERIFY(FunctionListParser::parse("whatever", "").isEmpty());
        QVERIFY(FunctionListParser::languageForSuffix("txt").isEmpty());
    }

    void clipboardDedupAndCap()
    {
        ClipboardHistory h(3);
        h.add("a"); h.add("b"); h.add("c"); h.add("d");  // 超過上限 3
        QCOMPARE(h.items().size(), 3);
        QCOMPARE(h.items().first(), QStringLiteral("d"));  // 最新在前

        h.add("b");  // 既有 → 置頂、不增量
        QCOMPARE(h.items().size(), 3);
        QCOMPARE(h.items().first(), QStringLiteral("b"));
        QCOMPARE(h.items().count("b"), 1);

        h.add("");  // 空字串忽略
        QCOMPARE(h.items().size(), 3);
    }

    // Function List 右鍵「Copy Name」的名稱裁切：去掉尾端「  (行號)」，不誤裁名稱內的 "  ("
    void functionListSymbolNameFromLabel()
    {
        using macpad::ui::FunctionListDock;
        QCOMPARE(FunctionListDock::symbolNameFromLabel(QStringLiteral("doStuff  (42)")),
                 QStringLiteral("doStuff"));
        // 名稱本身含 "  (" → 只裁掉真正的尾端行號後綴
        QCOMPARE(FunctionListDock::symbolNameFromLabel(QStringLiteral("weird  (name)  (7)")),
                 QStringLiteral("weird  (name)"));
        // 無行號後綴 → 原樣返回
        QCOMPARE(FunctionListDock::symbolNameFromLabel(QStringLiteral("plain")),
                 QStringLiteral("plain"));
        // 群組節點（無括號）→ 原樣
        QCOMPARE(FunctionListDock::symbolNameFromLabel(QStringLiteral("MyClass")),
                 QStringLiteral("MyClass"));
    }
};

QTEST_APPLESS_MAIN(TestPanels)
#include "test_panels.moc"
