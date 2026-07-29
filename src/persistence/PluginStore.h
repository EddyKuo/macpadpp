#pragma once

// PluginStore — 擴充（外掛）啟用狀態的持久化（複刻 Notepad++ Plugins Admin 的啟用/停用）
// 只記錄「被停用的 id」而非全部狀態：新加入的內建擴充預設即為啟用，
// 不需要每次都去回填設定檔，也不會因為設定檔沒跟上而讓新擴充被誤停用。
// 停用採 Notepad++ 語意——需重新啟動才生效（載入發生在啟動時）。

#include <QSet>
#include <QString>

namespace macpad::persistence {

class PluginStore {
public:
    // 已停用的擴充 id 集合；檔案不存在或解析失敗一律回傳空集合（＝全部啟用）
    static QSet<QString> disabledIds();

    // 覆寫已停用集合；成功回傳 true
    static bool setDisabledIds(const QSet<QString> &ids);

    // 便利查詢：該 id 是否為啟用狀態
    static bool isEnabled(const QString &id) { return !disabledIds().contains(id); }
};

}  // namespace macpad::persistence
