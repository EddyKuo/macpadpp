// 單元測試：SnapshotRecoveryDialog — crash recovery 對話框（FR-054）
#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QListWidget>
#include <QPlainTextEdit>

#include "ui/SnapshotRecoveryDialog.h"
#include "features/backup/BackupService.h"

using namespace macpad::ui;
using namespace macpad::features;

class TestRecoveryDialog : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        BackupService::clearSnapshots();
    }

    void listsAndSelectsSnapshots()
    {
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("session1"), QStringLiteral("doc1"),
                                              QByteArrayLiteral("unsaved content A")));
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("session1"), QStringLiteral("doc2"),
                                              QByteArrayLiteral("unsaved content B")));

        const QStringList ids = BackupService::pendingSnapshots();
        QCOMPARE(ids.size(), 2);

        SnapshotRecoveryDialog dialog(ids);

        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 2);

        // 程式化勾選所有項目
        for (int i = 0; i < list->count(); ++i) {
            list->item(i)->setCheckState(Qt::Checked);
        }

        const QStringList selected = dialog.selectedSnapshots();
        QCOMPARE(selected.size(), 2);
        QVERIFY(selected.contains(QStringLiteral("session1/doc1")));
        QVERIFY(selected.contains(QStringLiteral("session1/doc2")));
    }

    void previewShowsSelectedSnapshotContent()
    {
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("session1"), QStringLiteral("doc1"),
                                              QByteArrayLiteral("preview content here")));

        const QStringList ids = BackupService::pendingSnapshots();
        SnapshotRecoveryDialog dialog(ids);

        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        list->setCurrentRow(0);

        auto *preview = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(preview != nullptr);
        QCOMPARE(preview->toPlainText(), QStringLiteral("preview content here"));
    }

    void noCheckedItemsFallsBackToCurrentSelection()
    {
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("s"), QStringLiteral("d1"),
                                              QByteArrayLiteral("v1")));
        QVERIFY(BackupService::writeSnapshot(QStringLiteral("s"), QStringLiteral("d2"),
                                              QByteArrayLiteral("v2")));

        const QStringList ids = BackupService::pendingSnapshots();
        SnapshotRecoveryDialog dialog(ids);

        auto *list = dialog.findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        list->setCurrentRow(1);

        const QStringList selected = dialog.selectedSnapshots();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first(), list->item(1)->text());
    }
};

QTEST_MAIN(TestRecoveryDialog)
#include "test_recoverydialog.moc"
