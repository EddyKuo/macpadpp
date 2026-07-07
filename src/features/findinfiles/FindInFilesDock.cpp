#include "features/findinfiles/FindInFilesDock.h"

#include <QtConcurrent>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QWidget>

namespace macpad::features {

FindInFilesDock::FindInFilesDock(QWidget *parent)
    : QDockWidget(tr("Find in Files"), parent)
{
    auto *root = new QWidget(this);
    auto *grid = new QGridLayout(root);

    m_pattern = new QLineEdit(root);
    m_pattern->setPlaceholderText(tr("搜尋…"));
    m_replace = new QLineEdit(root);
    m_replace->setPlaceholderText(tr("取代為…"));
    m_dir = new QLineEdit(root);
    m_filter = new QLineEdit(root);
    m_filter->setPlaceholderText(tr("*.cpp;*.h（空=全部）"));
    m_regex = new QCheckBox(tr("Regex"), root);
    m_caseSensitive = new QCheckBox(tr("Match case"), root);
    m_wholeWord = new QCheckBox(tr("Whole word"), root);
    m_searchBtn = new QPushButton(tr("Search"), root);
    auto *replaceBtn = new QPushButton(tr("Replace in Files"), root);
    m_cancelBtn = new QPushButton(tr("Cancel"), root);
    m_cancelBtn->setEnabled(false);
    auto *browse = new QPushButton(tr("…"), root);
    m_status = new QLabel(root);
    m_results = new QTreeWidget(root);
    m_results->setHeaderLabels({tr("File"), tr("Line"), tr("Text")});
    m_results->setRootIsDecorated(false);

    grid->addWidget(new QLabel(tr("Find:"), root), 0, 0);
    grid->addWidget(m_pattern, 0, 1, 1, 3);
    grid->addWidget(m_searchBtn, 0, 4);
    grid->addWidget(new QLabel(tr("Replace:"), root), 1, 0);
    grid->addWidget(m_replace, 1, 1, 1, 3);
    grid->addWidget(replaceBtn, 1, 4);
    grid->addWidget(new QLabel(tr("Dir:"), root), 2, 0);
    grid->addWidget(m_dir, 2, 1, 1, 2);
    grid->addWidget(browse, 2, 3);
    grid->addWidget(m_cancelBtn, 2, 4);
    grid->addWidget(new QLabel(tr("Filter:"), root), 3, 0);
    grid->addWidget(m_filter, 3, 1);
    grid->addWidget(m_regex, 3, 2);
    grid->addWidget(m_caseSensitive, 3, 3);
    grid->addWidget(m_wholeWord, 3, 4);
    grid->addWidget(m_status, 4, 0, 1, 5);
    grid->addWidget(m_results, 5, 0, 1, 5);
    setWidget(root);

    connect(m_searchBtn, &QPushButton::clicked, this, &FindInFilesDock::startSearch);
    connect(m_pattern, &QLineEdit::returnPressed, this, &FindInFilesDock::startSearch);
    connect(replaceBtn, &QPushButton::clicked, this, &FindInFilesDock::replaceInFiles);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FindInFilesDock::cancelSearch);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choose Folder"), m_dir->text());
        if (!d.isEmpty()) m_dir->setText(d);
    });
    connect(&m_watcher, &QFutureWatcher<QVector<FindMatch>>::finished,
            this, &FindInFilesDock::onSearchDone);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) return;
        emit openLocation(path, item->data(1, Qt::UserRole).toInt(),
                          item->data(2, Qt::UserRole).toInt());
    });
}

FindInFilesDock::~FindInFilesDock()
{
    cancelSearch();
    m_watcher.waitForFinished();
}

void FindInFilesDock::setSearchRoot(const QString &dir)
{
    m_dir->setText(dir);
}

FindInFilesOptions FindInFilesDock::currentOptions() const
{
    FindInFilesOptions opts;
    opts.pattern = m_pattern->text();
    opts.regex = m_regex->isChecked();
    opts.caseSensitive = m_caseSensitive->isChecked();
    opts.wholeWord = m_wholeWord->isChecked();
    const QString filterText = m_filter->text().trimmed();
    if (!filterText.isEmpty())
        opts.fileFilters = filterText.split(QRegularExpression(QStringLiteral("[;, ]")),
                                            Qt::SkipEmptyParts);
    return opts;
}

void FindInFilesDock::replaceInFiles()
{
    if (m_pattern->text().isEmpty() || m_dir->text().isEmpty())
        return;
    const auto ret = QMessageBox::question(
        this, tr("Replace in Files"),
        tr("將在目錄「%1」中把符合項全部取代並**寫回檔案**，此操作無法復原。是否繼續？")
            .arg(m_dir->text()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    const auto r = FindInFilesEngine::replaceInFiles(m_dir->text(), currentOptions(),
                                                     m_replace->text());
    m_status->setText(tr("已於 %1 個檔案取代 %2 處").arg(r.filesChanged).arg(r.replacements));
}

void FindInFilesDock::startSearch()
{
    if (m_watcher.isRunning() || m_pattern->text().isEmpty() || m_dir->text().isEmpty())
        return;

    m_results->clear();
    m_status->setText(tr("搜尋中…"));
    m_searchBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    FindInFilesOptions opts = currentOptions();
    const QString dir = m_dir->text();

    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancel;
    // 背景執行（NFR-005，不阻塞 GUI）
    m_watcher.setFuture(QtConcurrent::run([dir, opts, cancel] {
        return FindInFilesEngine::search(dir, opts, cancel.get());
    }));
}

void FindInFilesDock::cancelSearch()
{
    if (m_cancel)
        m_cancel->store(true);
}

void FindInFilesDock::onSearchDone()
{
    const QVector<FindMatch> matches = m_watcher.result();
    for (const FindMatch &m : matches) {
        auto *item = new QTreeWidgetItem(m_results);
        item->setText(0, QFileInfo(m.filePath).fileName());
        item->setText(1, QString::number(m.line));
        item->setText(2, m.lineText.trimmed());
        item->setData(0, Qt::UserRole, m.filePath);
        item->setData(1, Qt::UserRole, m.line);
        item->setData(2, Qt::UserRole, m.column);
        item->setToolTip(0, m.filePath);
    }
    m_status->setText(tr("找到 %1 筆").arg(matches.size()));
    m_searchBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

}  // namespace macpad::features
