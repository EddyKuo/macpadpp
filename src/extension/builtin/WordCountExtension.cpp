#include "extension/builtin/WordCountExtension.h"

#include "core/EditorWidget.h"

#include <QRegularExpression>

namespace macpad::extension {

ExtensionCapabilities WordCountExtension::capabilities() const
{
    return {QStringLiteral("builtin.wordcount"),
            QStringLiteral("Word Count"),
            QStringLiteral("1.0.0")};
}

void WordCountExtension::onLoad(IHostServices *host)
{
    m_host = host;
    // 透過協定在 Edit 選單掛一個動作（不直接碰 MainWindow 內部）
    m_host->addMenuAction(QStringLiteral("Edit"), QStringLiteral("Word Count"), [this] {
        auto *editor = m_host->activeEditor();
        if (!editor) {
            m_host->showStatusMessage(QStringLiteral("No active editor"));
            return;
        }
        const QString text = editor->text();
        const int chars = static_cast<int>(text.size());
        static const QRegularExpression ws(QStringLiteral("\\s+"));
        const QStringList words = text.split(ws, Qt::SkipEmptyParts);
        const int lineCount = editor->lines();
        m_host->showStatusMessage(
            QStringLiteral("Words: %1  Chars: %2  Lines: %3")
                .arg(words.size()).arg(chars).arg(lineCount),
            5000);
    });
}

void WordCountExtension::onUnload()
{
    m_host = nullptr;
}

}  // namespace macpad::extension
