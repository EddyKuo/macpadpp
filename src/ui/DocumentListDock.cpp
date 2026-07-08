#include "ui/DocumentListDock.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <numeric>

namespace macpad::ui {

DocumentListDock::DocumentListDock(QWidget *parent)
    : QDockWidget(tr("Documents"), parent)
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sortCheck = new QCheckBox(tr("Sort A-Z"), container);
    layout->addWidget(m_sortCheck);

    m_list = new QListWidget(container);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->viewport()->installEventFilter(this);
    layout->addWidget(m_list);

    setWidget(container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) {
            const int origIdx = displayRowToOriginal(row);
            if (origIdx >= 0)
                emit activated(origIdx);
        }
    });
    connect(m_sortCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_sortAsc = checked;
        rebuildDisplay();
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &DocumentListDock::showContextMenu);
}

void DocumentListDock::refresh(const QStringList &names, int current)
{
    refresh(names, QStringList(), current, QList<QColor>());
}

void DocumentListDock::refresh(const QStringList &names, const QStringList &paths, int current,
                                const QList<QColor> &colors)
{
    m_names = names;
    m_paths = paths;
    m_colors = colors;
    m_current = current;
    rebuildDisplay();
}

void DocumentListDock::rebuildDisplay()
{
    m_updating = true;
    m_list->clear();

    QVector<int> order(m_names.size());
    std::iota(order.begin(), order.end(), 0);
    if (m_sortAsc) {
        std::sort(order.begin(), order.end(), [this](int a, int b) {
            return m_names.at(a).compare(m_names.at(b), Qt::CaseInsensitive) < 0;
        });
    }
    m_displayToOriginal = order;

    for (int origIdx : order) {
        auto *item = new QListWidgetItem(m_names.at(origIdx));
        if (origIdx < m_colors.size() && m_colors.at(origIdx).isValid())
            item->setForeground(m_colors.at(origIdx));
        m_list->addItem(item);
    }

    int displayRow = -1;
    if (m_current >= 0)
        displayRow = m_displayToOriginal.indexOf(m_current);
    if (displayRow >= 0 && displayRow < m_list->count())
        m_list->setCurrentRow(displayRow);

    m_updating = false;
}

int DocumentListDock::displayRowToOriginal(int displayRow) const
{
    if (displayRow < 0 || displayRow >= m_displayToOriginal.size())
        return -1;
    return m_displayToOriginal.at(displayRow);
}

bool DocumentListDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            const QPoint pos = mouseEvent->position().toPoint();
            QListWidgetItem *item = m_list->itemAt(pos);
            if (item) {
                const int displayRow = m_list->row(item);
                const int origIdx = displayRowToOriginal(displayRow);
                if (origIdx >= 0)
                    emit closeRequested(origIdx);
            }
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void DocumentListDock::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const int displayRow = m_list->row(item);
    const int origIdx = displayRowToOriginal(displayRow);
    if (origIdx < 0)
        return;

    QMenu menu(m_list);
    QAction *closeAction = menu.addAction(tr("Close"));
    QAction *copyPathAction = menu.addAction(tr("Copy Path"));
    if (origIdx >= m_paths.size() || m_paths.at(origIdx).isEmpty())
        copyPathAction->setEnabled(false);

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == closeAction) {
        emit closeRequested(origIdx);
    } else if (chosen == copyPathAction) {
        QApplication::clipboard()->setText(m_paths.at(origIdx));
    }
}

}  // namespace macpad::ui
