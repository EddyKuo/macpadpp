#include "ui/WorkspaceDock.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QTreeView>

namespace macpad::ui {

WorkspaceDock::WorkspaceDock(QWidget *parent)
    : QDockWidget(tr("Workspace"), parent)
{
    m_model = new QFileSystemModel(this);
    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    // 只顯示檔名欄
    for (int c = 1; c < m_model->columnCount(); ++c)
        m_tree->hideColumn(c);
    m_tree->setHeaderHidden(true);
    setWidget(m_tree);

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
        const QString path = m_model->filePath(idx);
        if (QFileInfo(path).isFile())
            emit fileActivated(path);
    });
}

void WorkspaceDock::setRoot(const QString &dir)
{
    m_model->setRootPath(dir);
    m_tree->setRootIndex(m_model->index(dir));
}

}  // namespace macpad::ui
