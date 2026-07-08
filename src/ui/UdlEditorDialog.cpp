#include "ui/UdlEditorDialog.h"

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::ui {

using macpad::features::kUdlMaxKeywordGroups;

UdlEditorDialog::UdlEditorDialog(macpad::features::UdlManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Define Your Language"));
    resize(560, 560);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    // --- 一般設定分頁 ---
    auto *generalPage = new QWidget(tabs);
    auto *form = new QFormLayout(generalPage);

    m_name = new QLineEdit(generalPage);
    m_name->setPlaceholderText(tr("My Language"));
    m_extensions = new QLineEdit(generalPage);
    m_extensions->setPlaceholderText(tr("以空白或逗號分隔，不含點，如：foo bar"));
    m_lineComment = new QLineEdit(generalPage);
    m_lineComment->setPlaceholderText(QStringLiteral("//"));
    m_blockStart = new QLineEdit(generalPage);
    m_blockStart->setPlaceholderText(QStringLiteral("/*"));
    m_blockEnd = new QLineEdit(generalPage);
    m_blockEnd->setPlaceholderText(QStringLiteral("*/"));
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), generalPage);
    m_caseSensitive->setChecked(true);

    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Extensions:"), m_extensions);
    form->addRow(tr("Line comment:"), m_lineComment);
    form->addRow(tr("Block comment start:"), m_blockStart);
    form->addRow(tr("Block comment end:"), m_blockEnd);
    form->addRow(QString(), m_caseSensitive);
    tabs->addTab(generalPage, tr("General"));

    // --- 關鍵字分頁（8 組，FR-059） ---
    auto *keywordsPage = new QWidget(tabs);
    auto *kwForm = new QFormLayout(keywordsPage);
    for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
        auto *edit = new QPlainTextEdit(keywordsPage);
        edit->setPlaceholderText(tr("關鍵字，以空白或換行分隔"));
        edit->setMaximumHeight(60);
        kwForm->addRow(tr("Group %1:").arg(i + 1), edit);
        m_keywordGroups.push_back(edit);
    }
    tabs->addTab(keywordsPage, tr("Keywords"));

    // --- 運算子/分隔符/摺疊符分頁（FR-059） ---
    auto *advancedPage = new QWidget(tabs);
    auto *advForm = new QFormLayout(advancedPage);
    m_operators = new QPlainTextEdit(advancedPage);
    m_operators->setPlaceholderText(tr("運算子，以空白或換行分隔，如：+ - == !="));
    m_operators->setMaximumHeight(70);
    advForm->addRow(tr("Operators:"), m_operators);

    m_delimiters = new QPlainTextEdit(advancedPage);
    m_delimiters->setPlaceholderText(tr("每行一組，格式：開始|跳脫|結束，如：\"|\\|\""));
    m_delimiters->setMaximumHeight(70);
    advForm->addRow(tr("Delimiters:"), m_delimiters);

    m_folderOpen = new QLineEdit(advancedPage);
    m_folderOpen->setPlaceholderText(QStringLiteral("{"));
    m_folderMiddle = new QLineEdit(advancedPage);
    m_folderClose = new QLineEdit(advancedPage);
    m_folderClose->setPlaceholderText(QStringLiteral("}"));
    advForm->addRow(tr("Folder open:"), m_folderOpen);
    advForm->addRow(tr("Folder middle:"), m_folderMiddle);
    advForm->addRow(tr("Folder close:"), m_folderClose);
    tabs->addTab(advancedPage, tr("Operators / Delimiters / Folding"));

    root->addWidget(tabs);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *exportBtn = box->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &UdlEditorDialog::saveDefinition);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exportBtn, &QPushButton::clicked, this, &UdlEditorDialog::exportDefinition);
}

macpad::features::UdlDefinition UdlEditorDialog::collectDefinition() const
{
    macpad::features::UdlDefinition def;
    def.name = m_name->text().trimmed();
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    for (const QString &ext : m_extensions->text().split(sep, Qt::SkipEmptyParts))
        def.extensions << ext.toLower();

    def.keywordGroups.resize(kUdlMaxKeywordGroups);
    for (int i = 0; i < kUdlMaxKeywordGroups && i < m_keywordGroups.size(); ++i) {
        for (const QString &kw : m_keywordGroups.at(i)->toPlainText().split(sep, Qt::SkipEmptyParts))
            def.keywordGroups[i].insert(kw);
    }
    def.keywords = def.keywordGroups.at(0);

    for (const QString &op : m_operators->toPlainText().split(sep, Qt::SkipEmptyParts))
        def.operators.insert(op);

    const auto delimLines = m_delimiters->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : delimLines) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.isEmpty() || parts.at(0).isEmpty())
            continue;
        macpad::features::UdlDelimiter del;
        del.open = parts.at(0);
        del.escape = parts.size() > 1 ? parts.at(1) : QString();
        del.close = parts.size() > 2 ? parts.at(2) : QString();
        def.delimiters.push_back(del);
    }

    def.folderTokens.open = m_folderOpen->text();
    def.folderTokens.middle = m_folderMiddle->text();
    def.folderTokens.close = m_folderClose->text();

    def.lineComment = m_lineComment->text();
    def.blockCommentStart = m_blockStart->text();
    def.blockCommentEnd = m_blockEnd->text();
    def.caseSensitive = m_caseSensitive->isChecked();
    return def;
}

void UdlEditorDialog::saveDefinition()
{
    const macpad::features::UdlDefinition def = collectDefinition();
    if (def.name.isEmpty()) {
        QMessageBox::warning(this, tr("Define Your Language"), tr("請輸入語言名稱。"));
        return;
    }

    if (m_manager && m_manager->save(def))
        accept();
    else
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法儲存 UDL。"));
}

void UdlEditorDialog::exportDefinition()
{
    const macpad::features::UdlDefinition def = collectDefinition();
    if (def.name.isEmpty()) {
        QMessageBox::warning(this, tr("Define Your Language"), tr("請輸入語言名稱。"));
        return;
    }
    if (!m_manager || !m_manager->save(def)) {
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法儲存 UDL。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export User Defined Language"),
        def.name + QStringLiteral(".json"), tr("UDL JSON (*.json)"));
    if (path.isEmpty())
        return;

    if (!m_manager->exportToFile(def.name, path))
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法匯出 UDL。"));
}

}  // namespace macpad::ui
