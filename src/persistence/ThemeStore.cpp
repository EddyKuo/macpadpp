#include "persistence/ThemeStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>              // 深/淺色判定
#include <QHash>
#include <QXmlStreamReader>    // Notepad++ stylers.xml 匯入

namespace macpad::persistence {

// themes/ 子目錄（不存在則建立）；回傳絕對路徑
static QString themesDir()
{
    const QString dir = AppPaths::configDir() + QStringLiteral("/themes");
    QDir().mkpath(dir);
    return dir;
}

static QString themeFilePath(const QString &name)
{
    return themesDir() + QLatin1Char('/') + name + QStringLiteral(".json");
}

// 主題的 styles 區塊與 styles.json 共用同一份 schema（含 global 邊界/底色 + 每語言覆寫），
// 序列化統一委派給 StyleStore，確保欄位不漂移（IL-3 單一真相）。
static QJsonObject themeToJson(const Theme &t)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("dark"), t.dark);
    o.insert(QStringLiteral("styles"), StyleStore::styleSettingsToJson(t.styles));
    return o;
}

static Theme themeFromJson(const QJsonObject &o)
{
    Theme t;
    t.name = o.value(QStringLiteral("name")).toString();
    t.dark = o.value(QStringLiteral("dark")).toBool();
    t.styles = StyleStore::styleSettingsFromJson(o.value(QStringLiteral("styles")).toObject());
    return t;
}

QStringList ThemeStore::listThemes()
{
    QStringList names;
    const QDir dir(themesDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                             QDir::Files, QDir::Name);
    names.reserve(files.size());
    for (const QString &f : files)
        names.append(QFileInfo(f).completeBaseName());
    return names;
}

Theme ThemeStore::load(const QString &name)
{
    if (name.isEmpty())
        return Theme();
    const QJsonObject o = JsonFile::load(themeFilePath(name));
    if (o.isEmpty())
        return Theme();
    Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        t.name = name;  // 容錯：檔案內未存 name 時以檔名回填
    return t;
}

bool ThemeStore::save(const Theme &theme)
{
    if (theme.name.isEmpty())
        return false;
    return JsonFile::save(themeFilePath(theme.name), themeToJson(theme));
}

bool ThemeStore::importFromFile(const QString &path)
{
    const QJsonObject o = JsonFile::load(path);
    if (o.isEmpty())
        return false;
    const Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        return false;
    return save(t);
}

bool ThemeStore::exportToFile(const QString &name, const QString &path)
{
    const Theme t = load(name);
    if (t.name.isEmpty())
        return false;
    return JsonFile::save(path, themeToJson(t));
}

bool ThemeStore::remove(const QString &name)
{
    if (name.isEmpty())
        return false;
    return QFile::remove(themeFilePath(name));
}

int ThemeStore::seedBundledThemes()
{
    const QDir res(QStringLiteral(":/themes"));
    const QStringList files = res.entryList(QStringList() << QStringLiteral("*.json"),
                                             QDir::Files, QDir::Name);
    int seeded = 0;
    for (const QString &f : files) {
        const QString name = QFileInfo(f).completeBaseName();
        const QString target = themeFilePath(name);
        if (QFileInfo::exists(target))
            continue;  // 已存在（使用者可能已修改）→ 不覆蓋；使用者刪掉的也不還原
        // 直接複製資源檔內容（本身已是合法的主題 JSON）；QFile::copy 對唯讀資源可正常讀取。
        if (QFile::copy(res.filePath(f), target)) {
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner
                                          | QFile::ReadGroup | QFile::ReadOther);  // 資源檔為唯讀，放寬使其可被匯出/刪除
            ++seeded;
        }
    }
    return seeded;
}

// ── Notepad++ stylers.xml 匯入 ────────────────────────────────────────────
// Notepad++ 主題為 XML，結構如下（顏色為不含 '#' 的 RRGGBB）：
//   <NotepadPlus>
//     <GlobalStyles>
//       <WidgetStyle name="Global override" styleID="0" fgColor="DCDCDC" bgColor="1E1E1E"/>
//       <WidgetStyle name="Current line background colour" bgColor="282828"/>  …
//     </GlobalStyles>
//     <LexerStyles>
//       <LexerType name="cpp" desc="C++"><WordsStyle name="COMMENT" styleID="1" fgColor="6A9955" fontStyle="1"/>…
//     </LexerStyles>
//   </NotepadPlus>
// fontStyle 為位元旗標：1=bold 2=italic 4=underline。

namespace {

// "1E1E1E" → "#1E1E1E"；空字串或格式不符回傳空字串（代表不覆寫）。
QString nppColor(const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.size() != 6)
        return {};
    for (const QChar c : t) {
        if (!isxdigit(c.toLatin1()))
            return {};
    }
    return QLatin1Char('#') + t.toUpper();
}

// Notepad++ 的 LexerType name → 本專案 LexerFactory 語言鍵。
// 只映射兩邊都支援者；未列出的語言略過（不強行猜測，IL-1）。
QString nppLangToKey(const QString &nppName)
{
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("cpp"), QStringLiteral("cpp")},
        {QStringLiteral("c"), QStringLiteral("cpp")},
        {QStringLiteral("java"), QStringLiteral("java")},
        {QStringLiteral("javascript"), QStringLiteral("javascript")},
        {QStringLiteral("javascript.js"), QStringLiteral("javascript")},
        {QStringLiteral("python"), QStringLiteral("python")},
        {QStringLiteral("json"), QStringLiteral("json")},
        {QStringLiteral("css"), QStringLiteral("css")},
        {QStringLiteral("bash"), QStringLiteral("bash")},
        {QStringLiteral("sql"), QStringLiteral("sql")},
        {QStringLiteral("yaml"), QStringLiteral("yaml")},
        {QStringLiteral("markdown"), QStringLiteral("markdown")},
        {QStringLiteral("html"), QStringLiteral("html")},
        {QStringLiteral("xml"), QStringLiteral("xml")},
    };
    return kMap.value(nppName.trimmed().toLower());
}

// GlobalStyles 的 WidgetStyle name → GlobalStyles 欄位指標（取 fg 或 bg）。
// Notepad++ 各項名稱固定，直接以名稱對應，不依賴 styleID（不同版本會漂移）。
void applyGlobalWidget(GlobalStyles &g, const QString &name,
                       const QString &fg, const QString &bg)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("global override")) {
        if (!fg.isEmpty()) g.editorFg = fg;
        if (!bg.isEmpty()) g.editorBg = bg;
    } else if (n == QLatin1String("default style")) {
        if (!fg.isEmpty()) g.editorFg = fg;
        if (!bg.isEmpty()) g.editorBg = bg;
    } else if (n == QLatin1String("current line background colour")) {
        g.caretLineBg = bg.isEmpty() ? fg : bg;
    } else if (n == QLatin1String("selected text colour")) {
        g.selectionBg = bg.isEmpty() ? fg : bg;
    } else if (n == QLatin1String("caret colour")) {
        g.caretColor = fg;
    } else if (n == QLatin1String("edge colour")) {
        g.edgeColor = fg;
    } else if (n == QLatin1String("line number margin")) {
        g.marginFg = fg;
        g.marginBg = bg;
    } else if (n == QLatin1String("current line number")) {
        g.currentLineNumber = fg;
    } else if (n == QLatin1String("fold margin")) {
        g.foldMargin = bg.isEmpty() ? fg : bg;
    } else if (n == QLatin1String("fold active")) {
        g.foldActive = fg;
    } else if (n == QLatin1String("white space symbol")) {
        g.whitespaceFg = fg;
    } else if (n == QLatin1String("bookmark margin")) {
        g.bookmarkMargin = bg.isEmpty() ? fg : bg;
    } else if (n == QLatin1String("indent guideline style")) {
        g.indentGuide = fg;
    } else if (n == QLatin1String("bad brace colour")) {
        g.badBrace = fg;
    } else if (n == QLatin1String("mark colour")) {
        g.markColor = fg;
    } else if (n == QLatin1String("url hovered")) {
        g.urlHovered = fg;
    }
    // 其餘（Brace highlight、Smart HighLighting…）本專案無對應欄位，略過。
}

// 以背景亮度判定深/淺色主題（Notepad++ 的 XML 不含此資訊）。
bool looksDark(const QString &bgHex)
{
    const QColor c(bgHex);
    if (!c.isValid())
        return false;
    // 感知亮度（ITU-R BT.601）；< 128 視為深色
    return (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) < 128.0;
}

}  // namespace

Theme ThemeStore::themeFromNppXml(const QByteArray &xml, const QString &themeName,
                                  QString *errorMessage)
{
    Theme theme;
    QXmlStreamReader r(xml);

    QString currentLang;     // 目前 LexerType 對應到的本專案語言鍵（空 = 不支援，略過）
    bool sawNotepadPlus = false;

    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        const QStringView el = r.name();
        const QXmlStreamAttributes at = r.attributes();

        if (el == QLatin1String("NotepadPlus")) {
            sawNotepadPlus = true;
        } else if (el == QLatin1String("WidgetStyle")) {
            applyGlobalWidget(theme.styles.global,
                              at.value(QLatin1String("name")).toString(),
                              nppColor(at.value(QLatin1String("fgColor")).toString()),
                              nppColor(at.value(QLatin1String("bgColor")).toString()));
        } else if (el == QLatin1String("LexerType")) {
            currentLang = nppLangToKey(at.value(QLatin1String("name")).toString());
        } else if (el == QLatin1String("WordsStyle")) {
            if (currentLang.isEmpty())
                continue;   // 該語言本專案無對應，整段略過
            bool ok = false;
            const int styleId = at.value(QLatin1String("styleID")).toInt(&ok);
            if (!ok)
                continue;
            StyleOverride so;
            so.style = styleId;
            so.fg = nppColor(at.value(QLatin1String("fgColor")).toString());
            so.bg = nppColor(at.value(QLatin1String("bgColor")).toString());
            const int fontStyle = at.value(QLatin1String("fontStyle")).toInt();
            so.bold      = (fontStyle & 1) != 0;
            so.italic    = (fontStyle & 2) != 0;
            so.underline = (fontStyle & 4) != 0;
            // 全空的覆寫沒有意義，不要塞進去膨脹檔案
            if (so.fg.isEmpty() && so.bg.isEmpty() && !so.bold && !so.italic && !so.underline)
                continue;
            theme.styles.byLang[currentLang].append(so);
        }
    }

    if (r.hasError()) {
        if (errorMessage)
            *errorMessage = QObject::tr("XML parse error: %1").arg(r.errorString());
        return {};
    }
    if (!sawNotepadPlus) {
        if (errorMessage)
            *errorMessage = QObject::tr("Not a Notepad++ theme file (missing <NotepadPlus> root)");
        return {};
    }

    theme.name = themeName;
    theme.dark = looksDark(theme.styles.global.editorBg);
    return theme;
}

bool ThemeStore::importFromNppXmlFile(const QString &path, QString *errorMessage)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = f.errorString();
        return false;
    }
    const QByteArray xml = f.readAll();
    f.close();

    const Theme theme = themeFromNppXml(xml, QFileInfo(path).completeBaseName(), errorMessage);
    if (theme.name.isEmpty())
        return false;   // errorMessage 已由 themeFromNppXml 填寫
    return save(theme);
}

}  // namespace macpad::persistence
