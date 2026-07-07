#include "platform/SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedPointer>

namespace macpad::platform {

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent), m_key(key)
{
    // 先嘗試連線既有 primary
    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(150)) {
        m_primary = false;  // 已有 primary 在跑
        probe.disconnectFromServer();
        return;
    }

    // 成為 primary：移除殘留 socket 後監聽
    QLocalServer::removeServer(m_key);
    m_server = new QLocalServer(this);
    if (m_server->listen(m_key)) {
        m_primary = true;
        connect(m_server, &QLocalServer::newConnection, this, [this] {
            QLocalSocket *conn = m_server->nextPendingConnection();
            if (!conn)
                return;
            // 每個連線一個累積緩衝區：payload 以 quint32 長度前綴分幀。
            // readyRead 可能在資料尚未到齊時分多次觸發（大型 QStringList），
            // 故先收滿整個 frame 才反序列化，避免 stream underflow 靜默丟失參數（IL-4 / FR-034）。
            auto buffer = QSharedPointer<QByteArray>::create();
            connect(conn, &QLocalSocket::readyRead, this, [this, conn, buffer] {
                buffer->append(conn->readAll());
                // 尚未收滿 4 bytes 長度前綴 → 等待後續 readyRead
                if (buffer->size() < int(sizeof(quint32)))
                    return;
                QDataStream head(*buffer);
                quint32 frameSize = 0;
                head >> frameSize;
                // 尚未收齊整個 frame → 繼續等待
                if (buffer->size() < int(sizeof(quint32)) + int(frameSize))
                    return;
                QDataStream in(*buffer);
                quint32 skip = 0;
                in >> skip;  // 跳過長度前綴
                QStringList args;
                in >> args;
                if (in.status() == QDataStream::Ok && !args.isEmpty())
                    emit messageReceived(args);
                conn->deleteLater();
            });
            // 連線於資料到齊前中斷 → 清理，避免洩漏
            connect(conn, &QLocalSocket::disconnected, conn, &QLocalSocket::deleteLater);
        });
    } else {
        // listen 失敗且非「已有 primary」情形（權限/路徑/OS 層問題）：
        // 明確記錄原因，讓呼叫端可據此回退（獨立執行/記錄/重試）而非靜默失敗（IL-4）。
        m_error = m_server->errorString();
    }
}

bool SingleInstance::sendToPrimary(const QStringList &args)
{
    QLocalSocket sock;
    sock.connectToServer(m_key);
    if (!sock.waitForConnected(200))
        return false;
    // 以 quint32 長度前綴分幀，讓 primary 端能在資料到齊前不誤判（見 constructor 讀取端）
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out << quint32(0);  // 佔位，稍後回填 payload 長度
    out << args;
    out.device()->seek(0);
    out << quint32(block.size() - int(sizeof(quint32)));
    sock.write(block);
    const bool ok = sock.waitForBytesWritten(200);
    sock.disconnectFromServer();
    return ok;
}

}  // namespace macpad::platform
