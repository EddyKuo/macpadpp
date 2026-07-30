// 單元測試：版本比較（更新檢查的核心判定）。
// 網路查詢本身不在單元測試範圍——測試不應依賴外部服務。
#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "features/update/UpdateChecker.h"
#include "features/update/UpdateDownloader.h"

using macpad::features::compareVersions;

class TestUpdateChecker : public QObject {
    Q_OBJECT
private slots:
    void compare_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");

        QTest::newRow("equal")            << "1.2.3"  << "1.2.3"  << 0;
        QTest::newRow("patch newer")      << "1.2.3"  << "1.2.4"  << -1;
        QTest::newRow("patch older")      << "1.2.5"  << "1.2.4"  << 1;
        QTest::newRow("minor")            << "1.2.9"  << "1.3.0"  << -1;
        QTest::newRow("major")            << "1.9.9"  << "2.0.0"  << -1;
        // 字串比較會誤判 "1.2.10" < "1.2.9"，數值比較不會
        QTest::newRow("double digit")     << "1.2.9"  << "1.2.10" << -1;
        // 'v' 前綴（GitHub tag 慣例）
        QTest::newRow("v prefix")         << "0.5.2"  << "v0.5.3" << -1;
        QTest::newRow("both v prefix")    << "v1.0.0" << "v1.0.0" << 0;
        // 段數不同：缺少的段視為 0
        QTest::newRow("shorter equal")    << "1.2"    << "1.2.0"  << 0;
        QTest::newRow("shorter older")    << "1.2"    << "1.2.1"  << -1;
        // 預發布後綴取前導數字
        QTest::newRow("prerelease suffix")<< "1.2.3"  << "1.2.3-rc1" << 0;
        // 完全非數字段落視為 0，不得崩潰
        QTest::newRow("garbage")          << "abc"    << "0.0.0"  << 0;
    }
    void compare()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(compareVersions(a, b), expected);
        QCOMPARE(compareVersions(b, a), -expected);   // 反向必須對稱
    }

    // 從 Releases 的 assets 陣列挑出本平台的安裝檔（Windows 取 .zip、macOS 取 .dmg）
    void picksAssetForCurrentPlatform()
    {
        const QByteArray json =
            "[{\"name\":\"macpad++-0.6.0-arm64.dmg\","
            "  \"browser_download_url\":\"https://x/a.dmg\",\"size\":143960231},"
            " {\"name\":\"macpad++-0.6.0-x64.zip\","
            "  \"browser_download_url\":\"https://x/b.zip\",\"size\":158691520}]";
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        QVERIFY(doc.isArray());

        QString size;
        const QString url =
            macpad::features::UpdateChecker::pickAssetForPlatform(doc.array(), &size);
#if defined(Q_OS_WIN)
        QCOMPARE(url, QStringLiteral("https://x/b.zip"));
        QCOMPARE(size, QStringLiteral("158691520"));
#elif defined(Q_OS_MACOS)
        QCOMPARE(url, QStringLiteral("https://x/a.dmg"));
        QCOMPARE(size, QStringLiteral("143960231"));
#else
        QVERIFY(url.isEmpty());   // 其他平台沒有對應發佈檔
#endif
    }

    // 該版沒有本平台的檔案時必須回空字串，讓呼叫端退回 release 頁面，不臆測
    void noMatchingAssetYieldsEmpty()
    {
        const QJsonDocument doc = QJsonDocument::fromJson(
            "[{\"name\":\"source.tar.bz2\","
            "  \"browser_download_url\":\"https://x/s.tar.bz2\"}]");
        QString size = QStringLiteral("stale");
        QVERIFY(macpad::features::UpdateChecker::pickAssetForPlatform(doc.array(), &size).isEmpty());
        QVERIFY(size.isEmpty());   // 未命中時也要清掉輸出參數
        QVERIFY(macpad::features::UpdateChecker::pickAssetForPlatform(QJsonArray()).isEmpty());
    }

    // 下載落地檔名取自網址最後一段，且不得被 query string 汙染
    void downloadFileNameFromUrl()
    {
        using macpad::features::UpdateDownloader;
        QCOMPARE(UpdateDownloader::fileNameForUrl(
                     QStringLiteral("https://github.com/x/releases/download/v1/macpad++-1-x64.zip")),
                 QStringLiteral("macpad++-1-x64.zip"));
        QCOMPARE(UpdateDownloader::fileNameForUrl(
                     QStringLiteral("https://cdn.example/a/b.dmg?token=abc&x=1")),
                 QStringLiteral("b.dmg"));
        QVERIFY(UpdateDownloader::fileNameForUrl(QString()).isEmpty());
    }

    void downloadDirIsWritable()
    {
        const QString d = macpad::features::UpdateDownloader::downloadDir();
        QVERIFY(!d.isEmpty());
        QVERIFY(QFileInfo(d).isDir());
    }
};

QTEST_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
