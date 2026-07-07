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
class QPushButton;
class QLabel;

namespace macpad::features {

class FindInFilesDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FindInFilesDock(QWidget *parent = nullptr);
    ~FindInFilesDock() override;

    void setSearchRoot(const QString &dir);

signals:
    void openLocation(const QString &path, int line, int column);

private slots:
    void startSearch();
    void cancelSearch();
    void onSearchDone();
    void replaceInFiles();

private:
    FindInFilesOptions currentOptions() const;
    QLineEdit *m_pattern = nullptr;
    QLineEdit *m_replace = nullptr;
    QLineEdit *m_dir = nullptr;
    QLineEdit *m_filter = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QTreeWidget *m_results = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QLabel *m_status = nullptr;

    QFutureWatcher<QVector<FindMatch>> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

}  // namespace macpad::features
