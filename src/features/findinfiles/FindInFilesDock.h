#pragma once

// FindInFilesDock — Find in Files 面板（FR-013）
// 於 worker thread（QtConcurrent）搜尋，結果列於樹狀清單，雙擊跳轉。可取消。

#include <atomic>
#include <memory>

#include <QDockWidget>
#include <QFutureWatcher>

#include "features/findinfiles/FindInFilesEngine.h"

class QLineEdit;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;
class QPoint;

namespace macpad::features {

class FindInFilesDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FindInFilesDock(QWidget *parent = nullptr);
    ~FindInFilesDock() override;

    void setSearchRoot(const QString &dir);

    // 以明確檔案清單為範圍搜尋（供「Find in Projects」）：設定查詢字串、於 worker thread
    // 呼叫 FindInFilesEngine::searchInFiles，結果沿用與 Find in Files 相同的渲染（onSearchDone）。
    void findInProjectFiles(const QString &pattern, const QStringList &filePaths);

signals:
    void openLocation(const QString &path, int line, int column);

private slots:
    void startSearch();
    void cancelSearch();
    void onSearchDone();
    void replaceInFiles();
    void onReplaceDone();
    void showResultsContextMenu(const QPoint &pos);

private:
    FindInFilesOptions currentOptions() const;
    // 目前選取列的檔案路徑/行文字（右鍵選單「Copy Line Text」「Copy File Path」用）
    QString selectedResultFilePath() const;
    QString selectedResultLineText() const;

    QLineEdit *m_pattern = nullptr;
    QLineEdit *m_replace = nullptr;
    QLineEdit *m_dir = nullptr;
    QLineEdit *m_filter = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QCheckBox *m_includeHidden = nullptr;
    QLineEdit *m_exclude = nullptr;
    QTreeWidget *m_results = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_replaceBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_autoPurge = nullptr;   // 「auto-purge previous results」：新搜尋前先清空樹狀清單
    QCheckBox *m_wordWrapRows = nullptr; // 結果列文字換行（QTreeWidget word-wrap）

    QFutureWatcher<QVector<FindMatch>> m_watcher;
    QFutureWatcher<FindInFilesEngine::ReplaceResult> m_replaceWatcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

}  // namespace macpad::features
