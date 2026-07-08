#pragma once

// CharacterPanel — 複刻 Notepad++「Edit ▸ Character Panel」（ASCII 插入面板）。
// 顯示 0..255 的十進位值、十六進位、字元、HTML 名稱、HTML 十進位／十六進位跳脫序列；
// 雙擊某列時依欄位發出 charChosen 供插入到編輯器。

#include <QDockWidget>
#include <QString>

class QTableWidget;
class QLabel;

namespace macpad::ui {

class CharacterPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit CharacterPanel(QWidget *parent = nullptr);

public slots:
    // 供 MainWindow 通知目前文件使用的編碼（ANSI / Unicode / UTF-8…），僅更新面板上的提示標籤。
    void setEncodingLabel(const QString &encoding);

signals:
    void charChosen(const QString &text);

protected:
    // 攔截表格的按鍵事件：Return/Enter 時比照雙擊插入目前選取的字元
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void emitForCell(int row, int column);

    QTableWidget *m_table = nullptr;
    QLabel *m_encodingLabel = nullptr;
};

}  // namespace macpad::ui
