// 單元測試：檔案關聯（複刻 Notepad++ Preferences ▸ File Association）
//
// Windows 上會真的寫入 HKCU\Software\Classes，故測試一律用不可能與真實關聯衝突的
// 假副檔名，並在結束時還原，不動使用者既有的關聯。
#include <QtTest>

#include "platform/FileAssociation.h"

using macpad::platform::FileAssociation;

class TestFileAssociation : public QObject {
    Q_OBJECT
private slots:
    void progIdIsStable()
    {
        // ProgID 不可隨版本變動，否則升版後既有關聯會指向不存在的類別
        QCOMPARE(FileAssociation::progId(), QStringLiteral("macpadpp.Document"));
    }

    void commonExtensionsAreBareAndLowercase()
    {
        const QStringList exts = FileAssociation::commonExtensions();
        QVERIFY(!exts.isEmpty());
        for (const QString &e : exts) {
            QVERIFY2(!e.startsWith(QLatin1Char('.')), qPrintable(e));   // 不含點
            QCOMPARE(e, e.toLower());
            QVERIFY(!e.contains(QLatin1Char(' ')));
        }
        QVERIFY(exts.contains(QStringLiteral("txt")));
    }

    // 不支援的平台必須明確回報，而不是靜默假裝成功
    void unsupportedPlatformReportsReason()
    {
        if (FileAssociation::isSupported())
            QSKIP("本平台支援關聯，改由 roundTrip 測試涵蓋");
        QVERIFY(!FileAssociation::unsupportedReason().isEmpty());
        QString err;
        QVERIFY(!FileAssociation::associate(QStringLiteral("txt"), &err));
        QVERIFY(!err.isEmpty());
    }

    // 建立 → 查詢 → 解除 → 查詢，且解除後不留殘跡
    void associateRoundTrip()
    {
        if (!FileAssociation::isSupported())
            QSKIP("本平台不支援執行期變更關聯");

        // 用一個不可能存在於真實系統的副檔名，避免動到使用者的關聯
        const QString ext = QStringLiteral("macpadpptest");
        QVERIFY2(!FileAssociation::isAssociated(ext), "測試前置條件：該副檔名不應已關聯");

        QString err;
        QVERIFY2(FileAssociation::associate(ext, &err), qPrintable(err));
        QVERIFY(FileAssociation::isAssociated(ext));

        QVERIFY2(FileAssociation::unassociate(ext, &err), qPrintable(err));
        QVERIFY(!FileAssociation::isAssociated(ext));
    }

    // 前後空白與前置點都應被正規化，否則使用者輸入 ".TXT" 會建出錯誤的鍵
    void extensionNormalisation()
    {
        if (!FileAssociation::isSupported())
            QSKIP("本平台不支援執行期變更關聯");

        const QString ext = QStringLiteral("macpadpptest2");
        QString err;
        QVERIFY2(FileAssociation::associate(QStringLiteral("  .%1  ").arg(ext.toUpper()), &err),
                 qPrintable(err));
        QVERIFY(FileAssociation::isAssociated(ext));                       // 小寫、無點
        QVERIFY(FileAssociation::isAssociated(QStringLiteral(".%1").arg(ext)));  // 帶點也認得

        QVERIFY2(FileAssociation::unassociate(ext, &err), qPrintable(err));
        QVERIFY(!FileAssociation::isAssociated(ext));
    }

    // 解除本程式未建立的關聯必須是無操作且成功（冪等），不可去動別人的設定
    void unassociateForeignAssociationIsNoOp()
    {
        if (!FileAssociation::isSupported())
            QSKIP("本平台不支援執行期變更關聯");

        QString err;
        QVERIFY2(FileAssociation::unassociate(QStringLiteral("macpadppnotours"), &err),
                 qPrintable(err));
    }

    void emptyExtensionIsRejected()
    {
        QString err;
        QVERIFY(!FileAssociation::associate(QString(), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!FileAssociation::isAssociated(QString()));
    }
};

QTEST_MAIN(TestFileAssociation)
#include "test_fileassociation.moc"
