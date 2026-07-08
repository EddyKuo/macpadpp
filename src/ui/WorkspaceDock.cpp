#include "ui/WorkspaceDock.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QTreeWidget>

namespace macpad::ui {

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIsDirRole = Qt::UserRole + 2;
constexpr int kPlaceholderRole = Qt::UserRole + 3;
}  // namespace

WorkspaceDock::WorkspaceDock(QWidget *parent)
    : QDockWidget(tr("Workspace"), parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    setWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemExpanded, this, &WorkspaceDock::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &WorkspaceDock::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &WorkspaceDock::showContextMenu);
}

QString WorkspaceDock::pathOf(QTreeWidgetItem *item) const
{
    return item ? item->data(0, kPathRole).toString() : QString();
}

bool WorkspaceDock::isDirItem(QTreeWidgetItem *item) const
{
    return item && item->data(0, kIsDirRole).toBool();
}

QTreeWidgetItem *WorkspaceDock::rootItemFor(QTreeWidgetItem *item) const
{
    if (!item)
        return nullptr;
    while (item->parent())
        item = item->parent();
    return item;
}

void WorkspaceDock::addPlaceholder(QTreeWidgetItem *item)
{
    auto *placeholder = new QTreeWidgetItem(item);
    placeholder->setData(0, kPlaceholderRole, true);
}

void WorkspaceDock::populateChildren(QTreeWidgetItem *item, const QString &dirPath)
{
    // 清除既有子節點（含 placeholder）
    while (item->childCount() > 0)
        delete item->takeChild(0);

    // 資料夾一律顯示；檔案名稱過濾器（m_nameFilters）僅套用於檔案。
    // m_showHidden 開啟時連同隱藏（dot 開頭 / 具隱藏屬性）資料夾與檔案一併列出。
    const QDir::Filters hiddenFlag = m_showHidden ? QDir::Hidden : QDir::Filters();
    QFileInfoList entries;
    {
        QDir dirs(dirPath);
        dirs.setFilter(QDir::Dirs | QDir::NoDotAndDotDot | hiddenFlag);
        dirs.setSorting(QDir::Name | QDir::IgnoreCase);
        entries += dirs.entryInfoList();
    }
    {
        QDir files(dirPath);
        files.setFilter(QDir::Files | hiddenFlag);
        files.setSorting(QDir::Name | QDir::IgnoreCase);
        if (!m_nameFilters.isEmpty())
            files.setNameFilters(m_nameFilters);
        entries += files.entryInfoList();
    }

    QFileIconProvider iconProvider;
    for (const QFileInfo &info : entries) {
        auto *child = new QTreeWidgetItem(item);
        child->setText(0, info.fileName());
        child->setIcon(0, iconProvider.icon(info));
        child->setData(0, kPathRole, info.absoluteFilePath());
        child->setData(0, kIsDirRole, info.isDir());
        if (info.isDir())
            addPlaceholder(child);
    }
}

void WorkspaceDock::onItemExpanded(QTreeWidgetItem *item)
{
    if (!item || item->childCount() != 1)
        return;
    QTreeWidgetItem *only = item->child(0);
    if (!only->data(0, kPlaceholderRole).toBool())
        return;
    populateChildren(item, pathOf(item));
}

void WorkspaceDock::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!item)
        return;
    if (item->data(0, kPlaceholderRole).toBool())
        return;
    if (!isDirItem(item)) {
        const QString path = pathOf(item);
        if (!path.isEmpty())
            emit fileActivated(path);
    }
}

void WorkspaceDock::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);

    QMenu menu(m_tree);

    QAction *addFolderAction = menu.addAction(tr("Add Folder…"));
    connect(addFolderAction, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
        if (!dir.isEmpty())
            addRoot(dir);
    });

    QAction *setFilterAction = menu.addAction(tr("Set Filter…"));
    connect(setFilterAction, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString current = m_nameFilters.join(QStringLiteral(";"));
        const QString text = QInputDialog::getText(this, tr("Set Filter"),
                                                     tr("Name filters (e.g. *.cpp;*.h), empty to clear:"),
                                                     QLineEdit::Normal, current, &ok);
        if (!ok)
            return;
        QStringList filters;
        for (const QString &part : text.split(QChar(';'), Qt::SkipEmptyParts))
            filters.append(part.trimmed());
        setNameFilters(filters);
    });

    QAction *showHiddenAction = menu.addAction(tr("Show Hidden Files"));
    showHiddenAction->setCheckable(true);
    showHiddenAction->setChecked(m_showHidden);
    connect(showHiddenAction, &QAction::toggled, this, [this](bool on) {
        if (m_showHidden == on)
            return;
        m_showHidden = on;
        refreshAll();
    });

    if (item && !item->data(0, kPlaceholderRole).toBool()) {
        QTreeWidgetItem *rootItem = rootItemFor(item);
        const bool isRoot = (item == rootItem);
        const bool isDir = isDirItem(item);
        const QString itemPath = pathOf(item);
        const QString containingDir = isDir ? itemPath : QFileInfo(itemPath).absolutePath();

        QAction *removeRootAction = menu.addAction(tr("Remove Root"));
        removeRootAction->setEnabled(isRoot);
        connect(removeRootAction, &QAction::triggered, this, [this, rootItem]() {
            removeRoot(pathOf(rootItem));
        });

        menu.addSeparator();

        QAction *newFileAction = menu.addAction(tr("New File…"));
        connect(newFileAction, &QAction::triggered, this, [this, containingDir, item]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New File"), tr("File name:"),
                                                         QLineEdit::Normal, QString(), &ok);
            if (!ok || name.isEmpty())
                return;
            QFile file(QDir(containingDir).filePath(name));
            if (!file.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, tr("New File"), tr("Failed to create file: %1").arg(name));
                return;
            }
            file.close();
            QTreeWidgetItem *dirItem = isDirItem(item) ? item : item->parent();
            if (dirItem)
                populateChildren(dirItem, containingDir);
        });

        QAction *newFolderAction = menu.addAction(tr("New Folder…"));
        connect(newFolderAction, &QAction::triggered, this, [this, containingDir, item]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                                         QLineEdit::Normal, QString(), &ok);
            if (!ok || name.isEmpty())
                return;
            if (!QDir(containingDir).mkdir(name)) {
                QMessageBox::warning(this, tr("New Folder"), tr("Failed to create folder: %1").arg(name));
                return;
            }
            QTreeWidgetItem *dirItem = isDirItem(item) ? item : item->parent();
            if (dirItem)
                populateChildren(dirItem, containingDir);
        });

        menu.addSeparator();

        QAction *renameAction = menu.addAction(tr("Rename…"));
        renameAction->setEnabled(!isRoot);
        connect(renameAction, &QAction::triggered, this, [this, item, itemPath, containingDir]() {
            const QFileInfo info(itemPath);
            bool ok = false;
            const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                                            QLineEdit::Normal, info.fileName(), &ok);
            if (!ok || newName.isEmpty() || newName == info.fileName())
                return;
            const QString newPath = QDir(containingDir).filePath(newName);
            if (!QFile::rename(itemPath, newPath)) {
                QMessageBox::warning(this, tr("Rename"), tr("Failed to rename to: %1").arg(newName));
                return;
            }
            QTreeWidgetItem *parentItem = item->parent();
            if (parentItem)
                populateChildren(parentItem, containingDir);
        });

        QAction *deleteAction = menu.addAction(tr("Delete"));
        deleteAction->setEnabled(!isRoot);
        connect(deleteAction, &QAction::triggered, this, [this, item, itemPath, containingDir]() {
            const auto reply = QMessageBox::question(this, tr("Delete"),
                                                       tr("Move \"%1\" to Trash?").arg(QFileInfo(itemPath).fileName()));
            if (reply != QMessageBox::Yes)
                return;
            if (!QFile::moveToTrash(itemPath)) {
                QMessageBox::warning(this, tr("Delete"), tr("Failed to move to Trash: %1").arg(itemPath));
                return;
            }
            QTreeWidgetItem *parentItem = item->parent();
            if (parentItem)
                populateChildren(parentItem, containingDir);
        });

        menu.addSeparator();

        QAction *copyPathAction = menu.addAction(tr("Copy Full Path"));
        connect(copyPathAction, &QAction::triggered, this, [itemPath]() {
            QApplication::clipboard()->setText(itemPath);
        });

        QAction *copyNameAction = menu.addAction(tr("Copy File Name"));
        connect(copyNameAction, &QAction::triggered, this, [itemPath]() {
            QApplication::clipboard()->setText(QFileInfo(itemPath).fileName());
        });

        menu.addSeparator();

        QAction *findAction = menu.addAction(tr("Find in This Folder…"));
        connect(findAction, &QAction::triggered, this, [this, item]() {
            const QString path = pathOf(item);
            const QString dir = isDirItem(item) ? path : QFileInfo(path).absolutePath();
            if (!dir.isEmpty())
                emit findInFolderRequested(dir);
        });

        QAction *revealAction = menu.addAction(tr("Reveal in Finder"));
        connect(revealAction, &QAction::triggered, this, [item, this]() {
            const QString path = pathOf(item);
            if (!path.isEmpty())
                QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
        });

        QAction *terminalAction = menu.addAction(tr("Open Terminal Here"));
        connect(terminalAction, &QAction::triggered, this, [containingDir]() {
            if (!containingDir.isEmpty())
                QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), containingDir});
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void WorkspaceDock::refreshAll()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *rootItem = m_tree->topLevelItem(i);
        populateChildren(rootItem, pathOf(rootItem));
    }
}

void WorkspaceDock::setNameFilters(const QStringList &filters)
{
    m_nameFilters = filters;
    refreshAll();
}

void WorkspaceDock::setRoot(const QString &dir)
{
    m_tree->clear();
    m_roots.clear();
    addRoot(dir);
}

void WorkspaceDock::addRoot(const QString &dir)
{
    const QString absDir = QDir(dir).absolutePath();
    if (m_roots.contains(absDir))
        return;

    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, QFileInfo(absDir).fileName().isEmpty() ? absDir : QFileInfo(absDir).fileName());
    item->setData(0, kPathRole, absDir);
    item->setData(0, kIsDirRole, true);
    m_tree->addTopLevelItem(item);
    m_roots.append(absDir);

    populateChildren(item, absDir);
    item->setExpanded(true);
}

void WorkspaceDock::removeRoot(const QString &dir)
{
    const QString absDir = QDir(dir).absolutePath();
    const int idx = m_roots.indexOf(absDir);
    if (idx < 0)
        return;

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (pathOf(item) == absDir) {
            delete m_tree->takeTopLevelItem(i);
            break;
        }
    }
    m_roots.removeAt(idx);
}

}  // namespace macpad::ui
