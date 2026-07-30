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
    // 物件先於下載完成被銷毀時，不留下半截檔案
    if (m_reply)
        m_reply->abort();
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
    m_cancelled = false;
    m_expected = expectedBytes;

    const QString name = fileNameForUrl(url);
    if (name.isEmpty()) {
        emit finished(false, QString(), tr("下載網址無效"));
        return;
    }

    // 落地檔名衝突時加序號，不覆寫使用者既有檔案
    QString target = QDir(downloadDir()).filePath(name);
    for (int n = 1; QFileInfo::exists(target) && n < 1000; ++n) {
        const QFileInfo fi(name);
        target = QDir(downloadDir())
                     .filePath(QStringLiteral("%1 (%2).%3")
                                   .arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
    }
    m_path = target;

    m_file = new QFile(m_path);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString err = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        m_path.clear();
        emit finished(false, QString(), err);
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("macpad++"));
    // GitHub 的下載網址會 302 轉址到 CDN，必須允許跟隨
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
    m_reply = m_net->get(req);

    // 串流寫檔：150 MB 的發佈檔不可整包留在記憶體
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_file)
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();

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
            m_file->write(reply->readAll());   // 收尾未讀完的部分
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
