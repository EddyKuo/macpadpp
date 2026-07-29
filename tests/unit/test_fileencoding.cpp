// 單元測試：FileEncoding 偵測與編解碼（FR-019/020）
#include <QtTest>

#include "core/FileEncoding.h"

using namespace macpad::core;

class TestFileEncoding : public QObject {
    Q_OBJECT
private slots:
    void detectsUtf8Bom()
    {
        const QByteArray data = QByteArray("\xEF\xBB\xBF") + "hello";
        const auto r = FileEncoding::detect(data);
        QCOMPARE(r.encoding, Encoding::Utf8Bom);
        QVERIFY(r.hasBom);
    }

    void detectsUtf16LeBom()
    {
        const QByteArray data = QByteArray("\xFF\xFE", 2) + QByteArray("h\0e\0", 4);
        const auto r = FileEncoding::detect(data);
        QCOMPARE(r.encoding, Encoding::Utf16LE);
        QVERIFY(r.hasBom);
    }

    void detectsPlainUtf8()
    {
        const auto r = FileEncoding::detect(QByteArray("plain ascii text"));
        QCOMPARE(r.encoding, Encoding::Utf8);
        QVERIFY(!r.hasBom);
    }

    void detectsInvalidUtf8AsLatin1()
    {
        // 0xFF 單獨出現非合法 UTF-8
        const auto r = FileEncoding::detect(QByteArray("caf\xE9 text"));  // Latin-1 é
        QCOMPARE(r.encoding, Encoding::Latin1);
    }

    void detectsEol_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("eol");
        QTest::newRow("lf")   << QByteArray("a\nb")   << int(Eol::Lf);
        QTest::newRow("crlf") << QByteArray("a\r\nb") << int(Eol::CrLf);
        QTest::newRow("cr")   << QByteArray("a\rb")   << int(Eol::Cr);
    }
    void detectsEol()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, eol);
        QCOMPARE(int(FileEncoding::detect(bytes).eol), eol);
    }

    void roundtrip_data()
    {
        QTest::addColumn<int>("enc");
        QTest::newRow("utf8")    << int(Encoding::Utf8);
        QTest::newRow("utf8bom") << int(Encoding::Utf8Bom);
        QTest::newRow("utf16le") << int(Encoding::Utf16LE);
        QTest::newRow("utf16be") << int(Encoding::Utf16BE);
    }
    void roundtrip()
    {
        QFETCH(int, enc);
        const Encoding e = Encoding(enc);
        const QString original = QStringLiteral("Hello 世界 café �123");
        const QByteArray bytes = FileEncoding::encode(original, e);
        const QString back = FileEncoding::decode(bytes, e);
        QCOMPARE(back, original);
    }

    // 傳統/區域編碼（Qt5Compat QTextCodec）——複刻 NP++ Character sets
    void codecRoundtrip_data()
    {
        QTest::addColumn<QString>("codec");
        QTest::addColumn<QString>("text");
        QTest::newRow("big5")     << QStringLiteral("Big5")      << QStringLiteral("繁體中文測試");
        QTest::newRow("gbk")      << QStringLiteral("GBK")       << QStringLiteral("简体中文测试");
        QTest::newRow("gb18030")  << QStringLiteral("GB18030")   << QStringLiteral("简体中文");
        QTest::newRow("shiftjis") << QStringLiteral("Shift_JIS") << QStringLiteral("日本語テスト");
        QTest::newRow("euckr")    << QStringLiteral("EUC-KR")    << QStringLiteral("한국어테스트");
        QTest::newRow("cp1251")   << QStringLiteral("windows-1251") << QStringLiteral("Привет");
    }
    void codecRoundtrip()
    {
        QFETCH(QString, codec);
        QFETCH(QString, text);
        const QByteArray bytes = FileEncoding::encodeWithCodec(text, codec);
        // 編出來的位元組不應等於 UTF-8（確認真的走了該 codec，而非回退）
        QVERIFY(bytes != text.toUtf8());
        const QString back = FileEncoding::decodeWithCodec(bytes, codec);
        QCOMPARE(back, text);
    }

    void characterSetsMenuNonEmpty()
    {
        const auto groups = FileEncoding::characterSets();
        QVERIFY(groups.size() >= 10);          // 多個地區分組
        bool hasBig5 = false;
        for (const auto &g : groups)
            for (const auto &cs : g.items)
                if (cs.codec == QLatin1String("Big5"))
                    hasBig5 = true;
        QVERIFY(hasBig5);
    }

    // XML/HTML 檔頭字元集宣告嗅探（複刻 Notepad++）——純函式層
    void declaredCharsetSniffing()
    {
        using FE = macpad::core::FileEncoding;

        // XML 宣告
        QCOMPARE(FE::declaredCharsetIn("<?xml version=\"1.0\" encoding=\"Big5\"?><root/>"),
                 QStringLiteral("Big5"));
        // 單引號、大小寫混雜亦須辨識
        QCOMPARE(FE::declaredCharsetIn("<?XML version='1.0' ENCODING='shift_jis' ?>"),
                 QStringLiteral("shift_jis"));

        // HTML5 <meta charset>
        QCOMPARE(FE::declaredCharsetIn("<html><head><meta charset=\"gbk\"></head>"),
                 QStringLiteral("gbk"));
        // 無引號形式
        QCOMPARE(FE::declaredCharsetIn("<meta charset=euc-kr>"), QStringLiteral("euc-kr"));

        // http-equiv 形式
        QCOMPARE(FE::declaredCharsetIn(
                     "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=Big5\">"),
                 QStringLiteral("Big5"));

        // 未宣告 → 空字串
        QVERIFY(FE::declaredCharsetIn("plain text, no declaration").isEmpty());
        QVERIFY(FE::declaredCharsetIn("").isEmpty());

        // 宣告出現在極後段（超過掃描視窗）→ 不予理會，避免掃全檔拖慢大檔
        QByteArray late(6000, ' ');
        late.append("<meta charset=\"big5\">");
        QVERIFY(FE::declaredCharsetIn(late).isEmpty());
    }

    // BOM 必須勝過文件內的文字宣告（BOM 是位元組層級的硬證據）
    void bomBeatsDeclaredCharset()
    {
        QByteArray withBom("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"Big5\"?>");
        const auto r = macpad::core::FileEncoding::detect(withBom);
        QCOMPARE(r.encoding, macpad::core::Encoding::Utf8Bom);
        QVERIFY(r.declaredCharset.isEmpty());   // 有 BOM 就不填，呼叫端不會被誤導
    }
};

QTEST_APPLESS_MAIN(TestFileEncoding)
#include "test_fileencoding.moc"
