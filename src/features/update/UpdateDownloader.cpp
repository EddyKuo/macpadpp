#include "features/update/UpdateDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

namespace macpad::features {

UpdateDownloader::UpdateDownloader(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
}

UpdateDownloader::~UpdateDownloader()
{
    // 物件先於下載完成被銷毀時（例如下載中關閉視窗，父物件連帶刪除本物件），
    // 不留下半截檔案。abort() 會同步觸發 finished 的 lambda，而該 lambda 的
    // receiver 正是「正在解構的 this」，還會 emit finished ——從解構子發訊號是
    // Qt 明文警告的危險操作（接收端可能同為正在銷毀的兄弟物件）。
    // 故先切斷訊號再中止，讓解構子只做清理、不發任何訊號。
    if (m_reply) {
        m_reply->blockSignals(true);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    cleanupPartial();
}

QString UpdateDownloader::fileNameForUrl(const QString &url)
{
    if (url.isEmpty())
        return QString();
    const QString path = QUrl(url).path();      // 去掉 query / fragment
    const QString name = path.section(QLatin1Char('/'), -1);
    return name;
}

QString UpdateDownloader::downloadDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!d.isEmpty() && QDir().mkpath(d))
        return d;
    return QDir::tempPath();   // 取不到下載資料夾就退回暫存目錄，不讓下載失敗
}

void UpdateDownloader::cleanupPartial()
{
    if (m_file) {
        m_file->close();
        // 只刪自己建立的殘檔
        if (!m_path.isEmpty() && QFileInfo::exists(m_path))
            QFile::remove(m_path);
        delete m_file;
        m_file = nullptr;
    }
}

void UpdateDownloader::start(const QString &url, qint64 expectedBytes)
{
    // 重入防護：直接覆寫 m_reply/m_file 會漏掉前一個下載的物件與磁碟殘檔，
    // 且舊 reply 的 lambda 仍連著本物件，會把資料寫進新的檔案而互相汙染。
    if (m_reply) {
        emit finished(false, QString(), tr("已有下載進行中"));
        return;
    }

    m_cancelled = false;
    m_expected = expectedBytes;

    const QString name = fileNameForUrl(url);
    if (name.isEmpty()) {
        emit finished(false, QString(), tr("下載網址無效"));
        return;
    }

    // 落地檔名衝突時加序號，絕不覆寫使用者既有檔案。
    // 開檔一律用 NewOnly：即使在 exists() 與 open() 之間有人搶先建立同名檔，
    // 開檔會失敗而不是把對方截斷（WriteOnly 會 truncate）。
    const QFileInfo nameInfo(name);
    const QString base = nameInfo.completeBaseName();
    const QString suffix = nameInfo.suffix();
    constexpr int kMaxAttempts = 1000;
    QString err;
    for (int n = 0; n < kMaxAttempts; ++n) {
        const QString candidate = (n == 0)
            ? name
            : (suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n)
                                : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix));
        m_path = QDir(downloadDir()).filePath(candidate);
        auto *f = new QFile(m_path);
        if (f->open(QIODevice::NewOnly)) {
            m_file = f;
            break;
        }
        err = f->errorString();
        delete f;
        m_path.clear();
    }
    if (!m_file) {
        // 試滿仍開不成就明確失敗，不退而求其次去覆寫既有檔案
        emit finished(false, QString(),
                      err.isEmpty() ? tr("無法建立下載檔案") : err);
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("macpad++"));
    // GitHub 的下載網址會 302 轉址到 CDN，必須允許跟隨
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    m_reply = m_net->get(req);

    // 串流寫檔：150 MB 的發佈檔不可整包留在記憶體。
    // 寫入失敗（磁碟滿/權限被撤）必須立刻中止——否則只會得到一個看似正常的截斷檔。
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_file)
            return;
        const QByteArray chunk = m_reply->readAll();
        if (m_file->write(chunk) != chunk.size()) {
            m_writeError = m_file->errorString();
            m_reply->abort();   // 觸發 finished，於該處統一清理並回報
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();

        // 寫檔失敗優先回報：此時 reply 也是被我們 abort 的，錯誤訊息會是「已取消」而誤導
        if (!m_writeError.isEmpty()) {
            const QString err = m_writeError;
            m_writeError.clear();
            cleanupPartial();
            m_path.clear();
            emit finished(false, QString(), tr("寫入檔案失敗：%1").arg(err));
            return;
        }
        if (m_cancelled) {
            cleanupPartial();
            m_path.clear();
            emit finished(false, QString(), tr("已取消"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QString err = reply->errorString();
            cleanupPartial();
            m_path.clear();
            emit finished(false, QString(), err);
            return;
        }

        if (m_file) {
            const QByteArray tail = reply->readAll();   // 收尾未讀完的部分
            if (m_file->write(tail) != tail.size()) {
                const QString err = m_file->errorString();
                cleanupPartial();
                m_path.clear();
                emit finished(false, QString(), tr("寫入檔案失敗：%1").arg(err));
                return;
            }
            m_file->close();
        }
        const qint64 actual = QFileInfo(m_path).size();

        // 完整性檢查：位元組數不符表示下載被截斷，不可交給使用者安裝
        if (m_expected > 0 && actual != m_expected) {
            const QString err = tr("下載不完整：預期 %1 位元組，實得 %2")
                                    .arg(m_expected).arg(actual);
            cleanupPartial();
            m_path.clear();
            emit finished(false, QString(), err);
            return;
        }
        if (actual <= 0) {
            cleanupPartial();
            m_path.clear();
            emit finished(false, QString(), tr("下載內容為空"));
            return;
        }

        delete m_file;   // 已 close，保留檔案本體
        m_file = nullptr;
        const QString done = m_path;
        m_path.clear();
        emit finished(true, done, QString());
    });
}

void UpdateDownloader::cancel()
{
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();   // 觸發 finished()，於該處清理殘檔
}

}  // namespace macpad::features
