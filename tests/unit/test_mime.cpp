// 單元測試：MimeTools MIME 編碼工具（Notepad++ MIME Tools 選單對等）
#include <QtTest>

#include "features/mime/MimeTools.h"

using M = macpad::features::MimeTools;

class TestMimeTools : public QObject {
    Q_OBJECT
private slots:
    // --- Base64 ---
    void base64RoundTrip()
    {
        const QString original = QStringLiteral("Hello, 世界！123");
        const QString encoded = M::base64Encode(original);
        QCOMPARE(M::base64Decode(encoded), original);
    }

    void base64KnownVector()
    {
        // 經典測試向量："Man" -> "TWFu"
        QCOMPARE(M::base64Encode("Man"), QStringLiteral("TWFu"));
        QCOMPARE(M::base64Decode("TWFu"), QStringLiteral("Man"));
    }

    // --- URL ---
    void urlRoundTrip()
    {
        const QString original = QStringLiteral("hello world/測試?a=1&b=2");
        const QString encoded = M::urlEncode(original);
        QCOMPARE(M::urlDecode(encoded), original);
    }

    void urlEncodeReservedAndSpace()
    {
        QCOMPARE(M::urlEncode("a b"), QStringLiteral("a%20b"));
        QCOMPARE(M::urlDecode("a%20b"), QStringLiteral("a b"));
        QCOMPARE(M::urlEncode("a=b&c"), QStringLiteral("a%3Db%26c"));
        QCOMPARE(M::urlDecode("a%3Db%26c"), QStringLiteral("a=b&c"));
    }
};

QTEST_GUILESS_MAIN(TestMimeTools)
#include "test_mime.moc"
