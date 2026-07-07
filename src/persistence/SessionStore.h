#pragma once

// SessionStore — session.json：開啟分頁、游標/捲動位置、作用分頁（FR-016, DR-002）
// 檔案損毀時 load 回傳空 session（不崩潰）。

#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::persistence {

struct TabState {
    QString path;
    int line = 0;
    int index = 0;             // 游標欄
    int firstVisibleLine = 0;  // 捲動位置
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

    static constexpr int kSchemaVersion = 1;
};

}  // namespace macpad::persistence
