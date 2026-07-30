#pragma once

// UpdateDownloader — 下載新版安裝檔（複刻 Notepad++ 的 WinGUp 下載步驟）
//
// 職責僅止於「取得檔案並確認完整」：
//   1. 串流寫入暫存檔（不整包留在記憶體——發佈檔約 150 MB）
//   2. 回報進度供 UI 顯示
//   3. 完成後比對位元組數（伺服器有給 Content-Length 時），不符即視為失敗並刪除殘檔
//
// 刻意不做的事：不自行覆寫執行中的二進位。發佈物為免安裝 zip / DMG 而非安裝程式，
// 且未經簽章；由程式在使用者未察覺時替換自身，風險與收益不成比例。
// 下載完成後交由系統處理（在檔案管理器中顯示 / 開啟 DMG），最後一步由使用者決定。
//
// 全程只在使用者明確確認後才連線。

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace macpad::features {

class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    explicit UpdateDownloader(QObject *parent = nullptr);
    ~UpdateDownloader() override;

    // 開始下載 url 至系統下載目錄；expectedBytes > 0 時完成後會比對大小。
    // 同一物件同時只支援一個下載（呼叫端負責不重入）。
    void start(const QString &url, qint64 expectedBytes = 0);
    // 取消下載並刪除殘檔（使用者按取消時呼叫）
    void cancel();

    // 依網址推出落地檔名（去掉 query、取最後一段）；空網址回傳空字串。純函式，可單元測試。
    static QString fileNameForUrl(const QString &url);
    // 目的地目錄：系統下載資料夾，取不到時退回暫存目錄
    static QString downloadDir();

signals:
    void progress(qint64 received, qint64 total);
    // ok=false 時 path 為空、error 說明原因（含使用者取消）
    void finished(bool ok, const QString &path, const QString &error);

private:
    void cleanupPartial();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_path;
    QString m_writeError;   // 串流寫檔失敗訊息（非空即代表因寫檔失敗而中止）
    qint64 m_expected = 0;
    bool m_cancelled = false;
};

}  // namespace macpad::features
