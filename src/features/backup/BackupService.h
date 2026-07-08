#pragma once

// BackupService — 非破壞性備份 + 當機復原（FR-054）
// - backupOnSave：儲存時於原檔旁（或指定目錄）寫出 .bak 備份，Simple 覆寫單一備份、
//   Verbose 每次另存帶時間戳記的備份（時間戳記由呼叫端傳入，確保可測試/決定性）。
// - Snapshot：定期將未儲存內容寫入設定目錄 snapshots/，供下次啟動時偵測未預期關閉並復原；
//   正常關閉時應呼叫 clearSnapshots() 清空。

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace macpad::features {

enum class BackupMode {
    None,     // 不備份
    Simple,   // 覆寫單一 "<file>.bak"
    Verbose,  // 每次另存 "<name>.<timestamp>.bak"
};

class BackupService {
public:
    // 依 mode 於儲存時寫出備份檔。
    // customDir 非空時備份寫入該目錄（檔名仍依原檔名推導），否則寫在原檔旁。
    // timestamp 僅 Verbose 模式使用，由呼叫端提供（避免依賴系統時間，確保可測試）。
    static bool backupOnSave(const QString &filePath, const QByteArray &contentBytes, BackupMode mode,
                              const QString &customDir = QString(), const QString &timestamp = QString());

    // 將未儲存內容寫入快照（sessionKey 區分不同 session/視窗，docId 區分同 session 內不同分頁）
    static bool writeSnapshot(const QString &sessionKey, const QString &docId, const QByteArray &content);
    // 目前所有待復原快照的識別碼列表（"sessionKey/docId"）
    static QStringList pendingSnapshots();
    // 讀回指定識別碼的快照內容；不存在則回傳空 QByteArray
    static QByteArray readSnapshot(const QString &id);
    // 清空所有快照（正常關閉時呼叫）
    static void clearSnapshots();
};

}  // namespace macpad::features
