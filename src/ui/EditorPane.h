#pragma once

// EditorPane — 一個分頁的內容容器（FR-002 分割視窗）
// 主檢視（primary）持有檔案狀態；分割時新增次檢視共享同一 QsciDocument（同源、游標獨立）。
// 檔案操作（載入/儲存/編碼/書籤）一律走 primary；次檢視僅供閱讀對照。

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QSplitter;
class QsciScintilla;

namespace macpad::core { class EditorWidget; }

namespace macpad::ui {

class EditorPane : public QWidget {
    Q_OBJECT
public:
    explicit EditorPane(QWidget *parent = nullptr);

    // 建立一個「複製檢視」分頁（Dual-View 的 Clone to Other View）：
    // 本 pane 的 primary 編輯器與 source 共享同一 QsciDocument——內容即時同步，
    // 但游標/捲動/選取各自獨立（複刻 Notepad++ 的 Clone to Other View）。
    // 回傳的 pane 不持有檔案狀態（filePath 為空），標題透過 tabTitle() 追隨來源檔名。
    static EditorPane *makeClone(macpad::core::EditorWidget *source, QWidget *parent = nullptr);

    macpad::core::EditorWidget *primary() const { return m_primary; }
    bool isSplit() const { return m_secondary != nullptr; }

    // 是否為 clone 檢視（primary 與他人共享文件，不自行管理檔案）
    bool isClone() const { return m_isClone; }
    // 分頁標題：clone 追隨來源檔名（來源關閉後回退為自身顯示名）；否則用自身 primary 顯示名
    QString tabTitle() const;

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
    QList<QMetaObject::Connection> m_syncConns;  // 同步捲動連線；關閉分割時逐一斷開，避免懸空 QScrollBar 指標
    bool m_syncV = false;
    bool m_syncH = false;
    bool m_syncing = false;  // 防止相互 setValue 遞迴
    bool m_isClone = false;  // 由 makeClone 建立的複製檢視
    // clone 來源編輯器（供標題追隨；QPointer 於來源分頁關閉後自動歸零，避免懸空指標）
    QPointer<macpad::core::EditorWidget> m_cloneSource;
};

}  // namespace macpad::ui
