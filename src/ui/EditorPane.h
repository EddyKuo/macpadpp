#pragma once

// EditorPane — 一個分頁的內容容器（FR-002 分割視窗）
// 主檢視（primary）持有檔案狀態；分割時新增次檢視共享同一 QsciDocument（同源、游標獨立）。
// 檔案操作（載入/儲存/編碼/書籤）一律走 primary；次檢視僅供閱讀對照。

#include <QWidget>

class QSplitter;
class QsciScintilla;

namespace macpad::core { class EditorWidget; }

namespace macpad::ui {

class EditorPane : public QWidget {
    Q_OBJECT
public:
    explicit EditorPane(QWidget *parent = nullptr);

    macpad::core::EditorWidget *primary() const { return m_primary; }
    bool isSplit() const { return m_secondary != nullptr; }

    // 切換水平分割（FR-002）：開啟時新增共享文件的次檢視；再次呼叫關閉
    void toggleSplit();

    // 分割視窗同步捲動（複刻 Notepad++ Synchronize Scrolling）
    void setSyncVerticalScroll(bool on);
    void setSyncHorizontalScroll(bool on);
    bool syncVerticalScroll() const { return m_syncV; }
    bool syncHorizontalScroll() const { return m_syncH; }

private:
    void wireSyncScroll();   // 於分割開啟時連接兩檢視捲軸

    QSplitter *m_splitter = nullptr;
    macpad::core::EditorWidget *m_primary = nullptr;
    QsciScintilla *m_secondary = nullptr;
    bool m_syncV = false;
    bool m_syncH = false;
    bool m_syncing = false;  // 防止相互 setValue 遞迴
};

}  // namespace macpad::ui
