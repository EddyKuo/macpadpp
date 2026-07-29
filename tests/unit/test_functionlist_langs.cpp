// 單元測試：Function List 語言規則擴充（對齊 Notepad++ functionList.xml 覆蓋範圍）
// 驗證副檔名對照與各語言的符號擷取；規則本身為純設定，解析器不含語言分支。
#include <QtTest>

#include "features/functionlist/FunctionListConfig.h"
#include "features/functionlist/FunctionListParser.h"

using namespace macpad::features;

namespace {
QStringList namesOf(const QString &code, const QString &lang)
{
    QStringList out;
    for (const Symbol &s : FunctionListParser::parse(code, lang))
        out << s.name;
    return out;
}
}  // namespace

class TestFunctionListLangs : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // 副檔名 → 語言鍵
    void suffixMapping_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("lang");

        QTest::newRow("go")     << "go"    << "go";
        QTest::newRow("rs")     << "rs"    << "rust";
        QTest::newRow("swift")  << "swift" << "swift";
        QTest::newRow("kt")     << "kt"    << "kotlin";
        QTest::newRow("cs")     << "cs"    << "csharp";
        QTest::newRow("php")    << "php"   << "php";
        QTest::newRow("rb")     << "rb"    << "ruby";
        QTest::newRow("ps1")    << "ps1"   << "powershell";
        QTest::newRow("lua")    << "lua"   << "lua";
        QTest::newRow("sh")     << "sh"    << "bash";
        QTest::newRow("md")     << "md"    << "markdown";
        QTest::newRow("tsx")    << "tsx"   << "javascript";
        QTest::newRow("unknown")<< "zzz"   << "";
    }
    void suffixMapping()
    {
        QFETCH(QString, suffix);
        QFETCH(QString, lang);
        QCOMPARE(FunctionListParser::languageForSuffix(suffix), lang);
    }

    // 每個新增語言鍵都有有效規則
    void newLanguagesHaveRules()
    {
        for (const char *lang : {"csharp", "php", "perl", "ruby", "go", "rust", "swift", "kotlin",
                                 "scala", "dart", "groovy", "powershell", "r", "lua", "bash",
                                 "batch", "sql", "css", "vb", "pascal", "fortran", "haskell",
                                 "matlab", "tcl", "nim", "d", "tex", "asm", "properties", "toml",
                                 "markdown", "autoit", "nsis", "inno", "verilog", "vhdl", "erlang",
                                 "coffeescript", "sas", "cmake", "makefile"})
            QVERIFY2(FunctionListConfig::builtinRule(QString::fromLatin1(lang)).isValid(), lang);
    }

    void goFunctions()
    {
        const QString code = QStringLiteral(
            "package main\n"
            "type Server struct {\n"
            "\tAddr string\n"
            "}\n"
            "func main() {\n"
            "}\n"
            "func (s *Server) Listen(addr string) error {\n"
            "\treturn nil\n"
            "}\n");
        const QStringList n = namesOf(code, QStringLiteral("go"));
        QVERIFY(n.contains(QStringLiteral("Server")));
        QVERIFY(n.contains(QStringLiteral("main")));
        QVERIFY(n.contains(QStringLiteral("Listen")));
    }

    void rustFunctions()
    {
        const QString code = QStringLiteral(
            "pub struct Config {}\n"
            "impl Config {\n"
            "    pub fn new() -> Self { Config {} }\n"
            "    async fn load(&self) {}\n"
            "}\n"
            "fn main() {}\n");
        const QStringList n = namesOf(code, QStringLiteral("rust"));
        QVERIFY(n.contains(QStringLiteral("Config")));
        QVERIFY(n.contains(QStringLiteral("new")));
        QVERIFY(n.contains(QStringLiteral("load")));
        QVERIFY(n.contains(QStringLiteral("main")));

        // impl 區塊內的方法歸屬於 Config（trackScope）
        for (const Symbol &s : FunctionListParser::parse(code, QStringLiteral("rust")))
            if (s.name == QStringLiteral("new"))
                QCOMPARE(s.scope, QStringLiteral("Config"));
    }

    void swiftAndKotlin()
    {
        const QString swift = QStringLiteral(
            "class Downloader {\n"
            "    func start() {}\n"
            "    private static func shared() {}\n"
            "}\n");
        const QStringList s = namesOf(swift, QStringLiteral("swift"));
        QVERIFY(s.contains(QStringLiteral("Downloader")));
        QVERIFY(s.contains(QStringLiteral("start")));
        QVERIFY(s.contains(QStringLiteral("shared")));

        const QString kt = QStringLiteral(
            "data class User(val id: Int)\n"
            "class Repo {\n"
            "    suspend fun fetch(): User? = null\n"
            "}\n");
        const QStringList k = namesOf(kt, QStringLiteral("kotlin"));
        QVERIFY(k.contains(QStringLiteral("User")));
        QVERIFY(k.contains(QStringLiteral("Repo")));
        QVERIFY(k.contains(QStringLiteral("fetch")));
    }

    void scriptingLanguages()
    {
        QVERIFY(namesOf(QStringLiteral("sub main {\n}\npackage Foo::Bar;\n"),
                        QStringLiteral("perl"))
                    .contains(QStringLiteral("main")));

        QVERIFY(namesOf(QStringLiteral("class Foo\n  def bar?\n  end\nend\n"),
                        QStringLiteral("ruby"))
                    .contains(QStringLiteral("bar?")));

        QVERIFY(namesOf(QStringLiteral("function Get-Thing {\n}\n"),
                        QStringLiteral("powershell"))
                    .contains(QStringLiteral("Get-Thing")));

        QVERIFY(namesOf(QStringLiteral("local function helper(a)\nend\n"),
                        QStringLiteral("lua"))
                    .contains(QStringLiteral("helper")));

        QVERIFY(namesOf(QStringLiteral("deploy() {\n  echo hi\n}\n"),
                        QStringLiteral("bash"))
                    .contains(QStringLiteral("deploy")));

        QVERIFY(namesOf(QStringLiteral("summarise <- function(x) {\n}\n"),
                        QStringLiteral("r"))
                    .contains(QStringLiteral("summarise")));
    }

    // 大小寫不敏感語言（VB / SQL / Pascal）
    void caseInsensitiveLanguages()
    {
        QVERIFY(namesOf(QStringLiteral("PUBLIC SUB DoWork()\nEND SUB\n"), QStringLiteral("vb"))
                    .contains(QStringLiteral("DoWork")));
        QVERIFY(namesOf(QStringLiteral("create or replace procedure sp_load as\n"),
                        QStringLiteral("sql"))
                    .contains(QStringLiteral("sp_load")));
        QVERIFY(namesOf(QStringLiteral("Procedure Init;\nbegin\nend;\n"), QStringLiteral("pascal"))
                    .contains(QStringLiteral("Init")));
    }

    // 標記式/設定式檔案：Markdown 標題、INI 區段、TOML 表、TeX 章節
    void documentStyleLanguages()
    {
        QCOMPARE(namesOf(QStringLiteral("# Title\n\n## Section A\ntext\n### Deep\n"),
                         QStringLiteral("markdown")),
                 QStringList({QStringLiteral("Title"), QStringLiteral("Section A"),
                              QStringLiteral("Deep")}));

        QVERIFY(namesOf(QStringLiteral("[server]\nport=1\n[client]\n"),
                        QStringLiteral("properties"))
                    .contains(QStringLiteral("client")));

        QVERIFY(namesOf(QStringLiteral("[package]\nname = \"x\"\n[[bin]]\n"),
                        QStringLiteral("toml"))
                    .contains(QStringLiteral("package")));

        QVERIFY(namesOf(QStringLiteral("\\section{Intro}\n\\subsection{Detail}\n"),
                        QStringLiteral("tex"))
                    .contains(QStringLiteral("Intro")));
    }

    // 不支援解析規則的語言仍安全回傳空清單（不得崩潰）
    void unsupportedLanguageReturnsEmpty()
    {
        QVERIFY(FunctionListParser::parse(QStringLiteral("anything"),
                                          QStringLiteral("whitespace"))
                    .isEmpty());
        QVERIFY(FunctionListParser::parse(QStringLiteral("anything"), QString()).isEmpty());
    }
};

QTEST_MAIN(TestFunctionListLangs)
#include "test_functionlist_langs.moc"
