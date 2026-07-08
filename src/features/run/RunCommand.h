#pragma once

// RunCommand — 外部指令變數展開與安全 tokenize（FR-031, CON-006/IR-003）
// 純邏輯、可單元測試。展開 $(FULL_CURRENT_PATH) 等變數；tokenize 尊重引號，
// 供 QProcess 以參數陣列啟動（避免 shell injection）。

#include <QList>
#include <QString>
#include <QStringList>

namespace macpad::features {

struct RunVars {
    QString fullCurrentPath;   // $(FULL_CURRENT_PATH)
    QString currentDirectory;  // $(CURRENT_DIRECTORY)
    QString fileName;          // $(FILE_NAME)
    QString nameNoExt;         // $(NAME_PART)
    QString currentWord;       // $(CURRENT_WORD)
};

class RunCommand {
public:
    static QString expand(const QString &command, const RunVars &vars);

    // 依空白切割並尊重雙引號；回傳 [program, arg1, arg2, ...]
    static QStringList tokenize(const QString &command);
};

// 一筆已儲存的 Run 指令（Run 選單 > Run... 對話框「Save」後的項目）
struct RunCommandEntry {
    QString name;      // 顯示名稱（亦作為 run_commands.json 中的識別鍵）
    QString command;   // 原始指令字串（含未展開的變數，如 $(FULL_CURRENT_PATH)）
    QString shortcut;  // 選填快捷鍵文字（QKeySequence::toString() 格式，如 "Ctrl+Shift+F5"）；空字串＝未設定
};

// RunCommandStore — run_commands.json 讀寫（FR-031 延伸：已儲存指令的選填快捷鍵）
// 與 persistence/RecentFiles 相同模式：schema_version + items 陣列；缺 shortcut 欄位時預設空字串（向下相容）。
class RunCommandStore {
public:
    static QList<RunCommandEntry> load();
    static bool save(const QList<RunCommandEntry> &entries);  // 回傳寫檔是否成功

    static constexpr int kSchemaVersion = 1;
};

}  // namespace macpad::features
