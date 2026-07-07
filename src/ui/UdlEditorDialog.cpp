#include "ui/UdlEditorDialog.h"

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace macpad::ui {

UdlEditorDialog::UdlEditorDialog(macpad::features::UdlManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Define Your Language"));
    resize(480, 460);
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("My Language"));
    m_extensions = new QLineEdit(this);
    m_extensions->setPlaceholderText(tr("以空白或逗號分隔，不含點，如：foo bar"));
    m_keywords = new QPlainTextEdit(this);
    m_keywords->setPlaceholderText(tr("關鍵字，以空白或換行分隔"));
    m_keywords->setMinimumHeight(140);
    m_lineComment = new QLineEdit(this);
    m_lineComment->setPlaceholderText(QStringLiteral("//"));
    m_blockStart = new QLineEdit(this);
    m_blockStart->setPlaceholderText(QStringLiteral("/*"));
    m_blockEnd = new QLineEdit(this);
    m_blockEnd->setPlaceholderText(QStringLiteral("*/"));
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_caseSensitive->setChecked(true);

    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Extensions:"), m_extensions);
    form->addRow(tr("Keywords:"), m_keywords);
    form->addRow(tr("Line comment:"), m_lineComment);
    form->addRow(tr("Block comment start:"), m_blockStart);
    form->addRow(tr("Block comment end:"), m_blockEnd);
    form->addRow(QString(), m_caseSensitive);
    root->addLayout(form);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &UdlEditorDialog::saveDefinition);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void UdlEditorDialog::saveDefinition()
{
    macpad::features::UdlDefinition def;
    def.name = m_name->text().trimmed();
    if (def.name.isEmpty()) {
        QMessageBox::warning(this, tr("Define Your Language"), tr("請輸入語言名稱。"));
        return;
    }
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    for (const QString &ext : m_extensions->text().split(sep, Qt::SkipEmptyParts))
        def.extensions << ext.toLower();
    for (const QString &kw : m_keywords->toPlainText().split(sep, Qt::SkipEmptyParts))
        def.keywords.insert(kw);
    def.lineComment = m_lineComment->text();
    def.blockCommentStart = m_blockStart->text();
    def.blockCommentEnd = m_blockEnd->text();
    def.caseSensitive = m_caseSensitive->isChecked();

    if (m_manager && m_manager->save(def))
        accept();
    else
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法儲存 UDL。"));
}

}  // namespace macpad::ui
