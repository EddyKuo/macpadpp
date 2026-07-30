// 單元測試：UdlXmlIo — Notepad++ userDefineLang.xml 匯出/匯入 round-trip、失敗寬容
#include <QtTest>
#include <QTemporaryDir>

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlXmlIo.h"

using namespace macpad::features;

class TestUdlXmlIo : public QObject {
    Q_OBJECT
private slots:
    // 匯出後再匯入，核心欄位保持一致
    void roundtrip()
    {
        UdlDefinition in;
        in.name = QStringLiteral("MyLang");
        in.extensions = {QStringLiteral("mylang"), QStringLiteral("ml")};
        in.caseSensitive = false;
        in.lineComment = QStringLiteral("//");
        in.blockCommentStart = QStringLiteral("/*");
        in.blockCommentEnd = QStringLiteral("*/");
        in.keywordGroups.append({QStringLiteral("if"), QStringLiteral("else"),
                                 QStringLiteral("while")});
        in.operators = {QStringLiteral("+"), QStringLiteral("-"), QStringLiteral("=")};
        UdlDelimiter d;
        d.open = QStringLiteral("\"");
        d.escape = QStringLiteral("\\");
        d.close = QStringLiteral("\"");
        in.delimiters.append(d);
        UdlStyle st;
        st.fg = QStringLiteral("#FF8800");
        st.bold = true;
        in.styles.insert(1, st);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("udl.xml"));
        QVERIFY(UdlXmlIo::exportToXml(in, path));

        const UdlDefinition out = UdlXmlIo::importFromXml(path);
        QVERIFY(out.isValid());
        QCOMPARE(out.name, QStringLiteral("MyLang"));
        QCOMPARE(out.caseSensitive, false);
        QCOMPARE(out.lineComment, QStringLiteral("//"));
        QCOMPARE(out.blockCommentStart, QStringLiteral("/*"));
        QCOMPARE(out.blockCommentEnd, QStringLiteral("*/"));
        // 副檔名保序集合（以 join 比對避免順序敏感）
        QVERIFY(out.extensions.contains(QStringLiteral("mylang")));
        QVERIFY(out.extensions.contains(QStringLiteral("ml")));
        // 關鍵字群組 0 應含原三個關鍵字
        const QSet<QString> &g0 = out.keywordGroup(0);
        QVERIFY(g0.contains(QStringLiteral("if")));
        QVERIFY(g0.contains(QStringLiteral("else")));
        QVERIFY(g0.contains(QStringLiteral("while")));
        // 運算子
        QVERIFY(out.operators.contains(QStringLiteral("+")));
        QVERIFY(out.operators.contains(QStringLiteral("=")));
        // 分隔符第一組
        QCOMPARE(out.delimiters.size(), 1);
        QCOMPARE(out.delimiters[0].open, QStringLiteral("\""));
        QCOMPARE(out.delimiters[0].close, QStringLiteral("\""));
        // 樣式 fg 色（大小寫不敏感比對）
        QVERIFY(out.styles.contains(1));
        QCOMPARE(out.styles.value(1).fg.toUpper(), QStringLiteral("#FF8800"));
        QCOMPARE(out.styles.value(1).bold, true);
    }

    // Nesting 遮罩的可讀字串格式（供 UDL 編輯器輸入）
    void nestingSpecRoundTrip()
    {
        const int mask = UdlNest::Comment | UdlNest::Number
                       | UdlNest::keywordBit(0) | UdlNest::keywordBit(7)
                       | UdlNest::delimiterBit(1);
        const QString spec = UdlNest::toSpec(mask);
        QCOMPARE(UdlNest::fromSpec(spec), mask);
        // 具體字串形式（順序固定，便於人工閱讀與 diff）
        QCOMPARE(spec, QStringLiteral("comment,number,kw1,kw8,delim2"));

        // 空字串 / 無法辨識的名稱一律忽略，不臆測
        QCOMPARE(UdlNest::fromSpec(QString()), 0);
        QCOMPARE(UdlNest::fromSpec(QStringLiteral("bogus, kw99, delim0")), 0);
        // 空白容忍
        QCOMPARE(UdlNest::fromSpec(QStringLiteral(" number , KW2 ")),
                 UdlNest::Number | UdlNest::keywordBit(1));
    }

    // Notepad++ 的 nesting 位元配置與本專案不同，匯入/匯出必須逐位轉換，
    // 否則巢狀設定會對應到錯誤的類別。以 XML 往返驗證對映正確。
    void nestingSurvivesXmlRoundTrip()
    {
        UdlDefinition in;
        in.name = QStringLiteral("NestLang");
        in.lineComment = QStringLiteral("//");
        in.blockCommentStart = QStringLiteral("/*");
        in.blockCommentEnd = QStringLiteral("*/");
        in.blockCommentNesting = UdlNest::keywordBit(0) | UdlNest::Number;
        in.lineCommentNesting = UdlNest::keywordBit(2);
        UdlDelimiter d;
        d.open = QStringLiteral("\"");
        d.close = QStringLiteral("\"");
        d.nesting = UdlNest::Number | UdlNest::Operator;
        in.delimiters.append(d);
        // nesting 掛在 WordsStyle 上，故必須有對應樣式元素才寫得出來
        // （1 = Notepad++ COMMENT、2 = COMMENTLINE、16 = DELIMITER1）
        UdlStyle st;
        st.fg = QStringLiteral("#101010");
        in.styles.insert(1, st);
        in.styles.insert(2, st);
        in.styles.insert(16, st);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("nest.xml"));
        QVERIFY(UdlXmlIo::exportToXml(in, path));

        const UdlDefinition out = UdlXmlIo::importFromXml(path);
        QVERIFY(out.isValid());
        QCOMPARE(out.blockCommentNesting, in.blockCommentNesting);
        QCOMPARE(out.lineCommentNesting, in.lineCommentNesting);
        QCOMPARE(out.delimiters.size(), 1);
        QCOMPARE(out.delimiters.at(0).nesting, d.nesting);
    }

    // 上一個測試只證明「自己寫、自己讀」一致——若常數抄錯，兩邊會一起錯而測試照樣過。
    // 這裡改用寫死的 Notepad++ 原生數值當 fixture，直接驗證對映是否真的對得上上游：
    //   nesting="67108864"  = SCE_USER_MASK_NESTING_NUMBERS   (0x4000000)
    //   nesting="16777216"  = SCE_USER_MASK_NESTING_OPERATORS1(0x1000000)
    //   styleID="16"        = SCE_USER_STYLE_DELIMITER1
    //   styleID="1"/"2"     = SCE_USER_STYLE_COMMENT / COMMENTLINE
    void importsLiteralNotepadPlusPlusNestingValues()
    {
        const QByteArray xml = R"XML(<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus>
  <UserLang name="Fixture" ext="fx" udlVersion="2.1">
    <Settings><Global caseIgnored="no" /></Settings>
    <KeywordLists>
      <Keywords name="Comments">00// 01/* 02*/</Keywords>
      <Keywords name="Delimiters">00&quot; 01\ 02&quot;</Keywords>
    </KeywordLists>
    <Styles>
      <WordsStyle name="COMMENT" styleID="1" fgColor="008000" bgColor="FFFFFF" fontStyle="0" nesting="67108864" />
      <WordsStyle name="COMMENTLINE" styleID="2" fgColor="008000" bgColor="FFFFFF" fontStyle="0" nesting="16777216" />
      <WordsStyle name="DELIMITERS1" styleID="16" fgColor="808080" bgColor="FFFFFF" fontStyle="0" nesting="1024" />
    </Styles>
  </UserLang>
</NotepadPlus>)XML";

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("fixture.xml"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(xml);
        f.close();

        const UdlDefinition out = UdlXmlIo::importFromXml(path);
        QVERIFY(out.isValid());
        // NUMBERS → 內部的 Number（若常數抄成 FOLDERS_IN_CODE2 系列，這裡會是 0）
        QCOMPARE(out.blockCommentNesting, int(UdlNest::Number));
        // OPERATORS1 → 內部的 Operator
        QCOMPARE(out.lineCommentNesting, int(UdlNest::Operator));
        // styleID 16 必須被認成第 1 組分隔符；KEYWORD1(1024) → kw1
        QCOMPARE(out.delimiters.size(), 1);
        QCOMPARE(out.delimiters.at(0).nesting, UdlNest::keywordBit(0));
    }

    // 不存在的檔案 → isValid()==false（不崩潰）
    void invalidPathReturnsInvalid()
    {
        const UdlDefinition out = UdlXmlIo::importFromXml(
            QStringLiteral("/nonexistent/path/nope.xml"));
        QVERIFY(!out.isValid());
    }

    // 匯出無效定義（無 name）應失敗
    void exportInvalidFails()
    {
        UdlDefinition empty;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!UdlXmlIo::exportToXml(empty, dir.filePath(QStringLiteral("x.xml"))));
    }
};

QTEST_APPLESS_MAIN(TestUdlXmlIo)
#include "test_udlxmlio.moc"
