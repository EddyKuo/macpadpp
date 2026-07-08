#pragma once

// CharacterPanel — 複刻 Notepad++「Edit ▸ Character Panel」（ASCII 插入面板）。
// 顯示 0..255 的十進位值、十六進位、字元；雙擊某列時發出 charChosen 供插入到編輯器。

#include <QDockWidget>

class QTableWidget;

namespace macpad::ui {

class CharacterPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit CharacterPanel(QWidget *parent = nullptr);

signals:
    void charChosen(const QString &text);

protected:
    // 攔截表格的按鍵事件：Return/Enter 時比照雙擊插入目前選取的字元
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTableWidget *m_table = nullptr;
};

}  // namespace macpad::ui
