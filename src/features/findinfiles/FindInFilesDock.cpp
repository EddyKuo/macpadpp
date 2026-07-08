#include "features/findinfiles/FindInFilesDock.h"

#include <QtConcurrent>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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
    m_includeHidden = new QCheckBox(tr("Include hidden files/folders"), root);
    m_exclude = new QLineEdit(root);
    m_exclude->setPlaceholderText(tr("!*.min.js; !+\\node_modules"));
    m_searchBtn = new QPushButton(tr("Search"), root);
    m_replaceBtn = new QPushButton(tr("Replace in Files"), root);
    m_cancelBtn = new QPushButton(tr("Cancel"), root);
    m_cancelBtn->setEnabled(false);
    auto *browse = new QPushButton(tr("…"), root);
    m_status = new QLabel(root);
    m_results = new QTreeWidget(root);
    m_results->setHeaderLabels({tr("File"), tr("Line"), tr("Text")});
    m_results->setRootIsDecorated(false);
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);

    // 「auto-purge previous results」：開啟時新搜尋自動清空舊結果（預設開啟，沿用既有 startSearch 行為）
    m_autoPurge = new QCheckBox(tr("Auto-purge previous results"), root);
    m_autoPurge->setChecked(true);
    // 結果列文字換行（trivial 版：切換 QTreeWidget 的 wordWrap）
    m_wordWrapRows = new QCheckBox(tr("Wrap result rows"), root);
    m_wordWrapRows->setChecked(false);

    grid->addWidget(new QLabel(tr("Find:"), root), 0, 0);
    grid->addWidget(m_pattern, 0, 1, 1, 3);
    grid->addWidget(m_searchBtn, 0, 4);
    grid->addWidget(new QLabel(tr("Replace:"), root), 1, 0);
    grid->addWidget(m_replace, 1, 1, 1, 3);
    grid->addWidget(m_replaceBtn, 1, 4);
    grid->addWidget(new QLabel(tr("Dir:"), root), 2, 0);
    grid->addWidget(m_dir, 2, 1, 1, 2);
    grid->addWidget(browse, 2, 3);
    grid->addWidget(m_cancelBtn, 2, 4);
    grid->addWidget(new QLabel(tr("Filter:"), root), 3, 0);
    grid->addWidget(m_filter, 3, 1);
    grid->addWidget(m_regex, 3, 2);
    grid->addWidget(m_caseSensitive, 3, 3);
    grid->addWidget(m_wholeWord, 3, 4);
    grid->addWidget(new QLabel(tr("Exclude:"), root), 4, 0);
    grid->addWidget(m_exclude, 4, 1, 1, 3);
    grid->addWidget(m_includeHidden, 4, 4);
    grid->addWidget(m_autoPurge, 5, 0, 1, 2);
    grid->addWidget(m_wordWrapRows, 5, 2, 1, 2);
    grid->addWidget(m_status, 6, 0, 1, 5);
    grid->addWidget(m_results, 7, 0, 1, 5);
    setWidget(root);

    connect(m_searchBtn, &QPushButton::clicked, this, &FindInFilesDock::startSearch);
    connect(m_pattern, &QLineEdit::returnPressed, this, &FindInFilesDock::startSearch);
    connect(m_replaceBtn, &QPushButton::clicked, this, &FindInFilesDock::replaceInFiles);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FindInFilesDock::cancelSearch);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choose Folder"), m_dir->text());
        if (!d.isEmpty()) m_dir->setText(d);
    });
    connect(&m_watcher, &QFutureWatcher<QVector<FindMatch>>::finished,
            this, &FindInFilesDock::onSearchDone);
    connect(&m_replaceWatcher, &QFutureWatcher<FindInFilesEngine::ReplaceResult>::finished,
            this, &FindInFilesDock::onReplaceDone);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) return;
        emit openLocation(path, item->data(1, Qt::UserRole).toInt(),
                          item->data(2, Qt::UserRole).toInt());
    });
    connect(m_results, &QTreeWidget::customContextMenuRequested,
            this, &FindInFilesDock::showResultsContextMenu);
    connect(m_wordWrapRows, &QCheckBox::toggled, m_results, &QTreeWidget::setWordWrap);
}

FindInFilesDock::~FindInFilesDock()
{
    cancelSearch();
    m_watcher.waitForFinished();
    m_replaceWatcher.waitForFinished();
}

void FindInFilesDock::setSearchRoot(const QString &dir)
{
    m_dir->setText(dir);
}

void FindInFilesDock::findInProjectFiles(const QString &pattern, const QStringList &filePaths)
{
    if (m_watcher.isRunning() || m_replaceWatcher.isRunning() || filePaths.isEmpty())
        return;
    m_pattern->setText(pattern);
    if (pattern.isEmpty())
        return;

    if (m_autoPurge->isChecked())
        m_results->clear();
    m_status->setText(tr("在 %1 個專案檔案中搜尋…").arg(filePaths.size()));
    m_searchBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    FindInFilesOptions opts = currentOptions();  // 沿用 regex/case/whole-word 等勾選
    opts.pattern = pattern;
    const QStringList paths = filePaths;

    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancel;
    // 背景執行（NFR-005，不阻塞 GUI；可取消）；沿用 m_watcher → onSearchDone 渲染
    m_watcher.setFuture(QtConcurrent::run([paths, opts, cancel] {
        return FindInFilesEngine::searchInFiles(paths, opts, cancel.get());
    }));
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

    opts.includeHidden = m_includeHidden->isChecked();

    // Exclude 輸入以 ';' 或換行分隔多個 pattern（FR-045），逐一 trim 並去除空白項
    const QString excludeText = m_exclude->text();
    const QStringList rawExcludes = excludeText.split(QRegularExpression(QStringLiteral("[;\\n]")),
                                                        Qt::SkipEmptyParts);
    for (const QString &raw : rawExcludes) {
        const QString trimmed = raw.trimmed();
        if (!trimmed.isEmpty())
            opts.excludeFilters.append(trimmed);
    }
    return opts;
}

void FindInFilesDock::replaceInFiles()
{
    if (m_watcher.isRunning() || m_replaceWatcher.isRunning())
        return;
    if (m_pattern->text().isEmpty() || m_dir->text().isEmpty())
        return;
    const auto ret = QMessageBox::question(
        this, tr("Replace in Files"),
        tr("將在目錄「%1」中把符合項全部取代並**寫回檔案**，此操作無法復原。是否繼續？")
            .arg(m_dir->text()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_status->setText(tr("取代中…"));
    m_searchBtn->setEnabled(false);
    m_replaceBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    const FindInFilesOptions opts = currentOptions();
    const QString dir = m_dir->text();
    const QString replacement = m_replace->text();

    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancel;
    // 背景執行（NFR-005，不阻塞 GUI；可取消）
    m_replaceWatcher.setFuture(QtConcurrent::run([dir, opts, replacement, cancel] {
        return FindInFilesEngine::replaceInFiles(dir, opts, replacement, cancel.get());
    }));
}

void FindInFilesDock::onReplaceDone()
{
    const FindInFilesEngine::ReplaceResult r = m_replaceWatcher.result();
    m_status->setText(tr("已於 %1 個檔案取代 %2 處").arg(r.filesChanged).arg(r.replacements));
    m_searchBtn->setEnabled(true);
    m_replaceBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

void FindInFilesDock::startSearch()
{
    if (m_watcher.isRunning() || m_replaceWatcher.isRunning() ||
        m_pattern->text().isEmpty() || m_dir->text().isEmpty())
        return;

    // 「auto-purge previous results」關閉時保留上次結果，新結果附加其後
    if (m_autoPurge->isChecked())
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

QString FindInFilesDock::selectedResultFilePath() const
{
    QTreeWidgetItem *item = m_results->currentItem();
    if (!item)
        return {};
    return item->data(0, Qt::UserRole).toString();
}

QString FindInFilesDock::selectedResultLineText() const
{
    QTreeWidgetItem *item = m_results->currentItem();
    if (!item)
        return {};
    return item->text(2);
}

void FindInFilesDock::showResultsContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_results->itemAt(pos);
    if (item)
        m_results->setCurrentItem(item);

    QMenu menu(this);
    QAction *copyLineAct = menu.addAction(tr("Copy Line Text"));
    QAction *copyPathAct = menu.addAction(tr("Copy File Path"));
    copyLineAct->setEnabled(item != nullptr);
    copyPathAct->setEnabled(item != nullptr && !selectedResultFilePath().isEmpty());
    menu.addSeparator();
    QAction *collapseAct = menu.addAction(tr("Collapse All"));
    QAction *expandAct = menu.addAction(tr("Expand All"));

    QAction *chosen = menu.exec(m_results->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == copyLineAct) {
        QApplication::clipboard()->setText(selectedResultLineText());
    } else if (chosen == copyPathAct) {
        QApplication::clipboard()->setText(selectedResultFilePath());
    } else if (chosen == collapseAct) {
        m_results->collapseAll();
    } else if (chosen == expandAct) {
        m_results->expandAll();
    }
}

}  // namespace macpad::features
