// 單元測試：SingleInstance — 單一執行個體協調（FR-034, IR-005）
#include <QtTest>
#include <QSignalSpy>
#include <QStringList>

#include "platform/SingleInstance.h"

using namespace macpad::platform;

class TestSingleInstance : public QObject {
    Q_OBJECT
private slots:
    void firstInstanceIsPrimary()
    {
        SingleInstance primary(QStringLiteral("macpad-test-singleinstance-nonce-A1"));
        QVERIFY(primary.isPrimary());
        QVERIFY(primary.errorString().isEmpty());
    }

    void secondInstanceIsSecondary()
    {
        const QString key = QStringLiteral("macpad-test-singleinstance-nonce-A2");
        SingleInstance primary(key);
        QVERIFY(primary.isPrimary());

        SingleInstance secondary(key);
        QVERIFY(!secondary.isPrimary());
    }

    void sendToPrimaryRoundTrip()
    {
        const QString key = QStringLiteral("macpad-test-singleinstance-nonce-A3");
        SingleInstance primary(key);
        QVERIFY(primary.isPrimary());
        QVERIFY(primary.errorString().isEmpty());

        SingleInstance secondary(key);
        QVERIFY(!secondary.isPrimary());

        QSignalSpy spy(&primary, &SingleInstance::messageReceived);
        QVERIFY(spy.isValid());

        const QStringList args{QStringLiteral("/tmp/a.txt"), QStringLiteral(":5")};
        // sendToPrimary 內部會泵動事件迴圈直到寫入緩衝排空（Windows named pipe 非同步寫入
        // 需要事件迴圈才能實際送出），故此處直接斷言其回傳 true。
        QVERIFY(secondary.sendToPrimary(args));

        // 等候 primary 端 readyRead + 反序列化完成
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> received = spy.at(0);
        QCOMPARE(received.size(), 1);
        const QStringList payload = received.at(0).toStringList();
        QCOMPARE(payload, args);
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
