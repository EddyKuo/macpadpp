#pragma once

// MultiRowTabBar — 支援真正多列換行的分頁列（複刻 Notepad++ 的 Multi-Line Tab Bar）
//
// 背景：QTabBar 的版面計算（QTabBarPrivate::layoutTabs）不是虛擬函式，也沒有提供
// 多列排版的開關，因此先前只能以「關閉捲動按鈕」best-effort 近似，分頁放不下時
// 仍然是被擠壓而非換行。本類別在啟用多列時改為自行計算每個分頁的矩形並自行繪製、
// 自行命中測試，達成真正的換行；停用時則整條路徑退回 QTabBar 原本的行為，
// 確保預設情境的風險為零。
//
// 已自行處理的互動：點選切換、中鍵關閉、雙擊、右鍵選單定位、拖曳換位、關閉鈕定位。

#include <QRect>
#include <QTabBar>
#include <QTabWidget>
#include <QVector>

namespace macpad::ui {

class MultiRowTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit MultiRowTabBar(QWidget *parent = nullptr);

    // 開啟/關閉多列模式。關閉時所有繪製與命中測試都交還給 QTabBar。
    void setMultiRow(bool on);
    bool isMultiRow() const { return m_multiRow; }

    // 單一分頁寬度上限（px），0 = 不限制。分頁很多時避免一個長檔名吃掉整條分頁列；
    // 單列模式下放不下的分頁改由 QTabBar 內建的左右捲動鈕捲動。
    void setMaxTabWidth(int px);
    int maxTabWidth() const { return m_maxTabWidth; }

    // 滾輪在分頁列上左右移動分頁（切到前/後一個，QTabBar 會自動把它捲進可視範圍）
    void setWheelScrollEnabled(bool on) { m_wheelScroll = on; }
    bool isWheelScrollEnabled() const { return m_wheelScroll; }

    // 所有分頁是否已放不下（單列模式＝需要捲動；多列模式＝已換行成兩列以上）。
    // 即時計算，不依賴事件是否已送達，呼叫端隨時問都拿得到正確答案。
    bool isOverflowing() const;

    // 命中測試：多列模式用自算矩形，否則退回 QTabBar::tabAt。
    // （QTabBar::tabAt 非虛擬函式，呼叫端必須改呼叫此方法才會拿到正確結果。）
    int tabIndexAt(const QPoint &pos) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // 放不下 ↔ 放得下 的狀態切換（供上層顯示/隱藏「分頁清單」按鈕）
    void overflowChanged(bool overflowing);

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;

    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void tabLayoutChange() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // 依目前寬度重排所有分頁矩形；同時把關閉鈕移到對應位置。
    void relayout();
    int rowHeight() const;
    // relayout() + 重算 overflow 狀態（必要時發出 overflowChanged）
    void refreshLayoutState();
    void updateOverflow();     // 重算並在狀態改變時發出 overflowChanged
    bool computeOverflow() const;
    bool isVerticalShape() const;
    // 切換到相對目前分頁 delta 個位置的分頁（不繞回，超出範圍即停在端點）
    void stepCurrentTab(int delta);

    bool m_multiRow = false;
    QVector<QRect> m_rects;   // 與分頁索引一一對應（僅多列模式有效）
    int m_rows = 1;
    int m_pressedIndex = -1;  // 拖曳換位用：按下時的分頁索引
    int m_maxTabWidth = 0;    // 0 = 不限制
    bool m_wheelScroll = false;
    bool m_overflowing = false;
    int m_wheelAccum = 0;     // 觸控板會送出很小的 delta，累積到一格（120）才換頁
};

// 安裝 MultiRowTabBar 的 QTabWidget。QTabWidget::setTabBar 是 protected，
// 只能由子類別呼叫——此類別存在的唯一理由就是這件事，其餘行為與 QTabWidget 相同。
class MultiRowTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit MultiRowTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        setTabBar(new MultiRowTabBar(this));
    }
};

}  // namespace macpad::ui
