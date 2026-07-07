#include "extension/builtin/WordCountExtension.h"

#include "core/EditorWidget.h"

#include <QCoreApplication>
#include <QRegularExpression>

// 外掛選單/訊息以 "Extensions" context 翻譯（IExtension 非 QObject,不能用 tr()）
static QString xtr(const char *s) { return QCoreApplication::translate("Extensions", s); }

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
    m_host->addMenuAction(QStringLiteral("Edit"), xtr("Word Count"), [this] {
        auto *editor = m_host->activeEditor();
        if (!editor) {
            m_host->showStatusMessage(xtr("No active editor"));
            return;
        }
        const QString text = editor->text();
        // 以 Unicode 碼位計算字元數（跳過低代理項,避免將代理對計為 2 個字元）
        int chars = 0;
        for (const QChar c : text) {
            if (!c.isLowSurrogate())
                ++chars;
        }
        static const QRegularExpression ws(QStringLiteral("\\s+"));
        const QStringList words = text.split(ws, Qt::SkipEmptyParts);
        const int lineCount = editor->lines();
        m_host->showStatusMessage(
            xtr("Words: %1  Chars: %2  Lines: %3")
                .arg(words.size()).arg(chars).arg(lineCount),
            5000);
    });
}

void WordCountExtension::onUnload()
{
    m_host = nullptr;
}

}  // namespace macpad::extension
