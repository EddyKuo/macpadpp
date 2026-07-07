// 單元測試：UdlManager — 儲存/載入/查找/匯入（FR-032, DR-005）
#include <QtTest>
#include <QStandardPaths>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"

using namespace macpad::features;

class TestUdlManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // save() 新增一筆定義，並可透過 findForExtension 找到
    void saveAddsNewDefinition()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "AlphaLang";
        d.extensions = {"alp"};
        d.keywords = {"begin", "end"};
        QVERIFY(mgr.save(d));
        QCOMPARE(mgr.definitions().size(), 1);
        const UdlDefinition *found = mgr.findForExtension("alp");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("AlphaLang"));
    }

    // save() 同名時應在記憶體中「取代」而非新增一筆（REPLACE 語意）
    void saveReplacesExistingByName()
    {
        UdlManager mgr;
        UdlDefinition d1;
        d1.name = "BetaLang";
        d1.extensions = {"bet"};
        d1.keywords = {"old"};
        QVERIFY(mgr.save(d1));
        QCOMPARE(mgr.definitions().size(), 1);

        UdlDefinition d2;
        d2.name = "BetaLang";           // 同名
        d2.extensions = {"bet2"};       // 副檔名變更
        d2.keywords = {"newkw"};
        QVERIFY(mgr.save(d2));

        // 仍只有一筆（取代而非新增）
        QCOMPARE(mgr.definitions().size(), 1);
        // 舊副檔名不再對應
        QVERIFY(mgr.findForExtension("bet") == nullptr);
        // 新副檔名對應到更新後內容
        const UdlDefinition *found = mgr.findForExtension("bet2");
        QVERIFY(found != nullptr);
        QVERIFY(found->keywords.contains("newkw"));
    }

    // findForExtension 應忽略大小寫
    void findForExtensionIsCaseInsensitive()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "GammaLang";
        d.extensions = {"gam"};
        QVERIFY(mgr.save(d));

        QVERIFY(mgr.findForExtension("GAM") != nullptr);
        QVERIFY(mgr.findForExtension("Gam") != nullptr);
        QCOMPARE(mgr.findForExtension("GAM")->name, QStringLiteral("GammaLang"));
        QVERIFY(mgr.findForExtension("unknown") == nullptr);
    }

    // save() 寫入磁碟後，另一個 UdlManager 實例 loadAll() 應能讀回
    void loadAllReadsBackPersistedDefinitions()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "DeltaLang";
        d.extensions = {"del"};
        d.keywords = {"kw1", "kw2"};
        d.lineComment = "//";
        QVERIFY(mgr.save(d));

        UdlManager mgr2;
        mgr2.loadAll();
        const UdlDefinition *found = mgr2.findForExtension("del");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("DeltaLang"));
        QVERIFY(found->keywords.contains("kw1"));
        QCOMPARE(found->lineComment, QStringLiteral("//"));
    }

    // importFromFile() 應解析外部 JSON 檔並存入 manager（含持久化）
    void importFromFileParsesAndSaves()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        UdlDefinition d;
        d.name = "ImportedLang";
        d.extensions = {"imp"};
        d.keywords = {"import_kw"};
        d.lineComment = "#";

        const QString path = tmpDir.filePath("external_udl.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(d.toJson()).toJson());
        file.close();

        UdlManager mgr;
        QVERIFY(mgr.importFromFile(path));
        const UdlDefinition *found = mgr.findForExtension("imp");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("ImportedLang"));
        QVERIFY(found->keywords.contains("import_kw"));

        // 確認已持久化：新的 manager loadAll 後也能找到
        UdlManager mgr2;
        mgr2.loadAll();
        QVERIFY(mgr2.findForExtension("imp") != nullptr);
    }

    // importFromFile() 對不存在的路徑或非法 JSON 應回傳 false
    void importFromFileFailsGracefully()
    {
        UdlManager mgr;
        QVERIFY(!mgr.importFromFile(QStringLiteral("/nonexistent/path/does_not_exist.json")));

        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString badPath = tmpDir.filePath("bad.json");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not valid json ");
        bad.close();
        QVERIFY(!mgr.importFromFile(badPath));

        // 有效 JSON 但缺少 name（isValid() == false）
        QTemporaryDir tmpDir2;
        QVERIFY(tmpDir2.isValid());
        const QString noNamePath = tmpDir2.filePath("noname.json");
        QFile noName(noNamePath);
        QVERIFY(noName.open(QIODevice::WriteOnly));
        QJsonObject o;
        o.insert("extensions", QJsonArray{"xx"});
        noName.write(QJsonDocument(o).toJson());
        noName.close();
        QVERIFY(!mgr.importFromFile(noNamePath));
    }

    // 非 ASCII（CJK）UDL 名稱應能正確儲存並取回，不與 ASCII 名稱衝突
    void nonAsciiCjkNamePreserved()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = QString::fromUtf8("中文語言");
        d.extensions = {"zhw"};
        d.keywords = {QString::fromUtf8("關鍵字")};
        QVERIFY(mgr.save(d));

        const UdlDefinition *found = mgr.findForExtension("zhw");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QString::fromUtf8("中文語言"));
        QVERIFY(found->keywords.contains(QString::fromUtf8("關鍵字")));

        // 持久化後另一個 manager 也能正確讀回（驗證檔名 Unicode \w 未被塌縮成亂碼）
        UdlManager mgr2;
        mgr2.loadAll();
        const UdlDefinition *found2 = mgr2.findForExtension("zhw");
        QVERIFY(found2 != nullptr);
        QCOMPARE(found2->name, QString::fromUtf8("中文語言"));
    }

    // 兩個名稱僅在「非法標點」上不同（皆會被檔名淨化為底線），
    // 應仍被視為相異定義（分別儲存/查找皆正確），而非互相覆蓋
    void namesDifferingOnlyByIllegalPunctuationAreDistinct()
    {
        UdlManager mgr;

        UdlDefinition d1;
        d1.name = QStringLiteral("Lang/Test");   // '/' 為非法字元 -> 淨化為 "Lang_Test"
        d1.extensions = {"lpt1"};
        d1.keywords = {"one"};
        QVERIFY(mgr.save(d1));

        UdlDefinition d2;
        d2.name = QStringLiteral("Lang:Test");   // ':' 亦為非法字元 -> 淨化後檔名相同
        d2.extensions = {"lpt2"};
        d2.keywords = {"two"};
        QVERIFY(mgr.save(d2));

        // 記憶體中應保留兩筆相異定義（依 name 精確比對，不因檔名淨化而合併）
        QCOMPARE(mgr.definitions().size(), 2);

        const UdlDefinition *found1 = mgr.findForExtension("lpt1");
        const UdlDefinition *found2 = mgr.findForExtension("lpt2");
        QVERIFY(found1 != nullptr);
        QVERIFY(found2 != nullptr);
        QCOMPARE(found1->name, QStringLiteral("Lang/Test"));
        QCOMPARE(found2->name, QStringLiteral("Lang:Test"));
        QVERIFY(found1->keywords.contains("one"));
        QVERIFY(found2->keywords.contains("two"));
    }

    // definitions() 應反映目前記憶體中的所有筆數與內容
    void definitionsAccessorReflectsState()
    {
        UdlManager mgr;
        QCOMPARE(mgr.definitions().size(), 0);

        UdlDefinition d1;
        d1.name = "Epsilon";
        d1.extensions = {"eps"};
        QVERIFY(mgr.save(d1));
        QCOMPARE(mgr.definitions().size(), 1);

        UdlDefinition d2;
        d2.name = "Zeta";
        d2.extensions = {"zet"};
        QVERIFY(mgr.save(d2));
        QCOMPARE(mgr.definitions().size(), 2);

        bool sawEpsilon = false, sawZeta = false;
        for (const auto &d : mgr.definitions()) {
            if (d.name == QStringLiteral("Epsilon")) sawEpsilon = true;
            if (d.name == QStringLiteral("Zeta")) sawZeta = true;
        }
        QVERIFY(sawEpsilon);
        QVERIFY(sawZeta);
    }
};

QTEST_GUILESS_MAIN(TestUdlManager)
#include "test_udlmanager.moc"
