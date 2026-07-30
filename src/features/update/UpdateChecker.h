#pragma once

// UpdateChecker — 檢查是否有新版本（複刻 Notepad++ 的 Check for Updates / auto-updater）
//
// 與 Notepad++ 的差異（刻意保留）：本實作只「查詢並告知」，不下載、不自我覆寫。
// 靜默自我更新需要簽章與更新伺服器基礎建設，且會讓一個離線編輯器在使用者未察覺時
// 改寫自身二進位——風險與收益不成比例。使用者確認後由系統瀏覽器前往下載頁。
//
// 資料來源為 GitHub Releases API；僅在使用者主動點選或明確開啟偏好時才連線，
// 其餘時候本程式完全不連網。

#include <QJsonArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace macpad::features {

// 版本字串比較（"1.2.10" > "1.2.9"；忽略前置 'v'；非數字段落忽略）。
// 純函式，供單元測試直接驗證。
int compareVersions(const QString &a, const QString &b);

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    // 發出一次查詢。currentVersion 為目前執行中的版本（如 "0.5.2"）。
    // 完成後發出 finished()；失敗（無網路/逾時/格式錯誤）也會發出，errorText 非空。
    void check(const QString &currentVersion);

    // 從 Releases JSON 的 assets 陣列挑出符合目前平台的安裝檔。
    // Windows 取 *-x64.zip、macOS 取 *.dmg；找不到時回傳空字串（呼叫端退回 release 頁面）。
    // 純函式，與網路無關，可單元測試。
    static QString pickAssetForPlatform(const QJsonArray &assets, QString *sizeOut = nullptr);

signals:
    // hasUpdate：latestVersion 是否比 currentVersion 新
    // downloadUrl：release 頁面網址（供使用者自行下載）
    // assetUrl：符合目前平台的安裝檔直接下載網址（可能為空，代表該版沒有對應平台的檔案）
    // assetSize：該檔案位元組數（字串形式，0 = 未知）
    void finished(bool hasUpdate, const QString &latestVersion,
                  const QString &downloadUrl, const QString &errorText,
                  const QString &assetUrl = QString(), const QString &assetSize = QString());

private:
    QNetworkAccessManager *m_net = nullptr;
};

}  // namespace macpad::features
