#include "core/LexerFactory.h"

#include <QFileInfo>
#include <QHash>

#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexercoffeescript.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerd.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexerfortran.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexeridl.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexermatlab.h>
#include <Qsci/qscilexermatlab.h>
#include <Qsci/qscilexerpascal.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexerpostscript.h>
#include <Qsci/qscilexerproperties.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexertcl.h>
#include <Qsci/qscilexertex.h>
#include <Qsci/qscilexerverilog.h>
#include <Qsci/qscilexervhdl.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>

namespace macpad::core {

// 副檔名 → 語言鍵（小寫，不含點）
static QString langKey(const QString &suffix)
{
    static const QHash<QString, QString> map = {
        {"c", "cpp"}, {"h", "cpp"}, {"cpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"},
        {"hpp", "cpp"}, {"hxx", "cpp"}, {"m", "cpp"}, {"mm", "cpp"}, {"ino", "cpp"},
        {"cs", "csharp"},
        {"java", "java"},
        {"js", "javascript"}, {"mjs", "javascript"}, {"jsx", "javascript"}, {"ts", "javascript"},
        {"py", "python"}, {"pyw", "python"},
        {"rb", "ruby"}, {"gemspec", "ruby"},
        {"pl", "perl"}, {"pm", "perl"},
        {"lua", "lua"},
        {"tcl", "tcl"},
        {"d", "d"},
        {"json", "json"},
        {"xml", "xml"}, {"svg", "xml"}, {"plist", "xml"}, {"xaml", "xml"},
        {"html", "html"}, {"htm", "html"}, {"php", "html"},
        {"css", "css"}, {"scss", "css"}, {"less", "css"},
        {"md", "markdown"}, {"markdown", "markdown"},
        {"yml", "yaml"}, {"yaml", "yaml"},
        {"sh", "bash"}, {"bash", "bash"}, {"zsh", "bash"},
        {"bat", "batch"}, {"cmd", "batch"},
        {"cmake", "cmake"},
        {"coffee", "coffeescript"},
        {"sql", "sql"},
        {"ini", "properties"}, {"cfg", "properties"}, {"conf", "properties"}, {"properties", "properties"},
        {"tex", "tex"}, {"sty", "tex"},
        {"diff", "diff"}, {"patch", "diff"},
        {"pas", "pascal"}, {"pp", "pascal"},
        {"f", "fortran"}, {"f90", "fortran"}, {"f95", "fortran"}, {"for", "fortran"},
        {"v", "verilog"}, {"sv", "verilog"},
        {"vhd", "vhdl"}, {"vhdl", "vhdl"},
        {"ps", "postscript"},
        {"idl", "idl"},
        {"mk", "makefile"},
    };
    return map.value(suffix.toLower());
}

static QsciLexer *lexerForLang(const QString &lang, QObject *parent)
{
    if (lang == "cpp") return new QsciLexerCPP(parent);
    if (lang == "csharp") return new QsciLexerCSharp(parent);
    if (lang == "java") return new QsciLexerJava(parent);
    if (lang == "javascript") return new QsciLexerJavaScript(parent);
    if (lang == "python") return new QsciLexerPython(parent);
    if (lang == "ruby") return new QsciLexerRuby(parent);
    if (lang == "perl") return new QsciLexerPerl(parent);
    if (lang == "lua") return new QsciLexerLua(parent);
    if (lang == "tcl") return new QsciLexerTCL(parent);
    if (lang == "d") return new QsciLexerD(parent);
    if (lang == "json") return new QsciLexerJSON(parent);
    if (lang == "xml") return new QsciLexerXML(parent);
    if (lang == "html") return new QsciLexerHTML(parent);
    if (lang == "css") return new QsciLexerCSS(parent);
    if (lang == "markdown") return new QsciLexerMarkdown(parent);
    if (lang == "yaml") return new QsciLexerYAML(parent);
    if (lang == "bash") return new QsciLexerBash(parent);
    if (lang == "batch") return new QsciLexerBatch(parent);
    if (lang == "cmake") return new QsciLexerCMake(parent);
    if (lang == "coffeescript") return new QsciLexerCoffeeScript(parent);
    if (lang == "sql") return new QsciLexerSQL(parent);
    if (lang == "properties") return new QsciLexerProperties(parent);
    if (lang == "tex") return new QsciLexerTeX(parent);
    if (lang == "diff") return new QsciLexerDiff(parent);
    if (lang == "pascal") return new QsciLexerPascal(parent);
    if (lang == "fortran") return new QsciLexerFortran(parent);
    if (lang == "verilog") return new QsciLexerVerilog(parent);
    if (lang == "vhdl") return new QsciLexerVHDL(parent);
    if (lang == "postscript") return new QsciLexerPostScript(parent);
    if (lang == "idl") return new QsciLexerIDL(parent);
    if (lang == "makefile") return new QsciLexerMakefile(parent);
    if (lang == "matlab") return new QsciLexerMatlab(parent);
    return nullptr;
}

QsciLexer *LexerFactory::createForExtension(const QString &suffix, QObject *parent)
{
    const QString s = suffix.startsWith('.') ? suffix.mid(1) : suffix;
    return lexerForLang(langKey(s), parent);
}

QsciLexer *LexerFactory::createForFileName(const QString &fileName, QObject *parent)
{
    const QFileInfo info(fileName);
    const QString name = info.fileName().toLower();
    if (name == "cmakelists.txt")
        return new QsciLexerCMake(parent);
    if (name == "makefile" || name == "gnumakefile")
        return new QsciLexerMakefile(parent);
    if (name == "dockerfile")
        return new QsciLexerBash(parent);
    return createForExtension(info.suffix(), parent);
}

QsciLexer *LexerFactory::createForLanguage(const QString &key, QObject *parent)
{
    return lexerForLang(key.toLower(), parent);
}

QVector<LanguageEntry> LexerFactory::languages()
{
    return {
        {QStringLiteral("Plain Text"), QString()},
        {QStringLiteral("Bash / Shell"), QStringLiteral("bash")},
        {QStringLiteral("Batch"), QStringLiteral("batch")},
        {QStringLiteral("C / C++ / Obj-C"), QStringLiteral("cpp")},
        {QStringLiteral("C#"), QStringLiteral("csharp")},
        {QStringLiteral("CMake"), QStringLiteral("cmake")},
        {QStringLiteral("CoffeeScript"), QStringLiteral("coffeescript")},
        {QStringLiteral("CSS"), QStringLiteral("css")},
        {QStringLiteral("D"), QStringLiteral("d")},
        {QStringLiteral("Diff / Patch"), QStringLiteral("diff")},
        {QStringLiteral("Fortran"), QStringLiteral("fortran")},
        {QStringLiteral("HTML / PHP"), QStringLiteral("html")},
        {QStringLiteral("IDL"), QStringLiteral("idl")},
        {QStringLiteral("Java"), QStringLiteral("java")},
        {QStringLiteral("JavaScript / TS"), QStringLiteral("javascript")},
        {QStringLiteral("JSON"), QStringLiteral("json")},
        {QStringLiteral("Lua"), QStringLiteral("lua")},
        {QStringLiteral("Makefile"), QStringLiteral("makefile")},
        {QStringLiteral("Markdown"), QStringLiteral("markdown")},
        {QStringLiteral("MATLAB"), QStringLiteral("matlab")},
        {QStringLiteral("Pascal"), QStringLiteral("pascal")},
        {QStringLiteral("Perl"), QStringLiteral("perl")},
        {QStringLiteral("PostScript"), QStringLiteral("postscript")},
        {QStringLiteral("Properties / INI"), QStringLiteral("properties")},
        {QStringLiteral("Python"), QStringLiteral("python")},
        {QStringLiteral("Ruby"), QStringLiteral("ruby")},
        {QStringLiteral("SQL"), QStringLiteral("sql")},
        {QStringLiteral("Tcl"), QStringLiteral("tcl")},
        {QStringLiteral("TeX / LaTeX"), QStringLiteral("tex")},
        {QStringLiteral("Verilog"), QStringLiteral("verilog")},
        {QStringLiteral("VHDL"), QStringLiteral("vhdl")},
        {QStringLiteral("XML"), QStringLiteral("xml")},
        {QStringLiteral("YAML"), QStringLiteral("yaml")},
    };
}

}  // namespace macpad::core
