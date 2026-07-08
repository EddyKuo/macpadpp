// 單元測試：BackupService — 儲存時備份（Simple/Verbose）+ 快照 crash-recovery round-trip
#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>

#include "features/backup/BackupService.h"

using namespace macpad::features;

class TestBackup : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試前清空快照，避免互相汙染
        BackupService::clearSnapshots();
    }

    void backupOnSaveNoneDoesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("note.txt"));
        const QByteArray content = QByteArrayLiteral("hello");
        QVERIFY(BackupService::backupOnSave(filePath, content, BackupMode::None));
        QVERIFY(!QFile::exists(filePath + QStringLiteral(".bak")));
    }

    void backupOnSaveSimpleWritesBak()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("note.txt"));
        const QByteArray content = QByteArrayLiteral("hello world");
        QVERIFY(BackupService::backupOnSave(filePath, content, BackupMode::Simple));

        const QString bakPath = filePath + QStringLiteral(".bak");
        QVERIFY(QFile::exists(bakPath));
        QFile f(bakPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), content);
    }

    void backupOnSaveSimpleOverwritesPreviousBak()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("note.txt"));
        QVERIFY(BackupService::backupOnSave(filePath, QByteArrayLiteral("first"), BackupMode::Simple));
        QVERIFY(BackupService::backupOnSave(filePath, QByteArrayLiteral("second"), BackupMode::Simple));

        const QString bakPath = filePath + QStringLiteral(".bak");
        QFile f(bakPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArrayLiteral("second"));
    }

    void backupOnSaveVerboseUsesGivenTimestamp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("note.txt"));
        const QByteArray content = QByteArrayLiteral("verbose content");
        const QString timestamp = QStringLiteral("20250101_120000");
        QVERIFY(BackupService::backupOnSave(filePath, content, BackupMode::Verbose, QString(), timestamp));

        const QString bakPath = filePath + QLatin1Char('.') + timestamp + QStringLiteral(".bak");
        QVERIFY(QFile::exists(bakPath));
        QFile f(bakPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), content);
    }

    void backupOnSaveUsesCustomDir()
    {
        QTemporaryDir srcDir;
        QTemporaryDir backupDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(backupDir.isValid());
        const QString filePath = srcDir.filePath(QStringLiteral("note.txt"));
        const QByteArray content = QByteArrayLiteral("custom dir content");
        QVERIFY(BackupService::backupOnSave(filePath, content, BackupMode::Simple, backupDir.path()));

        const QString bakPath = backupDir.filePath(QStringLiteral("note.txt.bak"));
        QVERIFY(QFile::exists(bakPath));
        QVERIFY(!QFile::exists(srcDir.filePath(QStringLiteral("note.txt.bak"))));
    }

    void snapshotRoundTrip()
    {
        QVERIFY(BackupService::pendingSnapshots().isEmpty());

        QVERIFY(BackupService::writeSnapshot(QStringLiteral("session1"), QStringLiteral("doc1"),
                                              QByteArrayLiteral("unsaved content A")));
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("session1"), QStringLiteral("doc2"),
                                              QByteArrayLiteral("unsaved content B")));

        const QStringList ids = BackupService::pendingSnapshots();
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains(QStringLiteral("session1/doc1")));
        QVERIFY(ids.contains(QStringLiteral("session1/doc2")));

        QCOMPARE(BackupService::readSnapshot(QStringLiteral("session1/doc1")),
                 QByteArrayLiteral("unsaved content A"));
        QCOMPARE(BackupService::readSnapshot(QStringLiteral("session1/doc2")),
                 QByteArrayLiteral("unsaved content B"));
    }

    void readSnapshotMissingReturnsEmpty()
    {
        QCOMPARE(BackupService::readSnapshot(QStringLiteral("no/such")), QByteArray());
    }

    void clearSnapshotsEmptiesList()
    {
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("s"), QStringLiteral("d"),
                                              QByteArrayLiteral("data")));
        QVERIFY(!BackupService::pendingSnapshots().isEmpty());

        BackupService::clearSnapshots();
        QVERIFY(BackupService::pendingSnapshots().isEmpty());
    }

    void writeSnapshotOverwritesSameId()
    {
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("s"), QStringLiteral("d"),
                                              QByteArrayLiteral("v1")));
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("s"), QStringLiteral("d"),
                                              QByteArrayLiteral("v2")));
        QCOMPARE(BackupService::pendingSnapshots().size(), 1);
        QCOMPARE(BackupService::readSnapshot(QStringLiteral("s/d")), QByteArrayLiteral("v2"));
    }
};

QTEST_GUILESS_MAIN(TestBackup)
#include "test_backup.moc"
