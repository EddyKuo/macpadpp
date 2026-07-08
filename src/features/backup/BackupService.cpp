#include "features/backup/BackupService.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace macpad::features {

using macpad::persistence::AppPaths;
using macpad::persistence::JsonFile;

static bool writeFileBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = f.write(bytes);
    f.close();
    return written == bytes.size();
}

bool BackupService::backupOnSave(const QString &filePath, const QByteArray &contentBytes, BackupMode mode,
                                  const QString &customDir, const QString &timestamp)
{
    if (mode == BackupMode::None)
        return true;  // 未啟用備份，視為成功（無需動作）
    if (filePath.isEmpty())
        return false;

    const QFileInfo fi(filePath);
    const QString dir = customDir.isEmpty() ? fi.absolutePath() : customDir;
    if (!QDir().mkpath(dir))
        return false;

    QString backupPath;
    if (mode == BackupMode::Simple) {
        backupPath = dir + QLatin1Char('/') + fi.fileName() + QStringLiteral(".bak");
    } else {  // Verbose
        backupPath = dir + QLatin1Char('/') + fi.fileName() + QLatin1Char('.') + timestamp + QStringLiteral(".bak");
    }
    return writeFileBytes(backupPath, contentBytes);
}

// snapshots/ 目錄（設定目錄下）；不存在則建立
static QString snapshotsDir()
{
    const QString d = AppPaths::filePath(QStringLiteral("snapshots"));
    QDir().mkpath(d);
    return d;
}

// 由快照識別碼（"sessionKey/docId"）推導檔名：以雜湊避免非法字元問題，
// 原始 sessionKey/docId 另存於 JSON 內容中以供 pendingSnapshots() 還原。
static QString snapshotFileName(const QString &id)
{
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex());
    return hash + QStringLiteral(".json");
}

static QString makeSnapshotId(const QString &sessionKey, const QString &docId)
{
    return sessionKey + QLatin1Char('/') + docId;
}

bool BackupService::writeSnapshot(const QString &sessionKey, const QString &docId, const QByteArray &content)
{
    if (sessionKey.isEmpty() || docId.isEmpty())
        return false;
    const QString id = makeSnapshotId(sessionKey, docId);
    QJsonObject o;
    o.insert(QStringLiteral("session_key"), sessionKey);
    o.insert(QStringLiteral("doc_id"), docId);
    o.insert(QStringLiteral("content"), QString::fromLatin1(content.toBase64()));
    return JsonFile::save(snapshotsDir() + QLatin1Char('/') + snapshotFileName(id), o);
}

QStringList BackupService::pendingSnapshots()
{
    QStringList ids;
    QDir dir(snapshotsDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files) {
        const QJsonObject o = JsonFile::load(dir.filePath(f));
        const QString sessionKey = o.value(QStringLiteral("session_key")).toString();
        const QString docId = o.value(QStringLiteral("doc_id")).toString();
        if (sessionKey.isEmpty() && docId.isEmpty())
            continue;  // 損毀或非快照檔案，略過
        ids << makeSnapshotId(sessionKey, docId);
    }
    return ids;
}

QByteArray BackupService::readSnapshot(const QString &id)
{
    const QJsonObject o = JsonFile::load(snapshotsDir() + QLatin1Char('/') + snapshotFileName(id));
    if (o.isEmpty())
        return QByteArray();
    return QByteArray::fromBase64(o.value(QStringLiteral("content")).toString().toLatin1());
}

void BackupService::clearSnapshots()
{
    QDir dir(snapshotsDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files)
        QFile::remove(dir.filePath(f));
}

}  // namespace macpad::features
