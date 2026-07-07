#include "platform/SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

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
            connect(conn, &QLocalSocket::readyRead, this, [this, conn] {
                QDataStream in(conn);
                QStringList args;
                in >> args;
                if (!args.isEmpty())
                    emit messageReceived(args);
                conn->deleteLater();
            });
        });
    }
}

bool SingleInstance::sendToPrimary(const QStringList &args)
{
    QLocalSocket sock;
    sock.connectToServer(m_key);
    if (!sock.waitForConnected(200))
        return false;
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out << args;
    sock.write(block);
    const bool ok = sock.waitForBytesWritten(200);
    sock.disconnectFromServer();
    return ok;
}

}  // namespace macpad::platform
