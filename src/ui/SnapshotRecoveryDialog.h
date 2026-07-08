#pragma once

// SnapshotRecoveryDialog — 當機復原對話框（FR-054）
// 啟動時若偵測到未清空的快照（BackupService::pendingSnapshots()），顯示本對話框：
// 左側列出快照識別碼（可勾選複選），右側預覽選取項目的內容（以 UTF-8 解碼顯示）。
// 使用者可選擇「還原所選」（開啟已勾選/選取的快照為新分頁）、「全部捨棄」（清空所有快照）
// 或「取消」（不做任何處理）。本對話框不依賴 MainWindow，僅透過建構子傳入的識別碼清單運作。

#include <QDialog>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;

namespace macpad::ui {

class SnapshotRecoveryDialog : public QDialog {
    Q_OBJECT
public:
    explicit SnapshotRecoveryDialog(const QStringList &snapshotIds, QWidget *parent = nullptr);

    // 使用者按下「還原所選」時要還原的快照識別碼清單：
    // 若有任何項目被勾選則回傳所有勾選項目；否則回傳目前選取（currentItem）的單一項目。
    QStringList selectedSnapshots() const;

    // 使用者是否按下「全部捨棄」（呼叫端應接著呼叫 BackupService::clearSnapshots()）。
    bool discardAll() const { return m_discardAll; }

private slots:
    void onCurrentRowChanged(int row);
    void onRestoreClicked();
    void onDiscardAllClicked();

private:
    QListWidget *m_list = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    bool m_discardAll = false;
};

}  // namespace macpad::ui
