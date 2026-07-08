#include "ui/ProjectPanelDock.h"

#include "persistence/ProjectStore.h"

#include <functional>

#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTreeWidget>

namespace macpad::ui {

using macpad::persistence::Project;
using macpad::persistence::ProjectNode;
using macpad::persistence::ProjectNodeType;
using macpad::persistence::ProjectStore;
using macpad::persistence::ProjectWorkspace;

namespace {
constexpr int kNodeKindRole = Qt::UserRole + 1;  // 0=project, 1=folder, 2=file
constexpr int kPathRole = Qt::UserRole + 2;

constexpr int kKindProject = 0;
constexpr int kKindFolder = 1;
constexpr int kKindFile = 2;
}  // namespace

ProjectPanelDock::ProjectPanelDock(QWidget *parent)
    : QDockWidget(tr("Project"), parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    setWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ProjectPanelDock::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ProjectPanelDock::showContextMenu);
}

QString ProjectPanelDock::pathOf(QTreeWidgetItem *item) const
{
    return item ? item->data(0, kPathRole).toString() : QString();
}

bool ProjectPanelDock::isFileItem(QTreeWidgetItem *item) const
{
    return item && item->data(0, kNodeKindRole).toInt() == kKindFile;
}

bool ProjectPanelDock::isProjectItem(QTreeWidgetItem *item) const
{
    return item && item->data(0, kNodeKindRole).toInt() == kKindProject;
}

QTreeWidgetItem *ProjectPanelDock::projectItemOf(QTreeWidgetItem *item) const
{
    if (!item)
        return nullptr;
    while (item->parent())
        item = item->parent();
    return item;
}

QTreeWidgetItem *ProjectPanelDock::findProjectItem(const QString &name) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->text(0) == name)
            return item;
    }
    return nullptr;
}

QTreeWidgetItem *ProjectPanelDock::addFolderNode(QTreeWidgetItem *parent, const QString &name,
                                                  const QString &diskPath)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, name);
    item->setData(0, kNodeKindRole, kKindFolder);
    item->setData(0, kPathRole, diskPath);
    QFileIconProvider iconProvider;
    item->setIcon(0, iconProvider.icon(QFileIconProvider::Folder));
    return item;
}

QTreeWidgetItem *ProjectPanelDock::addFileNode(QTreeWidgetItem *parent, const QString &filePath)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, QFileInfo(filePath).fileName());
    item->setData(0, kNodeKindRole, kKindFile);
    item->setData(0, kPathRole, filePath);
    QFileIconProvider iconProvider;
    item->setIcon(0, iconProvider.icon(QFileInfo(filePath)));
    return item;
}

void ProjectPanelDock::load()
{
    m_tree->clear();
    const ProjectWorkspace ws = ProjectStore::load();

    // 遞迴建構節點（使用區域函式物件以捕捉自身）
    std::function<void(QTreeWidgetItem *, const ProjectNode &)> buildNode =
        [&](QTreeWidgetItem *parentItem, const ProjectNode &node) {
            QTreeWidgetItem *item = nullptr;
            if (node.type == ProjectNodeType::File) {
                item = addFileNode(parentItem, node.path.isEmpty() ? node.name : node.path);
                if (!node.name.isEmpty())
                    item->setText(0, node.name);
            } else {
                item = addFolderNode(parentItem, node.name, node.path);
            }
            for (const ProjectNode &child : node.children)
                buildNode(item, child);
        };

    for (const Project &p : ws.projects) {
        auto *projectItem = new QTreeWidgetItem(m_tree);
        projectItem->setText(0, p.name);
        projectItem->setData(0, kNodeKindRole, kKindProject);
        QFileIconProvider iconProvider;
        projectItem->setIcon(0, iconProvider.icon(QFileIconProvider::Drive));
        m_tree->addTopLevelItem(projectItem);
        for (const ProjectNode &root : p.roots)
            buildNode(projectItem, root);
        projectItem->setExpanded(true);
    }
}

static ProjectNode nodeFromItem(QTreeWidgetItem *item)
{
    ProjectNode node;
    node.type = (item->data(0, kNodeKindRole).toInt() == kKindFile) ? ProjectNodeType::File
                                                                     : ProjectNodeType::Folder;
    node.name = item->text(0);
    node.path = item->data(0, kPathRole).toString();
    for (int i = 0; i < item->childCount(); ++i)
        node.children.push_back(nodeFromItem(item->child(i)));
    return node;
}

bool ProjectPanelDock::save() const
{
    ProjectWorkspace ws;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *projectItem = m_tree->topLevelItem(i);
        Project p;
        p.name = projectItem->text(0);
        for (int j = 0; j < projectItem->childCount(); ++j)
            p.roots.push_back(nodeFromItem(projectItem->child(j)));
        ws.projects.push_back(p);
    }
    return ProjectStore::save(ws);
}

bool ProjectPanelDock::addProject(const QString &name)
{
    if (name.isEmpty() || findProjectItem(name))
        return false;
    if (m_tree->topLevelItemCount() >= ProjectStore::kMaxProjects)
        return false;
    auto *projectItem = new QTreeWidgetItem(m_tree);
    projectItem->setText(0, name);
    projectItem->setData(0, kNodeKindRole, kKindProject);
    QFileIconProvider iconProvider;
    projectItem->setIcon(0, iconProvider.icon(QFileIconProvider::Drive));
    m_tree->addTopLevelItem(projectItem);
    projectItem->setExpanded(true);
    return true;
}

void ProjectPanelDock::removeProject(const QString &name)
{
    QTreeWidgetItem *item = findProjectItem(name);
    if (!item)
        return;
    delete m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item));
}

QString ProjectPanelDock::activeProjectName() const
{
    QTreeWidgetItem *current = m_tree->currentItem();
    if (current) {
        QTreeWidgetItem *proj = projectItemOf(current);
        if (proj)
            return proj->text(0);
    }
    if (m_tree->topLevelItemCount() > 0)
        return m_tree->topLevelItem(0)->text(0);
    return QString();
}

void ProjectPanelDock::collectFiles(QTreeWidgetItem *item, QStringList *out) const
{
    if (!item || !out)
        return;
    if (isFileItem(item)) {
        const QString p = pathOf(item);
        if (!p.isEmpty())
            out->append(p);
        return;
    }
    for (int i = 0; i < item->childCount(); ++i)
        collectFiles(item->child(i), out);
}

QStringList ProjectPanelDock::filePathsForProject(const QString &projectName) const
{
    QStringList out;
    QTreeWidgetItem *projectItem =
        projectName.isEmpty() ? findProjectItem(activeProjectName()) : findProjectItem(projectName);
    if (!projectItem)
        return out;
    collectFiles(projectItem, &out);
    return out;
}

QStringList ProjectPanelDock::allFilePaths() const
{
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        collectFiles(m_tree->topLevelItem(i), &out);
    return out;
}

void ProjectPanelDock::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!isFileItem(item))
        return;
    const QString path = pathOf(item);
    if (!path.isEmpty())
        emit openFileRequested(path);
}

void ProjectPanelDock::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    QMenu menu(m_tree);

    QAction *addProjectAction = menu.addAction(tr("New Project…"));
    addProjectAction->setEnabled(m_tree->topLevelItemCount() < ProjectStore::kMaxProjects);
    connect(addProjectAction, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Project"), tr("Project name:"),
                                                     QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty())
            addProject(name);
    });

    if (item && !isFileItem(item)) {
        // Project 或 Folder 節點：可新增子節點
        menu.addSeparator();

        QAction *addFolderAction = menu.addAction(tr("Add Folder (Virtual)…"));
        connect(addFolderAction, &QAction::triggered, this, [this, item]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Add Folder"), tr("Folder name:"),
                                                         QLineEdit::Normal, QString(), &ok);
            if (ok && !name.isEmpty())
                addFolderNode(item, name);
        });

        QAction *addDiskFolderAction = menu.addAction(tr("Add Folder from Disk…"));
        connect(addDiskFolderAction, &QAction::triggered, this, [this, item]() {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder from Disk"));
            if (!dir.isEmpty())
                addFolderNode(item, QFileInfo(dir).fileName(), dir);
        });

        QAction *addFilesAction = menu.addAction(tr("Add Files…"));
        connect(addFilesAction, &QAction::triggered, this, [this, item]() {
            const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files"));
            for (const QString &f : files)
                addFileNode(item, f);
        });
    }

    if (item) {
        menu.addSeparator();

        QAction *renameAction = menu.addAction(tr("Rename…"));
        connect(renameAction, &QAction::triggered, this, [this, item]() {
            bool ok = false;
            const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                                            QLineEdit::Normal, item->text(0), &ok);
            if (ok && !newName.isEmpty())
                item->setText(0, newName);
        });

        QAction *removeAction = menu.addAction(isProjectItem(item) ? tr("Remove Project") : tr("Remove"));
        connect(removeAction, &QAction::triggered, this, [this, item]() {
            if (isProjectItem(item)) {
                delete m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item));
            } else if (item->parent()) {
                delete item->parent()->takeChild(item->parent()->indexOfChild(item));
            }
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

}  // namespace macpad::ui
