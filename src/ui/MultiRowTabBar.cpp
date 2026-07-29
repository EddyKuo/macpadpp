#include "ui/MultiRowTabBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOptionTab>
#include <QStylePainter>

namespace macpad::ui {

namespace {
constexpr int kHPadding = 4;   // 分頁之間的水平間距
constexpr int kVPadding = 2;   // 列與列之間的垂直間距
}  // namespace

MultiRowTabBar::MultiRowTabBar(QWidget *parent) : QTabBar(parent) {}

void MultiRowTabBar::setMultiRow(bool on)
{
    if (m_multiRow == on)
        return;
    m_multiRow = on;
    // 多列模式自行換行，不需要（也不該有）捲動按鈕
    setUsesScrollButtons(!on);
    // 多列模式下的拖曳換位由本類別自行處理；交給基底類別會用到它自己那套
    // 單列座標，結果會亂跳。
    setMovable(!on);
    relayout();
    updateGeometry();
    update();
}

int MultiRowTabBar::rowHeight() const
{
    int h = 0;
    for (int i = 0; i < count(); ++i)
        h = qMax(h, QTabBar::tabSizeHint(i).height());
    return h > 0 ? h : fontMetrics().height() + 8;
}

void MultiRowTabBar::relayout()
{
    m_rects.clear();
    m_rows = 1;
    if (!m_multiRow || count() == 0)
        return;

    const int avail = qMax(1, width());
    const int rh = rowHeight();
    int x = 0;
    int row = 0;
    m_rects.resize(count());
    for (int i = 0; i < count(); ++i) {
        int w = QTabBar::tabSizeHint(i).width();
        w = qMin(w, avail);                       // 單一分頁比整列還寬時就佔滿整列
        if (x > 0 && x + w > avail) {             // 放不下 → 換行（x>0 確保每列至少一個）
            ++row;
            x = 0;
        }
        m_rects[i] = QRect(x, row * (rh + kVPadding), w, rh);
        x += w + kHPadding;
    }
    m_rows = row + 1;

    // 關閉鈕是真實的子 widget，基底類別依它自己的單列座標擺放；多列模式必須自行移位，
    // 否則所有關閉鈕會疊在第一列。
    for (int i = 0; i < count(); ++i) {
        for (auto pos : {QTabBar::RightSide, QTabBar::LeftSide}) {
            QWidget *btn = tabButton(i, pos);
            if (!btn || !btn->isVisible())
                continue;
            const QRect r = m_rects.at(i);
            const int by = r.y() + (r.height() - btn->height()) / 2;
            const int bx = (pos == QTabBar::RightSide)
                               ? r.right() - btn->width() - 4
                               : r.left() + 4;
            btn->move(bx, by);
        }
    }
}

QSize MultiRowTabBar::sizeHint() const
{
    if (!m_multiRow)
        return QTabBar::sizeHint();
    const int rh = rowHeight();
    return QSize(QTabBar::sizeHint().width(), m_rows * rh + (m_rows - 1) * kVPadding);
}

QSize MultiRowTabBar::minimumSizeHint() const
{
    if (!m_multiRow)
        return QTabBar::minimumSizeHint();
    // 多列模式下不需要「至少容納一個完整分頁的寬度」，否則視窗縮不小
    return QSize(0, sizeHint().height());
}

void MultiRowTabBar::paintEvent(QPaintEvent *event)
{
    if (!m_multiRow) {
        QTabBar::paintEvent(event);
        return;
    }

    QStylePainter p(this);
    for (int i = 0; i < count() && i < m_rects.size(); ++i) {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);
        opt.rect = m_rects.at(i);
        // initStyleOption 依單列版面判定的位置/相鄰關係在多列下沒有意義，
        // 統一畫成「獨立分頁」，避免出現錯誤的接合圓角。
        opt.position = QStyleOptionTab::OnlyOneTab;
        opt.selectedPosition = QStyleOptionTab::NotAdjacent;
        p.drawControl(QStyle::CE_TabBarTab, opt);
    }
}

void MultiRowTabBar::resizeEvent(QResizeEvent *event)
{
    QTabBar::resizeEvent(event);
    if (!m_multiRow)
        return;
    const int before = m_rows;
    relayout();
    if (m_rows != before)
        updateGeometry();   // 列數改變會改變高度，必須讓上層重新配置
    update();
}

void MultiRowTabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);
    if (m_multiRow) {
        relayout();
        updateGeometry();
    }
}

void MultiRowTabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);
    if (m_multiRow) {
        relayout();
        updateGeometry();
    }
}

void MultiRowTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    if (m_multiRow) {
        relayout();
        updateGeometry();
        update();
    }
}

int MultiRowTabBar::tabIndexAt(const QPoint &pos) const
{
    if (!m_multiRow)
        return tabAt(pos);
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects.at(i).contains(pos))
            return i;
    return -1;
}

void MultiRowTabBar::mousePressEvent(QMouseEvent *event)
{
    if (!m_multiRow) {
        QTabBar::mousePressEvent(event);
        return;
    }
    const int idx = tabIndexAt(event->position().toPoint());
    m_pressedIndex = idx;
    if (idx >= 0 && event->button() == Qt::LeftButton)
        setCurrentIndex(idx);
    if (idx >= 0 && event->button() == Qt::MiddleButton)
        emit tabCloseRequested(idx);
    // 不呼叫基底類別：它會依自己的單列矩形再判一次，導致選到錯的分頁。
    event->accept();
}

void MultiRowTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_multiRow) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    // 拖曳換位：把按住的分頁移到游標所在的分頁位置
    if ((event->buttons() & Qt::LeftButton) && m_pressedIndex >= 0) {
        const int target = tabIndexAt(event->position().toPoint());
        if (target >= 0 && target != m_pressedIndex) {
            moveTab(m_pressedIndex, target);
            m_pressedIndex = target;
        }
    }
    event->accept();
}

void MultiRowTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_multiRow) {
        QTabBar::mouseReleaseEvent(event);
        return;
    }
    m_pressedIndex = -1;
    event->accept();
}

void MultiRowTabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_multiRow) {
        QTabBar::mouseDoubleClickEvent(event);
        return;
    }
    emit tabBarDoubleClicked(tabIndexAt(event->position().toPoint()));
    event->accept();
}

}  // namespace macpad::ui
