#include "features/update/UpdateChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace macpad::features {

namespace {
// 專案的 Releases API。只有在使用者主動要求檢查時才會被存取。
constexpr const char *kReleasesApi =
    "https://api.github.com/repos/EddyKuo/macpadpp/releases/latest";
constexpr int kTimeoutMs = 8000;
}  // namespace

int compareVersions(const QString &a, const QString &b)
{
    auto parts = [](const QString &v) {
        QString s = v.trimmed();
        if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V')))
            s = s.mid(1);
        QList<int> out;
        for (const QString &seg : s.split(QLatin1Char('.'))) {
            bool ok = false;
            // "3-rc1" 之類取前導數字部分；完全非數字則視為 0
            int i = 0;
            while (i < seg.size() && seg.at(i).isDigit())
                ++i;
            const int n = seg.left(i).toInt(&ok);
            out << (ok ? n : 0);
        }
        return out;
    };

    const QList<int> pa = parts(a);
    const QList<int> pb = parts(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < pa.size() ? pa.at(i) : 0;
        const int vb = i < pb.size() ? pb.at(i) : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
}

void UpdateChecker::check(const QString &currentVersion)
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kReleasesApi))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("macpad++"));
    req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = m_net->get(req);

    // 逾時保護：離線或網路壞掉時不能讓 reply 永遠不回來
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(kTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVersion] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, QString(), QString(), reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit finished(false, QString(), QString(), tr("回應格式無法解析"));
            return;
        }
        const QJsonObject obj = doc.object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString();
        const QString url = obj.value(QStringLiteral("html_url")).toString();
        if (tag.isEmpty()) {
            emit finished(false, QString(), QString(), tr("回應中沒有版本資訊"));
            return;
        }
        emit finished(compareVersions(currentVersion, tag) < 0, tag, url, QString());
    });
}

}  // namespace macpad::features
