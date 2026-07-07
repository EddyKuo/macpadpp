// 單元測試:確認 i18n .qm 內嵌資源可載入,且翻譯正確解析(MainWindow context)。
#include <QtTest>
#include <QCoreApplication>
#include <QTranslator>

class TestI18n : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(i18n);   // 靜態庫資源需顯式初始化
    }

    void qmFilesLoad_data()
    {
        QTest::addColumn<QString>("lang");
        QTest::newRow("zh_TW") << QStringLiteral("zh_TW");
        QTest::newRow("zh_CN") << QStringLiteral("zh_CN");
        QTest::newRow("ja")    << QStringLiteral("ja");
        QTest::newRow("en")    << QStringLiteral("en");
    }
    void qmFilesLoad()
    {
        QFETCH(QString, lang);
        QTranslator t;
        QVERIFY2(t.load(QStringLiteral(":/i18n/macpad_%1.qm").arg(lang)),
                 "內嵌 .qm 載入失敗(資源未註冊?)");
    }

    void japaneseMenus()
    {
        QTranslator t;
        QVERIFY(t.load(QStringLiteral(":/i18n/macpad_ja.qm")));
        qApp->installTranslator(&t);
        QCOMPARE(QCoreApplication::translate("MainWindow", "&File"),
                 QString::fromUtf8("ファイル"));
        QCOMPARE(QCoreApplication::translate("MainWindow", "Save"),
                 QString::fromUtf8("保存"));
        qApp->removeTranslator(&t);
    }

    void traditionalChineseMenus()
    {
        QTranslator t;
        QVERIFY(t.load(QStringLiteral(":/i18n/macpad_zh_TW.qm")));
        qApp->installTranslator(&t);
        QCOMPARE(QCoreApplication::translate("MainWindow", "&View"),
                 QString::fromUtf8("檢視"));
        QCOMPARE(QCoreApplication::translate("MainWindow", "Fold All"),
                 QString::fromUtf8("全部折疊"));
        QCOMPARE(QCoreApplication::translate("MainWindow", "Move to Recycle Bin"),
                 QString::fromUtf8("移到垃圾桶"));
        qApp->removeTranslator(&t);
    }

    void dialogsAndExtensionsTranslated()
    {
        QTranslator t;
        QVERIFY(t.load(QStringLiteral(":/i18n/macpad_zh_TW.qm")));
        qApp->installTranslator(&t);
        // 對話框(各自 context)
        QCOMPARE(QCoreApplication::translate("macpad::ui::PreferencesDialog", "Preferences"),
                 QString::fromUtf8("偏好設定"));
        QCOMPARE(QCoreApplication::translate("macpad::features::FindReplaceDialog", "Replace All"),
                 QString::fromUtf8("全部取代"));
        QCOMPARE(QCoreApplication::translate("macpad::ui::StyleConfiguratorDialog", "Bold"),
                 QString::fromUtf8("粗體"));
        // 外掛選單(Extensions context)
        QCOMPARE(QCoreApplication::translate("Extensions", "Word Count"),
                 QString::fromUtf8("字數統計"));
        QCOMPARE(QCoreApplication::translate("Extensions", "Markdown Preview"),
                 QString::fromUtf8("Markdown 預覽"));
        qApp->removeTranslator(&t);
    }

    void untranslatedFallsBackToSource()
    {
        QTranslator t;
        QVERIFY(t.load(QStringLiteral(":/i18n/macpad_ja.qm")));
        qApp->installTranslator(&t);
        // 未提供翻譯的字串應回退為原文
        QCOMPARE(QCoreApplication::translate("MainWindow", "____not_translated____"),
                 QString::fromUtf8("____not_translated____"));
        qApp->removeTranslator(&t);
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "test_i18n.moc"
