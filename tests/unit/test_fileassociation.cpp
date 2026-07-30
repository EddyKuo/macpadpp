// 單元測試：檔案關聯（複刻 Notepad++ Preferences ▸ File Association）
//
// Windows 上會真的寫入 HKCU\Software\Classes，故測試一律用不可能與真實關聯衝突的
// 假副檔名，並在結束時還原，不動使用者既有的關聯。
#include <QtTest>

#include <QSettings>
#include <QTabWidget>

#include "platform/FileAssociation.h"
#include "ui/PreferencesDialog.h"

using macpad::platform::FileAssociation;

class TestFileAssociation : public QObject {
    Q_OBJECT
private slots:
    void progIdIsStable()
    {
        // ProgID 不可隨版本變動，否則升版後既有關聯會指向不存在的類別
        QCOMPARE(FileAssociation::progId(), QStringLiteral("macpadpp.Document"));
    }

    // 關聯的成敗全繫於這個命令字串：Windows 會把被雙擊的檔案路徑代入其中的 %1。
    // 先前寫成 arg("\"%1\" \"%%1\"") ——QString::arg 沒有 %% 逸出且會取代所有同號標記，
    // 字面的 %%1 內那個 %1 也被換成執行檔路徑，產生 `"X" "%X"`：
    // 關聯看似成功、登錄檔也對，但雙擊開檔永遠收不到檔名。原本的測試只驗
    // .ext → ProgID 的對應，完全碰不到這條命令，所以會全綠地放行。
    void openCommandKeepsThePercentOneToken()
    {
        const QString exe = QStringLiteral("C:\\Program Files\\macpad++\\macpad++.exe");
        const QString cmd = FileAssociation::openCommandFor(exe);

        QCOMPARE(cmd, QStringLiteral("\"C:\\Program Files\\macpad++\\macpad++.exe\" \"%1\""));
        QVERIFY2(cmd.contains(QStringLiteral("\"%1\"")), qPrintable(cmd));
        // 執行檔路徑只能出現一次；出現兩次即代表 %1 被吃掉了
        QCOMPARE(cmd.count(exe), 1);
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

    // 直接檢查寫進登錄檔的成果，而不只是「我們自己的 API 說成功了」。
    // 兩項都是實際會咬人的：命令字串少了 %1 就收不到檔名；解除關聯若刪整個子樹
    // 會連別的軟體放在 .ext 底下的資料一起毀掉。
    void registryEffectsAreCorrect()
    {
        if (!FileAssociation::isSupported())
            QSKIP("本平台不支援執行期變更關聯");

        const QString ext = QStringLiteral("macpadppreg");
        const QString root = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes");
        const QString extKey = QStringLiteral(".%1").arg(ext);

        {   // 前置：模擬「別的軟體已在該副檔名底下放了子鍵」
            QSettings seed(root, QSettings::NativeFormat);
            seed.setValue(QStringLiteral("%1/OpenWithProgids/SomeOtherApp").arg(extKey),
                          QString());
            seed.sync();
        }

        QString err;
        QVERIFY2(FileAssociation::associate(ext, &err), qPrintable(err));

        {   // 1) shell\open\command 必須保留字面的 "%1"
            QSettings check(root, QSettings::NativeFormat);
            const QString cmd =
                check.value(QStringLiteral("%1/shell/open/command/.")
                                .arg(FileAssociation::progId())).toString();
            QVERIFY2(cmd.contains(QStringLiteral("\"%1\"")), qPrintable(cmd));
        }

        QVERIFY2(FileAssociation::unassociate(ext, &err), qPrintable(err));

        {   // 2) 別人的子鍵必須原封不動
            QSettings check(root, QSettings::NativeFormat);
            check.beginGroup(extKey);
            const QStringList groups = check.childGroups();
            check.endGroup();
            QVERIFY2(groups.contains(QStringLiteral("OpenWithProgids")),
                     "unassociate 刪掉了不屬於本程式的登錄檔子鍵");
        }

        {   // 收尾：清掉本測試自己種下的資料
            QSettings cleanup(root, QSettings::NativeFormat);
            cleanup.remove(extKey);
            cleanup.sync();
        }
    }

    // 偏好設定確實有這個分頁——模組寫好卻沒接進 UI 是這類功能最常見的漏接
    void preferencesDialogHasTheTab()
    {
        macpad::persistence::Settings s;
        macpad::ui::PreferencesDialog dlg(s);
        auto *tabs = dlg.findChild<QTabWidget *>();
        QVERIFY(tabs);
        bool found = false;
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QLatin1String("File Association"))
                found = true;
        QVERIFY2(found, "Preferences 缺少 File Association 分頁");
    }
};

QTEST_MAIN(TestFileAssociation)
#include "test_fileassociation.moc"
