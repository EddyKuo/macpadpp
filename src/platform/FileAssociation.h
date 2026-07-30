#pragma once

// FileAssociation — 把副檔名關聯到本程式（複刻 Notepad++ 的 Preferences ▸ File Association）
//
// 為何先前被列為「平台不可能」而現在補上：該判定是在本專案僅支援 macOS 時做的。
// 自 v0.5.0 起同時發行 Windows，而 Windows 的「每使用者」關聯只是寫入
// HKCU\Software\Classes 的登錄檔項目，完全可行且不需管理員權限、可完整還原。
//
// macOS 沒有等價的執行期 API：關聯由 app bundle 的 Info.plist（CFBundleDocumentTypes）
// 在建置時宣告，使用者端則透過 Finder「取得資訊 ▸ 打開檔案的應用程式」變更。
// 因此本模組在 macOS 回報 isSupported()==false，由 UI 明說原因，而不是靜默無效。
//
// 所有寫入僅在使用者於偏好設定中明確勾選時才發生，且只寫入 HKCU（不動 HKLM）。

#include <QString>
#include <QStringList>

namespace macpad::platform {

class FileAssociation {
public:
    // 本平台是否支援執行期變更關聯（Windows: true；其他: false）
    static bool isSupported();
    // 不支援時的說明文字（供 UI 顯示，避免使用者以為功能壞了）
    static QString unsupportedReason();

    // Notepad++ Preferences ▸ File Association 預設列出的常見副檔名（不含點）
    static QStringList commonExtensions();

    // 指定副檔名目前是否關聯到本程式
    static bool isAssociated(const QString &ext);
    // 建立關聯；失敗時回傳 false 並填入 error
    static bool associate(const QString &ext, QString *error = nullptr);
    // 移除本程式建立的關聯；未由本程式建立時不動作並回傳 true（冪等）
    static bool unassociate(const QString &ext, QString *error = nullptr);

    // 本程式使用的 ProgID（登錄檔中的類別識別字）
    static QString progId();

    // 組出登錄檔 shell\open\command 的命令字串：`"<exe>" "%1"`。
    // 其中的 `%1` 必須原樣保留——Windows 會把被雙擊的檔案路徑代入該 token；
    // 少了它，關聯後開檔會啟動程式卻收不到檔名。抽成純函式以便直接斷言。
    static QString openCommandFor(const QString &exePath);
};

}  // namespace macpad::platform
