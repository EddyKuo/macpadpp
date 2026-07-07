#include "features/run/RunDock.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::features {

RunDock::RunDock(QWidget *parent)
    : QDockWidget(tr("Run"), parent)
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    m_command = new QLineEdit(root);
    m_command->setPlaceholderText(
        QStringLiteral("python \"$(FULL_CURRENT_PATH)\""));
    auto *runBtn = new QPushButton(tr("Run"), root);
    m_output = new QPlainTextEdit(root);
    m_output->setReadOnly(true);

    auto *top = new QWidget(root);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(m_command);
    topLayout->addWidget(runBtn);
    layout->addWidget(top);
    layout->addWidget(m_output);
    setWidget(root);

    connect(runBtn, &QPushButton::clicked, this, &RunDock::run);
    connect(m_command, &QLineEdit::returnPressed, this, &RunDock::run);
}

void RunDock::setCommand(const QString &cmd)
{
    if (m_command)
        m_command->setText(cmd);
}

void RunDock::run()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;  // 已有指令執行中

    const QString expanded = RunCommand::expand(m_command->text(), m_vars);
    const QStringList tokens = RunCommand::tokenize(expanded);
    if (tokens.isEmpty())
        return;

    m_output->appendPlainText(QStringLiteral("$ ") + expanded);

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            m_output->appendPlainText(QString::fromUtf8(m_process->readAll()).trimmed());
        });
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            m_output->appendPlainText(QStringLiteral("[exit %1]").arg(code));
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            m_output->appendPlainText(QStringLiteral("[error: ") + m_process->errorString()
                                      + QStringLiteral("]"));
        });
    }

    const QString program = tokens.first();
    const QStringList args = tokens.mid(1);
    if (!m_vars.currentDirectory.isEmpty())
        m_process->setWorkingDirectory(m_vars.currentDirectory);
    // 參數陣列啟動，非 shell 字串（CON-006，避免 injection）
    m_process->start(program, args);
}

}  // namespace macpad::features
