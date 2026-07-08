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

    // 完整欄位（多關鍵字群組、前綴模式、運算子、分隔符、摺疊符、樣式）
    // 經 save()/loadAll() JSON 持久化後應完整往返（FR-059）
    void saveLoadRoundTripFullDefinition()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "FullRoundTrip";
        d.extensions = {"frt"};
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        d.keywordGroups[0] = {"foo", "bar"};
        d.keywordGroups[2] = {"baz"};
        d.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
        d.keywordGroupPrefixMode[0] = true;
        d.keywordGroupPrefixMode[2] = false;
        d.operators = {"+", "-"};
        UdlDelimiter del;
        del.open = "\"";
        del.escape = "\\";
        del.close = "\"";
        d.delimiters.push_back(del);
        d.folderTokens.open = "{";
        d.folderTokens.middle = "";
        d.folderTokens.close = "}";
        d.lineComment = "//";
        d.blockCommentStart = "/*";
        d.blockCommentEnd = "*/";
        d.caseSensitive = false;
        UdlStyle st;
        st.fg = "#ff0000";
        st.bg = "#00ff00";
        st.bold = true;
        st.italic = true;
        st.underline = false;
        d.styles.insert(3, st);

        QVERIFY(mgr.save(d));

        UdlManager mgr2;
        mgr2.loadAll();
        const UdlDefinition *found = mgr2.findForExtension("frt");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("FullRoundTrip"));
        QVERIFY(found->keywordGroup(0).contains("foo"));
        QVERIFY(found->keywordGroup(0).contains("bar"));
        QVERIFY(found->keywordGroup(2).contains("baz"));
        QVERIFY(found->keywordGroupPrefix(0));
        QVERIFY(!found->keywordGroupPrefix(2));
        QVERIFY(found->operators.contains("+"));
        QVERIFY(found->operators.contains("-"));
        QCOMPARE(found->delimiters.size(), 1);
        QCOMPARE(found->delimiters.at(0).open, QStringLiteral("\""));
        QCOMPARE(found->delimiters.at(0).escape, QStringLiteral("\\"));
        QCOMPARE(found->delimiters.at(0).close, QStringLiteral("\""));
        QCOMPARE(found->folderTokens.open, QStringLiteral("{"));
        QCOMPARE(found->folderTokens.close, QStringLiteral("}"));
        QVERIFY(found->folderTokens.middle.isEmpty());
        QCOMPARE(found->lineComment, QStringLiteral("//"));
        QCOMPARE(found->blockCommentStart, QStringLiteral("/*"));
        QCOMPARE(found->blockCommentEnd, QStringLiteral("*/"));
        QCOMPARE(found->caseSensitive, false);
        QVERIFY(found->styles.contains(3));
        QCOMPARE(found->styles.value(3).fg, QStringLiteral("#ff0000"));
        QCOMPARE(found->styles.value(3).bg, QStringLiteral("#00ff00"));
        QVERIFY(found->styles.value(3).bold);
        QVERIFY(found->styles.value(3).italic);
        QVERIFY(!found->styles.value(3).underline);
    }

    // rename() 應移除舊名、加入新名，且保留原始內容；並持久化至磁碟
    void renameMovesDefinitionPreservingContent()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "OldName";
        d.extensions = {"onm"};
        d.keywords = {"kw1"};
        d.lineComment = "#";
        QVERIFY(mgr.save(d));

        QVERIFY(mgr.rename("OldName", "NewName"));
        QCOMPARE(mgr.definitions().size(), 1);

        bool sawOld = false;
        for (const auto &def : mgr.definitions())
            if (def.name == QStringLiteral("OldName")) sawOld = true;
        QVERIFY(!sawOld);

        const UdlDefinition *found = mgr.findForExtension("onm");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("NewName"));
        QVERIFY(found->keywords.contains("kw1"));
        QCOMPARE(found->lineComment, QStringLiteral("#"));

        // 持久化確認：舊檔已刪除、新檔存在（另一個 manager 重新載入）
        UdlManager mgr2;
        mgr2.loadAll();
        const UdlDefinition *found2 = mgr2.findForExtension("onm");
        QVERIFY(found2 != nullptr);
        QCOMPARE(found2->name, QStringLiteral("NewName"));
    }

    // rename() 對非法輸入（新舊同名/新名為空白/舊名不存在）應回傳 false 且不變動狀態
    void renameFailsForInvalidInputs()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "SameNameLang";
        d.extensions = {"sml"};
        QVERIFY(mgr.save(d));

        QVERIFY(!mgr.rename("SameNameLang", "SameNameLang"));  // 新舊同名
        QVERIFY(!mgr.rename("SameNameLang", "   "));           // 新名 trimmed 為空
        QVERIFY(!mgr.rename("NoSuchLang", "Whatever"));        // 舊名不存在
        QCOMPARE(mgr.definitions().size(), 1);
    }

    // remove() 應自記憶體與磁碟移除該筆定義；重複移除/移除不存在項目回傳 false
    void removeDeletesDefinitionAndFile()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "RemoveMe";
        d.extensions = {"rem"};
        QVERIFY(mgr.save(d));
        QCOMPARE(mgr.definitions().size(), 1);

        QVERIFY(mgr.remove("RemoveMe"));
        QCOMPARE(mgr.definitions().size(), 0);
        QVERIFY(mgr.findForExtension("rem") == nullptr);

        // 持久化移除：另一個 manager loadAll 後也找不到
        UdlManager mgr2;
        mgr2.loadAll();
        QVERIFY(mgr2.findForExtension("rem") == nullptr);

        // 重複移除 / 移除不存在項目回傳 false
        QVERIFY(!mgr.remove("RemoveMe"));
        QVERIFY(!mgr.remove("NeverExisted"));
    }

    // exportToFile()/exportToXml() 對記憶體中不存在的名稱應回傳 false
    void exportFailsForNonexistentName()
    {
        UdlManager mgr;
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QVERIFY(!mgr.exportToFile("NoSuchLang", tmpDir.filePath("out.json")));
        QVERIFY(!mgr.exportToXml("NoSuchLang", tmpDir.filePath("out.xml")));
    }

    // exportToXml()/importFromXml() 往返：匯出一筆完整定義為 Notepad++ userDefineLang.xml
    // 後，以另一個 manager 匯入，關鍵欄位（副檔名/關鍵字群組/註解/運算子/分隔符/
    // 摺疊符/大小寫/樣式）應保留
    void exportImportXmlRoundTrip()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        UdlManager mgr;
        UdlDefinition d;
        d.name = "XmlRoundTrip";
        d.extensions = {"xrt", "xrt2"};
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        d.keywordGroups[0] = {"foo", "bar"};
        d.keywordGroups[1] = {"baz"};
        d.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
        d.keywordGroupPrefixMode[0] = true;
        d.operators = {"+", "-"};
        UdlDelimiter del;
        del.open = "\"";
        del.escape = "\\";
        del.close = "\"";
        d.delimiters.push_back(del);
        d.folderTokens.open = "{";
        d.folderTokens.middle = "";
        d.folderTokens.close = "}";
        d.lineComment = "//";
        d.blockCommentStart = "/*";
        d.blockCommentEnd = "*/";
        d.caseSensitive = false;
        UdlStyle st;
        st.fg = "#ff0000";
        st.bg = "#00ff00";
        st.bold = true;
        st.italic = false;
        st.underline = true;
        d.styles.insert(0, st);

        QVERIFY(mgr.save(d));

        const QString xmlPath = tmpDir.filePath("exported.xml");
        QVERIFY(mgr.exportToXml("XmlRoundTrip", xmlPath));
        QVERIFY(QFile::exists(xmlPath));

        UdlManager mgr2;
        QVERIFY(mgr2.importFromXml(xmlPath));
        const UdlDefinition *found = mgr2.findForExtension("xrt");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("XmlRoundTrip"));
        QVERIFY(found->extensions.contains("xrt"));
        QVERIFY(found->extensions.contains("xrt2"));
        QVERIFY(found->keywordGroup(0).contains("foo"));
        QVERIFY(found->keywordGroup(0).contains("bar"));
        QVERIFY(found->keywordGroup(1).contains("baz"));
        QVERIFY(found->operators.contains("+"));
        QVERIFY(found->operators.contains("-"));
        QCOMPARE(found->lineComment, QStringLiteral("//"));
        QCOMPARE(found->blockCommentStart, QStringLiteral("/*"));
        QCOMPARE(found->blockCommentEnd, QStringLiteral("*/"));
        QCOMPARE(found->folderTokens.open, QStringLiteral("{"));
        QCOMPARE(found->folderTokens.close, QStringLiteral("}"));
        QCOMPARE(found->caseSensitive, false);
        QVERIFY(!found->delimiters.isEmpty());
        QCOMPARE(found->delimiters.at(0).open, QStringLiteral("\""));
        QCOMPARE(found->delimiters.at(0).escape, QStringLiteral("\\"));
        QCOMPARE(found->delimiters.at(0).close, QStringLiteral("\""));
        QVERIFY(found->styles.contains(0));
        QCOMPARE(found->styles.value(0).fg, QStringLiteral("#ff0000"));
        QVERIFY(found->styles.value(0).bold);
        QVERIFY(found->styles.value(0).underline);
        QVERIFY(!found->styles.value(0).italic);

        // <Prefix> 節點前綴模式經由 XML 匯出/匯入正確往返（修正後與 JSON save/load 一致）
        QVERIFY(found->keywordGroupPrefix(0));

        // 匯入後應已持久化（manager 內部呼叫 save()）
        UdlManager mgr3;
        mgr3.loadAll();
        QVERIFY(mgr3.findForExtension("xrt") != nullptr);
    }

    // importFromXml() 解析一份手寫的 Notepad++ userDefineLang.xml 片段
    // （模擬 NPP「Define your language」匯出檔格式）
    void importFromXmlParsesNppStyleSnippet()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString xmlPath = tmpDir.filePath("userDefineLang.xml");

        const QString xmlContent = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
            "<NotepadPlus>\n"
            "    <UserLang name=\"MyNppLang\" ext=\"mnl mnl2\" udlVersion=\"2.1\">\n"
            "        <Settings>\n"
            "            <Global caseIgnored=\"no\" allowFoldOfComments=\"no\" foldCompact=\"no\" />\n"
            "            <Prefix words1=\"no\" words2=\"no\" words3=\"no\" words4=\"no\""
            "                    words5=\"no\" words6=\"no\" words7=\"no\" words8=\"no\" />\n"
            "        </Settings>\n"
            "        <KeywordLists>\n"
            "            <Keywords name=\"Comments\">00//01/*02*/</Keywords>\n"
            "            <Keywords name=\"Keywords1\">alpha beta gamma</Keywords>\n"
            "            <Keywords name=\"Keywords2\">delta</Keywords>\n"
            "            <Keywords name=\"Keywords3\"></Keywords>\n"
            "            <Keywords name=\"Keywords4\"></Keywords>\n"
            "            <Keywords name=\"Keywords5\"></Keywords>\n"
            "            <Keywords name=\"Keywords6\"></Keywords>\n"
            "            <Keywords name=\"Keywords7\"></Keywords>\n"
            "            <Keywords name=\"Keywords8\"></Keywords>\n"
            "            <Keywords name=\"Operators1\">+ - * /</Keywords>\n"
            "            <Keywords name=\"Operators2\"></Keywords>\n"
            "            <Keywords name=\"Folders in code1, open\">{</Keywords>\n"
            "            <Keywords name=\"Folders in code1, middle\"></Keywords>\n"
            "            <Keywords name=\"Folders in code1, close\">}</Keywords>\n"
            "            <Keywords name=\"Delimiters\">00\"01\\02\"</Keywords>\n"
            "        </KeywordLists>\n"
            "        <Styles>\n"
            "            <WordsStyle name=\"DEFAULT\" styleID=\"0\" fgColor=\"000000\""
            "                        bgColor=\"FFFFFF\" fontStyle=\"0\" />\n"
            "            <WordsStyle name=\"STYLE1\" styleID=\"1\" fgColor=\"FF0000\""
            "                        bgColor=\"FFFFFF\" fontStyle=\"1\" />\n"
            "        </Styles>\n"
            "    </UserLang>\n"
            "</NotepadPlus>\n");

        QFile file(xmlPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(xmlContent.toUtf8());
        file.close();

        UdlManager mgr;
        QVERIFY(mgr.importFromXml(xmlPath));

        const UdlDefinition *found = mgr.findForExtension("mnl");
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("MyNppLang"));
        QVERIFY(found->extensions.contains("mnl"));
        QVERIFY(found->extensions.contains("mnl2"));
        QVERIFY(found->keywordGroup(0).contains("alpha"));
        QVERIFY(found->keywordGroup(0).contains("beta"));
        QVERIFY(found->keywordGroup(0).contains("gamma"));
        QVERIFY(found->keywordGroup(1).contains("delta"));
        QCOMPARE(found->lineComment, QStringLiteral("//"));
        QCOMPARE(found->blockCommentStart, QStringLiteral("/*"));
        QCOMPARE(found->blockCommentEnd, QStringLiteral("*/"));
        QVERIFY(found->operators.contains("+"));
        QVERIFY(found->operators.contains("/"));
        QCOMPARE(found->folderTokens.open, QStringLiteral("{"));
        QCOMPARE(found->folderTokens.close, QStringLiteral("}"));
        QCOMPARE(found->caseSensitive, true);  // caseIgnored="no" -> caseSensitive true
        QVERIFY(!found->delimiters.isEmpty());
        QCOMPARE(found->delimiters.at(0).open, QStringLiteral("\""));
        QCOMPARE(found->delimiters.at(0).escape, QStringLiteral("\\"));
        QCOMPARE(found->delimiters.at(0).close, QStringLiteral("\""));
        QVERIFY(found->styles.contains(0));
        QVERIFY(found->styles.contains(1));
        QVERIFY(found->styles.value(1).bold);

        // 匯入後已持久化：另一個 manager loadAll 後也能找到
        UdlManager mgr2;
        mgr2.loadAll();
        QVERIFY(mgr2.findForExtension("mnl") != nullptr);
    }

    // importFromXml() 對無效路徑或找不到 UserLang 節點的 XML 應回傳 false
    void importFromXmlFailsGracefully()
    {
        UdlManager mgr;
        QVERIFY(!mgr.importFromXml(QStringLiteral("/nonexistent/path/does_not_exist.xml")));

        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        const QString badPath = tmpDir.filePath("not_udl.xml");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Text));
        bad.write("<NotepadPlus></NotepadPlus>");  // 無 UserLang 節點
        bad.close();
        QVERIFY(!mgr.importFromXml(badPath));
    }
};

QTEST_GUILESS_MAIN(TestUdlManager)
#include "test_udlmanager.moc"
