// 單元測試：Notepad++ 相容 API 定義檔解析（call tip / 自動完成的外部擴充來源）
#include <QtTest>

#include "core/EditorWidget.h"
#include "features/autocomplete/ApiFileStore.h"

using macpad::features::ApiEntry;
using macpad::features::ApiFileStore;

class TestApiFileStore : public QObject {
    Q_OBJECT
private slots:
    // Notepad++ 既有的 API 檔可直接沿用，故格式必須逐項對得上
    void parsesNotepadPlusPlusFormat()
    {
        const QByteArray xml = R"XML(<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus>
    <AutoComplete language="C++">
        <Environment ignoreCase="no" startFunc="(" stopFunc=")" paramSeparator="," terminal=";" />
        <KeyWord name="fopen" func="yes">
            <Overload retVal="FILE *" descr="Opens a file">
                <Param name="const char *filename" />
                <Param name="const char *mode" />
            </Overload>
        </KeyWord>
        <KeyWord name="NULL" />
    </AutoComplete>
</NotepadPlus>)XML";

        const auto entries = ApiFileStore::parseXml(xml);
        QCOMPARE(entries.size(), 2);

        QCOMPARE(entries.at(0).name, QStringLiteral("fopen"));
        QVERIFY(entries.at(0).isFunction);
        QCOMPARE(entries.at(0).overloads.size(), 1);
        QCOMPARE(entries.at(0).overloads.at(0),
                 QStringLiteral("FILE * fopen(const char *filename, const char *mode)\nOpens a file"));

        // 非函式關鍵字：只進自動完成清單，沒有簽名
        QCOMPARE(entries.at(1).name, QStringLiteral("NULL"));
        QVERIFY(!entries.at(1).isFunction);
        QVERIFY(entries.at(1).overloads.isEmpty());
    }

    // 多載必須各自成為一行，call tip 才能一次呈現全部
    void parsesMultipleOverloads()
    {
        const QByteArray xml = R"XML(<NotepadPlus><AutoComplete language="C++">
            <KeyWord name="max" func="yes">
                <Overload retVal="int"><Param name="int a" /><Param name="int b" /></Overload>
                <Overload retVal="double"><Param name="double a" /><Param name="double b" /></Overload>
            </KeyWord>
        </AutoComplete></NotepadPlus>)XML";

        const auto entries = ApiFileStore::parseXml(xml);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.at(0).overloads.size(), 2);
        QCOMPARE(entries.at(0).overloads.at(0), QStringLiteral("int max(int a, int b)"));
        QCOMPARE(entries.at(0).overloads.at(1),
                 QStringLiteral("double max(double a, double b)"));
    }

    // 無參數函式不應產生 "()" 以外的怪東西
    void parsesZeroArgFunction()
    {
        const QByteArray xml = R"XML(<NotepadPlus><AutoComplete>
            <KeyWord name="rand" func="yes"><Overload retVal="int" /></KeyWord>
        </AutoComplete></NotepadPlus>)XML";
        const auto entries = ApiFileStore::parseXml(xml);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.at(0).overloads.at(0), QStringLiteral("int rand()"));
    }

    // 壞掉的 XML 一律整份放棄，不得回傳半套資料（IL-4：失敗要明確）
    void malformedXmlYieldsNothing()
    {
        QVERIFY(ApiFileStore::parseXml("<NotepadPlus><AutoComplete>").isEmpty());
        QVERIFY(ApiFileStore::parseXml("not xml at all").isEmpty());
        QVERIFY(ApiFileStore::parseXml(QByteArray()).isEmpty());
    }

    // 沒有 name 的 KeyWord 應被略過，不產生空名條目污染自動完成清單
    void skipsUnnamedKeywords()
    {
        const QByteArray xml = R"XML(<NotepadPlus><AutoComplete>
            <KeyWord func="yes"><Overload retVal="int" /></KeyWord>
            <KeyWord name="ok" />
        </AutoComplete></NotepadPlus>)XML";
        const auto entries = ApiFileStore::parseXml(xml);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.at(0).name, QStringLiteral("ok"));
    }

    // 檔案不存在是正常情況（使用者未提供 API 檔），不得視為錯誤
    void missingFileIsEmptyNotAnError()
    {
        QVERIFY(ApiFileStore::parseFile(QStringLiteral("/no/such/api/file.xml")).isEmpty());
    }

    // 多載切換狀態：只有真的有多個多載時才進入切換模式
    void callTipOverloadCycling()
    {
        macpad::core::EditorWidget ed;
        // 單一簽名：不進入多載模式（外觀與原本一致）
        ed.showCallTips({QStringLiteral("int f(int a)")});
        QCOMPARE(ed.currentCallTipOverload(), -1);

        // 多個簽名：從第 0 個開始
        ed.showCallTips({QStringLiteral("int f(int)"), QStringLiteral("double f(double)"),
                         QStringLiteral("char f(char)")});
        QCOMPARE(ed.currentCallTipOverload(), 0);

        // 關閉後狀態需清空，否則之後點箭頭會對著已關閉的提示切換
        ed.cancelCallTip();
        QCOMPARE(ed.currentCallTipOverload(), -1);

        // 空清單與全空字串一律不顯示
        ed.showCallTips({});
        QCOMPARE(ed.currentCallTipOverload(), -1);
        ed.showCallTips({QString(), QString()});
        QCOMPARE(ed.currentCallTipOverload(), -1);
    }

    void filePathFollowsConfigDirLayout()
    {
        const QString p = ApiFileStore::filePathFor(QStringLiteral("cpp"));
        QVERIFY(p.endsWith(QStringLiteral("apis/cpp.xml")));
        QVERIFY(ApiFileStore::filePathFor(QString()).isEmpty());
    }
};

QTEST_MAIN(TestApiFileStore)
#include "test_apifilestore.moc"
