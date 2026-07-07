#pragma once

// RunCommand — 外部指令變數展開與安全 tokenize（FR-031, CON-006/IR-003）
// 純邏輯、可單元測試。展開 $(FULL_CURRENT_PATH) 等變數；tokenize 尊重引號，
// 供 QProcess 以參數陣列啟動（避免 shell injection）。

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

}  // namespace macpad::features
