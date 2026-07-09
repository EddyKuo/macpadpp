#pragma once

// SessionStore — session.json：開啟分頁、游標/捲動位置、作用分頁（FR-016, DR-002）
// 檔案損毀時 load 回傳空 session（不崩潰）。

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::persistence {

struct TabState {
    QString path;
    int line = 0;
    int index = 0;             // 游標欄
    int firstVisibleLine = 0;  // 捲動位置
    // FR-052：選取範圍，編碼為 "anchorLine,anchorIndex,caretLine,caretIndex"，無選取則為空字串
    QString selection;
    QList<int> bookmarks;      // FR-052：已加書籤的行號
    QString languageOverride;  // FR-052：詞法分析器 key，空字串表示自動偵測
    int view = 0;              // FR-062：分頁所屬檢視（0=主檢視、1=第二檢視），還原時歸位
    // Notepad++ session 快照：未存內容跨重啟保留（含未命名 untitled 緩衝與 dirty 已命名檔）
    bool untitled = false;     // 此分頁為未命名（無檔）的未存緩衝區
    bool dirty = false;        // 有未存變更（還原後維持 dirty 標記）
    QString unsavedContent;    // dirty 時的完整緩衝內容：untitled 用以重建、named 用以覆蓋磁碟版
};

struct SessionState {
    int activeIndex = 0;
    QVector<TabState> tabs;
};

class SessionStore {
public:
    // 預設（自動）session
    static SessionState load();
    static bool save(const SessionState &state);

    // 具名 session（存於設定 sessions/{name}.json）——FR-016 進階
    static bool saveNamed(const QString &name, const SessionState &state);
    static SessionState loadNamed(const QString &name);
    static QStringList listNames();

    // 任意路徑的 session 檔（供命令列 -openSession <file>）；檔案不存在/損毀回傳空 session
    static SessionState loadFromPath(const QString &path);

    static constexpr int kSchemaVersion = 1;
};

}  // namespace macpad::persistence
