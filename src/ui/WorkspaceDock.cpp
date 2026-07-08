#include "ui/WorkspaceDock.h"

#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QMenu>
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

    QDir dir(dirPath);
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QFileIconProvider iconProvider;
    const QFileInfoList entries = dir.entryInfoList();
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

    if (item && !item->data(0, kPlaceholderRole).toBool()) {
        QTreeWidgetItem *rootItem = rootItemFor(item);
        const bool isRoot = (item == rootItem);

        QAction *removeRootAction = menu.addAction(tr("Remove Root"));
        removeRootAction->setEnabled(isRoot);
        connect(removeRootAction, &QAction::triggered, this, [this, rootItem]() {
            removeRoot(pathOf(rootItem));
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
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
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
