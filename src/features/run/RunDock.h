#pragma once

// RunDock — 執行外部指令面板（FR-031, IR-003）
// 以 QProcess 參數陣列啟動（禁 shell 拼接，CON-006）；輸出即時顯示。

#include <QDockWidget>

#include "features/run/RunCommand.h"

class QLineEdit;
class QPlainTextEdit;
class QProcess;

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

private:
    QLineEdit *m_command = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QProcess *m_process = nullptr;
    RunVars m_vars;
};

}  // namespace macpad::features
