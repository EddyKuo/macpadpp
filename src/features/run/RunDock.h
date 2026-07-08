#pragma once

// RunDock — 執行外部指令面板（FR-031, IR-003）
// 以 QProcess 參數陣列啟動（禁 shell 拼接，CON-006）；輸出即時顯示。

#include <QDockWidget>
#include <QStringList>

#include "features/run/RunCommand.h"

class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QCompleter;
class QToolButton;

namespace macpad::features {

class RunDock : public QDockWidget {
    Q_OBJECT
public:
    explicit RunDock(QWidget *parent = nullptr);

    void setVars(const RunVars &vars) { m_vars = vars; }

    // 設定命令列文字（供 Saved Commands 直接帶入）
    void setCommand(const QString &cmd);
    // 帶入命令並立即執行
    void runCommand(const QString &cmd) { setCommand(cmd); run(); }

public slots:
    void run();

private slots:
    // 「Browse…」：開啟檔案選擇對話框，將路徑插入命令列（含空白則加引號）
    void browseForFile();
    // 「Variables」選單：插入所選變數 token 至命令列游標處
    void insertVariableToken(const QString &token);

private:
    // 將指令加入歷史清單（去重、最新在前、上限 20 筆）並更新補全清單
    void addToHistory(const QString &cmd);
    // 在游標處插入文字（無選取時取代選取範圍）
    void insertAtCursor(const QString &text);

    QLineEdit *m_command = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QProcess *m_process = nullptr;
    RunVars m_vars;

    QStringList m_history;
    QCompleter *m_historyCompleter = nullptr;
    QToolButton *m_browseBtn = nullptr;
    QToolButton *m_varsBtn = nullptr;

    static constexpr int kMaxHistory = 20;
};

}  // namespace macpad::features
