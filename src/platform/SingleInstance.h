#pragma once

// SingleInstance — 單一執行個體協調（FR-034, IR-005）
// 以 QLocalServer/QLocalSocket 實作：第一個實例為 primary 並監聽；
// 後續實例可將檔案參數送給 primary 後結束。

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace macpad::platform {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);

    bool isPrimary() const { return m_primary; }
    // secondary：將參數送給 primary；成功回傳 true
    bool sendToPrimary(const QStringList &args);

    // 既非 primary 亦無既有 primary（listen 失敗）時的原因；成功時為空字串。
    // 讓呼叫端可區分「該轉送給 primary」與「成為 primary 失敗」（IL-4）。
    QString errorString() const { return m_error; }

signals:
    void messageReceived(const QStringList &args);  // primary 收到後續實例的參數

private:
    QString m_key;
    bool m_primary = false;
    QString m_error;
    QLocalServer *m_server = nullptr;
};

}  // namespace macpad::platform
