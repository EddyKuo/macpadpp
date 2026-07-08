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

    // 編碼感知顯示（複刻 NP++ Character Panel）：切換 Unicode / ANSI 模式，
    // 影響 128..255 區段「Character」欄位顯示。與 setEncodingLabel 各自獨立、互不影響，
    // 上層通常同時呼叫兩者（文字標籤 + 實際顯示模式）。
    // unicode=true：128..255 視為系統預設 codepage（見 setCodepage）的單一位元組，顯示其映射字元。
    // unicode=false（ANSI/Latin-1）：顯示完整 8-bit 字元集（位元組值＝Unicode code point，即目前預設行為）。
    void setUnicodeMode(bool unicode);
    // 設定 Unicode 模式下 128..255 對應用的系統預設 codepage 名稱（如 "Windows-1252"）；
    // 僅在 unicode 模式下生效。codepage 不存在時安全回退為 Latin-1 顯示（不崩潰）。
    void setCodepage(const QString &codepageName);

signals:
    void charChosen(const QString &text);

protected:
    // 攔截表格的按鍵事件：Return/Enter 時比照雙擊插入目前選取的字元
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void emitForCell(int row, int column);
    // 依目前 m_unicodeMode/m_codepageName 重繪 128..255 列的「Character」欄位
    void rebuildHighRangeColumn();

    QTableWidget *m_table = nullptr;
    QLabel *m_encodingLabel = nullptr;
    bool m_unicodeMode = false;  // 預設維持既有行為（完整 8-bit 字元集）
    QString m_codepageName = QStringLiteral("Windows-1252");  // 系統預設 codepage 近似值
};

}  // namespace macpad::ui
