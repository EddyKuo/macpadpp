#include "ui/MultiRowTabBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QWheelEvent>

namespace macpad::ui {

namespace {
constexpr int kHPadding = 4;   // 分頁之間的水平間距
constexpr int kVPadding = 2;   // 列與列之間的垂直間距
}  // namespace

MultiRowTabBar::MultiRowTabBar(QWidget *parent) : QTabBar(parent)
{
    // macOS 的樣式 hint（SH_TabBar_PreferNoArrows）預設不給捲動箭頭，QTabBar 於是改成把
    // 分頁一路擠窄，分頁一多每個標籤都變成「a_r…」而認不出是哪個檔案。這裡明確要求捲動鈕：
    // 放不下就左右捲動（滾輪同樣可捲），而不是全部擠成一團。多列模式會再關掉它。
    setUsesScrollButtons(true);
}

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
    refreshLayoutState();
    updateGeometry();
    update();
}

void MultiRowTabBar::setMaxTabWidth(int px)
{
    const int v = qMax(0, px);
    if (m_maxTabWidth == v)
        return;
    m_maxTabWidth = v;
    // QTabBar 的版面（QTabBarPrivate::layoutTabs）只在插入/移除/resize/字型變更等時機重算，
    // 沒有公開的「重新排版」入口；setElideMode 是少數無條件觸發 refresh() 的 setter，
    // 這裡以同值呼叫強迫單列版面重新套用新的 tabSizeHint。
    setElideMode(elideMode());
    refreshLayoutState();
    updateGeometry();
    update();
}

bool MultiRowTabBar::isVerticalShape() const
{
    switch (shape()) {
    case QTabBar::RoundedWest:
    case QTabBar::RoundedEast:
    case QTabBar::TriangularWest:
    case QTabBar::TriangularEast:
        return true;
    default:
        return false;
    }
}

// 寬度上限只對水平分頁列有意義；垂直排列時 tabSizeHint 的 width 是分頁列厚度，
// 夾它只會把整條分頁列壓扁。
QSize MultiRowTabBar::tabSizeHint(int index) const
{
    QSize s = QTabBar::tabSizeHint(index);
    if (m_maxTabWidth > 0 && !isVerticalShape() && s.width() > m_maxTabWidth)
        s.setWidth(m_maxTabWidth);
    return s;
}

// QTabBar 的版面會先把分頁從 tabSizeHint 一路壓到 minimumTabSizeHint，壓到不能再壓才
// 出現捲動鈕——預設的最小值極窄，分頁一多每個標籤都變成「a_r…」而認不出是哪個檔案。
// 設了寬度上限時就把最小值提高到上限，讓分頁維持可讀寬度，放不下的改用捲動鈕/滾輪/分頁清單。
QSize MultiRowTabBar::minimumTabSizeHint(int index) const
{
    if (m_maxTabWidth <= 0 || isVerticalShape())
        return QTabBar::minimumTabSizeHint(index);
    return tabSizeHint(index);
}

int MultiRowTabBar::rowHeight() const
{
    int h = 0;
    for (int i = 0; i < count(); ++i)
        h = qMax(h, tabSizeHint(i).height());
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
        int w = tabSizeHint(i).width();
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

void MultiRowTabBar::refreshLayoutState()
{
    relayout();
    updateOverflow();
}

// 放不下的判定：多列模式看是否真的排成兩列以上；單列模式把所有分頁的理想尺寸加起來
// 跟分頁列比，超過即代表 QTabBar 會啟用捲動鈕（使用者需要「左右移動」的時機）。
bool MultiRowTabBar::computeOverflow() const
{
    if (count() == 0)
        return false;
    if (m_multiRow)
        return m_rows > 1;
    int total = 0;
    const bool vertical = isVerticalShape();
    for (int i = 0; i < count(); ++i) {
        const QSize s = tabSizeHint(i);
        total += vertical ? s.height() : s.width();
    }
    return total > (vertical ? height() : width());
}

bool MultiRowTabBar::isOverflowing() const
{
    return computeOverflow();
}

void MultiRowTabBar::updateOverflow()
{
    const bool of = computeOverflow();
    if (of == m_overflowing)
        return;
    m_overflowing = of;
    emit overflowChanged(of);
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
    if (!m_multiRow) {
        updateOverflow();   // 單列模式：視窗變窄/變寬會改變「放不放得下」
        return;
    }
    const int before = m_rows;
    refreshLayoutState();
    if (m_rows != before)
        updateGeometry();   // 列數改變會改變高度，必須讓上層重新配置
    update();
}

void MultiRowTabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);
    if (m_multiRow) {
        refreshLayoutState();
        updateGeometry();
    } else {
        updateOverflow();
    }
}

void MultiRowTabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);
    if (m_multiRow) {
        refreshLayoutState();
        updateGeometry();
    } else {
        updateOverflow();
    }
}

void MultiRowTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    if (m_multiRow) {
        refreshLayoutState();
        updateGeometry();
        update();
    } else {
        updateOverflow();
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

void MultiRowTabBar::stepCurrentTab(int delta)
{
    if (count() < 2)
        return;
    const int target = qBound(0, currentIndex() + delta, count() - 1);
    if (target != currentIndex())
        setCurrentIndex(target);   // QTabBar 會自動把新的目前分頁捲進可視範圍
}

// 在分頁列上滾動滾輪 = 左右移動分頁。垂直滾動與水平滾動（觸控板橫掃）都吃，
// 並累積到一個完整刻度（120）才換一頁，否則觸控板會一次跳過好幾個分頁。
void MultiRowTabBar::wheelEvent(QWheelEvent *event)
{
    if (!m_wheelScroll || count() < 2) {
        QTabBar::wheelEvent(event);
        return;
    }
    const QPoint d = event->angleDelta();
    const int delta = (qAbs(d.x()) > qAbs(d.y())) ? d.x() : d.y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    constexpr int kStep = 120;   // 一個標準滾輪刻度
    m_wheelAccum += delta;
    while (m_wheelAccum >= kStep) {
        m_wheelAccum -= kStep;
        stepCurrentTab(-1);      // 向上/向左 → 前一個分頁
    }
    while (m_wheelAccum <= -kStep) {
        m_wheelAccum += kStep;
        stepCurrentTab(1);
    }
    event->accept();
}

}  // namespace macpad::ui
