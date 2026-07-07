#include "ui/DocumentListDock.h"

#include <QListWidget>

namespace macpad::ui {

DocumentListDock::DocumentListDock(QWidget *parent)
    : QDockWidget(tr("Documents"), parent)
{
    m_list = new QListWidget(this);
    setWidget(m_list);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0)
            emit activated(row);
    });
}

void DocumentListDock::refresh(const QStringList &names, int current)
{
    m_updating = true;
    m_list->clear();
    m_list->addItems(names);
    if (current >= 0 && current < names.size())
        m_list->setCurrentRow(current);
    m_updating = false;
}

}  // namespace macpad::ui
