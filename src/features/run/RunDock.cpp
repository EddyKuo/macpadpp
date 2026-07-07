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

    // 先對原始樣板 tokenize（尊重使用者在樣板中的引號），再逐 token 展開變數：
    // 已展開的值不會再被重新切割，含空白或引號的值可安全保留為單一 argv。
    const QStringList rawTokens = RunCommand::tokenize(m_command->text());
    QStringList tokens;
    tokens.reserve(rawTokens.size());
    for (const QString &t : rawTokens)
        tokens << RunCommand::expand(t, m_vars);
    if (tokens.isEmpty())
        return;

    m_output->appendPlainText(QStringLiteral("$ ") + tokens.join(QLatin1Char(' ')));

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            m_output->appendPlainText(QString::fromUtf8(m_process->readAll()).trimmed());
        });
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            m_output->appendPlainText(tr("[exit %1]").arg(code));
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            m_output->appendPlainText(tr("[error: %1]").arg(m_process->errorString()));
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
