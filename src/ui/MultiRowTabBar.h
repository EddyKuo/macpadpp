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

    // 命中測試：多列模式用自算矩形，否則退回 QTabBar::tabAt。
    // （QTabBar::tabAt 非虛擬函式，呼叫端必須改呼叫此方法才會拿到正確結果。）
    int tabIndexAt(const QPoint &pos) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void tabLayoutChange() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    // 依目前寬度重排所有分頁矩形；同時把關閉鈕移到對應位置。
    void relayout();
    int rowHeight() const;

    bool m_multiRow = false;
    QVector<QRect> m_rects;   // 與分頁索引一一對應（僅多列模式有效）
    int m_rows = 1;
    int m_pressedIndex = -1;  // 拖曳換位用：按下時的分頁索引
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
