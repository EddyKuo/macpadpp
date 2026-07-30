#include "platform/FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace macpad::platform {

namespace {

#ifdef Q_OS_WIN
// 每使用者的類別根：寫這裡不需要管理員權限，且優先於 HKLM 的系統層關聯。
const QString kClassesRoot = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes");

QString normalizedExt(const QString &ext)
{
    QString e = ext.trimmed().toLower();
    if (e.startsWith(QLatin1Char('.')))
        e = e.mid(1);
    return e;
}

// 執行檔的原生路徑（登錄檔中的命令列需為反斜線形式）
QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}
#endif

}  // namespace

QString FileAssociation::openCommandFor(const QString &exePath)
{
    // 注意：不可寫成 QStringLiteral("\"%1\" \"%%1\"").arg(exePath)。
    // QString::arg 沒有 printf 的 %% 逸出，且會取代「所有」同號標記——
    // 字面上的 %%1 內含第二個合法的 %1，會一併被換成執行檔路徑，
    // 產生 `"X" "%X"`：Windows 找不到 %1 token，關聯後開檔永遠收不到檔名。
    // 改用雙參數形式：替換文字本身不會再被掃描，字面的 %1 得以保留。
    return QStringLiteral("\"%1\" \"%2\"").arg(exePath, QStringLiteral("%1"));
}

QString FileAssociation::progId()
{
    // 以固定字串為 ProgID：不隨版本變動，否則升版後舊關聯會指向不存在的類別。
    return QStringLiteral("macpadpp.Document");
}

bool FileAssociation::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString FileAssociation::unsupportedReason()
{
#ifdef Q_OS_MACOS
    return QCoreApplication::translate(
        "FileAssociation",
        "macOS 的檔案關聯由應用程式套件的 Info.plist 宣告，無法於執行期變更。\n"
        "請於 Finder 選取檔案 ▸ 取得資訊 ▸「打開檔案的應用程式」中設定。");
#else
    return QCoreApplication::translate(
        "FileAssociation", "此平台不支援於執行期變更檔案關聯。");
#endif
}

QStringList FileAssociation::commonExtensions()
{
    // 對齊 Notepad++ File Association 頁的常見類別；使用者仍可自行輸入其他副檔名。
    return {
        QStringLiteral("txt"),  QStringLiteral("log"),  QStringLiteral("ini"),
        QStringLiteral("inf"),  QStringLiteral("nfo"),  QStringLiteral("md"),
        QStringLiteral("json"), QStringLiteral("xml"),  QStringLiteral("yml"),
        QStringLiteral("yaml"), QStringLiteral("csv"),  QStringLiteral("c"),
        QStringLiteral("h"),    QStringLiteral("cpp"),  QStringLiteral("hpp"),
        QStringLiteral("cs"),   QStringLiteral("java"), QStringLiteral("py"),
        QStringLiteral("js"),   QStringLiteral("ts"),   QStringLiteral("html"),
        QStringLiteral("css"),  QStringLiteral("php"),  QStringLiteral("rb"),
        QStringLiteral("go"),   QStringLiteral("rs"),   QStringLiteral("sh"),
        QStringLiteral("bat"),  QStringLiteral("ps1"),  QStringLiteral("sql"),
    };
}

#ifdef Q_OS_WIN

bool FileAssociation::isAssociated(const QString &ext)
{
    const QString e = normalizedExt(ext);
    if (e.isEmpty())
        return false;
    QSettings classes(kClassesRoot, QSettings::NativeFormat);
    return classes.value(QStringLiteral(".%1/.").arg(e)).toString() == progId();
}

bool FileAssociation::associate(const QString &ext, QString *error)
{
    const QString e = normalizedExt(ext);
    if (e.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("FileAssociation", "副檔名不可為空");
        return false;
    }

    QSettings classes(kClassesRoot, QSettings::NativeFormat);

    // 1) 建立（或更新）本程式的 ProgID：開啟命令與圖示
    const QString cmd = openCommandFor(exePath());
    classes.setValue(QStringLiteral("%1/.").arg(progId()),
                     QCoreApplication::translate("FileAssociation", "macpad++ 文件"));
    classes.setValue(QStringLiteral("%1/DefaultIcon/.").arg(progId()),
                     QStringLiteral("\"%1\",0").arg(exePath()));
    classes.setValue(QStringLiteral("%1/shell/open/command/.").arg(progId()), cmd);

    // 2) 保留「本程式首次接手之前」的關聯值，解除時才還得回去。
    //    只在尚無備份時寫入：若中途有別的程式直接搶走關聯，使用者再次勾選時
    //    prev 會是那個程式，覆寫備份就會讓真正的原始擁有者永遠救不回來。
    const QString backupKey = QStringLiteral(".%1/macpadpp_backup").arg(e);
    const QString prev = classes.value(QStringLiteral(".%1/.").arg(e)).toString();
    if (!prev.isEmpty() && prev != progId() && classes.value(backupKey).toString().isEmpty())
        classes.setValue(backupKey, prev);

    // 3) 指向本程式
    classes.setValue(QStringLiteral(".%1/.").arg(e), progId());
    classes.sync();

    if (classes.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("FileAssociation", "寫入登錄檔失敗");
        return false;
    }

    // 通知檔案總管重新讀取關聯，否則圖示與「開啟方式」要等重新登入才更新
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool FileAssociation::unassociate(const QString &ext, QString *error)
{
    const QString e = normalizedExt(ext);
    if (e.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("FileAssociation", "副檔名不可為空");
        return false;
    }

    QSettings classes(kClassesRoot, QSettings::NativeFormat);
    // 只還原本程式建立的關聯；別人的關聯一律不碰
    if (classes.value(QStringLiteral(".%1/.").arg(e)).toString() != progId())
        return true;

    const QString backupKey = QStringLiteral(".%1/macpadpp_backup").arg(e);
    const QString prev = classes.value(backupKey).toString();
    if (!prev.isEmpty()) {
        classes.setValue(QStringLiteral(".%1/.").arg(e), prev);   // 還原先前的擁有者
        classes.remove(backupKey);
    } else {
        // 本來就沒人擁有預設值：只移除我們寫的那個預設值。
        // 不可 remove(".ext") ——QSettings 會刪掉整個子樹，而該鍵底下可能還有
        // 別的軟體放的 OpenWithProgids / OpenWithList / ShellNew 等，與我們無關。
        classes.remove(QStringLiteral(".%1/.").arg(e));
        // 若整個 .ext 已空無一物，才連空殼一併清掉
        classes.beginGroup(QStringLiteral(".%1").arg(e));
        const bool empty = classes.childKeys().isEmpty() && classes.childGroups().isEmpty();
        classes.endGroup();
        if (empty)
            classes.remove(QStringLiteral(".%1").arg(e));
    }

    // 已無任何副檔名指向本程式的 ProgID 時，一併移除 ProgID 本身，
    // 不在使用者的登錄檔留下孤兒鍵（解除關聯就該回到原狀）。
    bool stillReferenced = false;
    for (const QString &group : classes.childGroups()) {
        if (!group.startsWith(QLatin1Char('.')))
            continue;
        if (classes.value(QStringLiteral("%1/.").arg(group)).toString() == progId()) {
            stillReferenced = true;
            break;
        }
    }
    if (!stillReferenced)
        classes.remove(progId());

    classes.sync();

    if (classes.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("FileAssociation", "寫入登錄檔失敗");
        return false;
    }
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

#else   // 非 Windows：不假裝成功，明確回報不支援

bool FileAssociation::isAssociated(const QString &)
{
    return false;
}

bool FileAssociation::associate(const QString &, QString *error)
{
    if (error)
        *error = unsupportedReason();
    return false;
}

bool FileAssociation::unassociate(const QString &, QString *error)
{
    if (error)
        *error = unsupportedReason();
    return false;
}

#endif

}  // namespace macpad::platform
