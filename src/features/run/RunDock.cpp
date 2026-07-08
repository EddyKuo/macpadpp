#include "features/run/RunDock.h"

#include <QAction>
#include <QCompleter>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStringListModel>
#include <QToolButton>
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

    m_historyCompleter = new QCompleter(m_history, m_command);
    m_historyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_historyCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_command->setCompleter(m_historyCompleter);

    m_browseBtn = new QToolButton(root);
    m_browseBtn->setText(tr("Browse…"));
    m_browseBtn->setToolTip(tr("選擇檔案並插入路徑"));

    m_varsBtn = new QToolButton(root);
    m_varsBtn->setText(tr("Variables"));
    m_varsBtn->setToolTip(tr("插入指令變數"));
    m_varsBtn->setPopupMode(QToolButton::InstantPopup);

    auto *varsMenu = new QMenu(m_varsBtn);
    struct VarEntry { const char *token; };
    static const VarEntry kVars[] = {
        {"$(FULL_CURRENT_PATH)"},
        {"$(CURRENT_DIRECTORY)"},
        {"$(FILE_NAME)"},
        {"$(NAME_PART)"},
        {"$(CURRENT_WORD)"},
    };
    for (const auto &entry : kVars) {
        const QString token = QString::fromLatin1(entry.token);
        QAction *action = varsMenu->addAction(token);
        connect(action, &QAction::triggered, this, [this, token] {
            insertVariableToken(token);
        });
    }
    m_varsBtn->setMenu(varsMenu);

    auto *runBtn = new QPushButton(tr("Run"), root);
    m_output = new QPlainTextEdit(root);
    m_output->setReadOnly(true);

    auto *top = new QWidget(root);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto *cmdRow = new QWidget(top);
    auto *cmdRowLayout = new QHBoxLayout(cmdRow);
    cmdRowLayout->setContentsMargins(0, 0, 0, 0);
    cmdRowLayout->addWidget(m_command);
    cmdRowLayout->addWidget(m_browseBtn);
    cmdRowLayout->addWidget(m_varsBtn);

    topLayout->addWidget(cmdRow);
    topLayout->addWidget(runBtn);
    layout->addWidget(top);
    layout->addWidget(m_output);
    setWidget(root);

    connect(runBtn, &QPushButton::clicked, this, &RunDock::run);
    connect(m_command, &QLineEdit::returnPressed, this, &RunDock::run);
    connect(m_browseBtn, &QToolButton::clicked, this, &RunDock::browseForFile);
}

void RunDock::setCommand(const QString &cmd)
{
    if (m_command)
        m_command->setText(cmd);
}

void RunDock::browseForFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("選擇檔案"));
    if (path.isEmpty())
        return;
    const QString token = path.contains(QLatin1Char(' '))
        ? QLatin1Char('"') + path + QLatin1Char('"')
        : path;
    insertAtCursor(token);
}

void RunDock::insertVariableToken(const QString &token)
{
    insertAtCursor(token);
}

void RunDock::insertAtCursor(const QString &text)
{
    if (!m_command)
        return;
    m_command->insert(text);
    m_command->setFocus();
}

void RunDock::addToHistory(const QString &cmd)
{
    if (cmd.isEmpty())
        return;
    m_history.removeAll(cmd);
    m_history.prepend(cmd);
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();
    if (m_historyCompleter)
        m_historyCompleter->setModel(new QStringListModel(m_history, m_historyCompleter));
}

void RunDock::run()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;  // 已有指令執行中

    // 先對原始樣板 tokenize（尊重使用者在樣板中的引號），再逐 token 展開變數：
    // 已展開的值不會再被重新切割，含空白或引號的值可安全保留為單一 argv。
    const QString rawCommand = m_command->text();
    const QStringList rawTokens = RunCommand::tokenize(rawCommand);
    QStringList tokens;
    tokens.reserve(rawTokens.size());
    for (const QString &t : rawTokens)
        tokens << RunCommand::expand(t, m_vars);
    if (tokens.isEmpty())
        return;

    addToHistory(rawCommand.trimmed());

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
