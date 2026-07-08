#include "ui/UdlEditorDialog.h"

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlLexer.h"
#include "features/udl/UdlManager.h"
#include "features/udl/UdlXmlIo.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::ui {

using macpad::features::kUdlMaxKeywordGroups;
using macpad::features::UdlLexer;
using macpad::features::UdlStyle;

UdlEditorDialog::UdlEditorDialog(macpad::features::UdlManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Define Your Language"));
    resize(560, 560);
    auto *root = new QVBoxLayout(this);

    // --- 語言選單（Language picker，WORK 2）：可載入既有 UDL 編輯，或重新命名/移除 ---
    auto *pickerRow = new QHBoxLayout();
    m_languagePicker = new QComboBox(this);
    auto *renameBtn = new QPushButton(tr("Rename…"), this);
    auto *removeBtn = new QPushButton(tr("Remove"), this);
    pickerRow->addWidget(new QLabel(tr("Language:"), this));
    pickerRow->addWidget(m_languagePicker, 1);
    pickerRow->addWidget(renameBtn);
    pickerRow->addWidget(removeBtn);
    root->addLayout(pickerRow);

    // --- Dock/Undock 切換 + 透明度（比照 Notepad++ UDL 對話框，UDLxml 擴充） ---
    auto *toolRow = new QHBoxLayout();
    m_dockToggleBtn = new QPushButton(tr("Dock"), this);
    m_dockToggleBtn->setCheckable(true);
    toolRow->addWidget(m_dockToggleBtn);
    toolRow->addWidget(new QLabel(tr("Transparency:"), this));
    auto *transparencySlider = new QSlider(Qt::Horizontal, this);
    transparencySlider->setRange(20, 100);  // 20%~100% 不透明度，避免完全消失
    transparencySlider->setValue(100);
    toolRow->addWidget(transparencySlider, 1);
    root->addLayout(toolRow);
    connect(m_dockToggleBtn, &QPushButton::clicked, this, &UdlEditorDialog::toggleDocked);
    connect(transparencySlider, &QSlider::valueChanged, this, [this](int value) {
        setWindowOpacity(value / 100.0);
    });

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
        auto *rowWidget = new QWidget(keywordsPage);
        auto *rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QPlainTextEdit(rowWidget);
        edit->setPlaceholderText(tr("關鍵字，以空白或換行分隔"));
        edit->setMaximumHeight(60);
        // Prefix Mode（WORK 1）：勾選後，token 以此組任一關鍵字為前綴即視為命中。
        auto *prefixCheck = new QCheckBox(tr("Prefix Mode"), rowWidget);
        rowLayout->addWidget(edit);
        rowLayout->addWidget(prefixCheck);
        kwForm->addRow(tr("Group %1:").arg(i + 1), rowWidget);
        m_keywordGroups.push_back(edit);
        m_keywordGroupPrefix.push_back(prefixCheck);
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

    // --- 樣式分頁（③a UDL Styler：每個樣式的前景/背景色 + 粗體/斜體/底線） ---
    buildStylesPage(tabs);

    root->addWidget(tabs);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *exportBtn = box->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
    auto *importXmlBtn = box->addButton(tr("Import from Notepad++ XML…"), QDialogButtonBox::ActionRole);
    auto *exportXmlBtn = box->addButton(tr("Export to XML…"), QDialogButtonBox::ActionRole);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &UdlEditorDialog::saveDefinition);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exportBtn, &QPushButton::clicked, this, &UdlEditorDialog::exportDefinition);
    connect(importXmlBtn, &QPushButton::clicked, this, &UdlEditorDialog::importFromNppXml);
    connect(exportXmlBtn, &QPushButton::clicked, this, &UdlEditorDialog::exportToNppXml);

    connect(m_languagePicker, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UdlEditorDialog::loadSelectedLanguage);
    connect(renameBtn, &QPushButton::clicked, this, &UdlEditorDialog::renameLanguage);
    connect(removeBtn, &QPushButton::clicked, this, &UdlEditorDialog::removeLanguage);
    refreshLanguagePicker();
}

void UdlEditorDialog::updateColorButton(QPushButton *btn, const QColor &color)
{
    if (color.isValid()) {
        btn->setText(color.name());
        btn->setStyleSheet(QStringLiteral("background-color:%1;").arg(color.name()));
    } else {
        btn->setText(QObject::tr("(default)"));
        btn->setStyleSheet(QString());
    }
}

void UdlEditorDialog::pickColor(QPushButton *btn, QColor *target)
{
    const QColor initial = target->isValid() ? *target : Qt::black;
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Choose Color"));
    if (!chosen.isValid())
        return;  // 使用者取消：保持原狀（含未設定）
    *target = chosen;
    updateColorButton(btn, *target);
}

void UdlEditorDialog::buildStylesPage(QTabWidget *tabs)
{
    auto *stylesPage = new QWidget(tabs);
    auto *outer = new QVBoxLayout(stylesPage);
    auto *scroll = new QScrollArea(stylesPage);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget(scroll);
    auto *grid = new QFormLayout(inner);

    static const struct { int id; const char *label; } kStyles[] = {
        { UdlLexer::Default,   "Default" },
        { UdlLexer::Keyword,   "Keyword group 1" },
        { UdlLexer::Keyword2,  "Keyword group 2" },
        { UdlLexer::Keyword3,  "Keyword group 3" },
        { UdlLexer::Keyword4,  "Keyword group 4" },
        { UdlLexer::Keyword5,  "Keyword group 5" },
        { UdlLexer::Keyword6,  "Keyword group 6" },
        { UdlLexer::Keyword7,  "Keyword group 7" },
        { UdlLexer::Keyword8,  "Keyword group 8" },
        { UdlLexer::Comment,   "Comment" },
        { UdlLexer::String,    "String" },
        { UdlLexer::Number,    "Number" },
        { UdlLexer::Operator,  "Operator" },
        { UdlLexer::Delimiter, "Delimiter" },
    };

    for (const auto &s : kStyles) {
        StyleRow row;
        row.styleId = s.id;
        auto *rowWidget = new QWidget(inner);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        row.fgButton = new QPushButton(rowWidget);
        updateColorButton(row.fgButton, QColor());
        connect(row.fgButton, &QPushButton::clicked, this, [this, idx = m_styleRows.size()]() {
            pickColor(m_styleRows[idx].fgButton, &m_styleRows[idx].fg);
        });

        row.bgButton = new QPushButton(rowWidget);
        updateColorButton(row.bgButton, QColor());
        connect(row.bgButton, &QPushButton::clicked, this, [this, idx = m_styleRows.size()]() {
            pickColor(m_styleRows[idx].bgButton, &m_styleRows[idx].bg);
        });

        row.bold = new QCheckBox(tr("B"), rowWidget);
        row.italic = new QCheckBox(tr("I"), rowWidget);
        row.underline = new QCheckBox(tr("U"), rowWidget);

        rowLayout->addWidget(new QLabel(tr("FG:"), rowWidget));
        rowLayout->addWidget(row.fgButton);
        rowLayout->addWidget(new QLabel(tr("BG:"), rowWidget));
        rowLayout->addWidget(row.bgButton);
        rowLayout->addWidget(row.bold);
        rowLayout->addWidget(row.italic);
        rowLayout->addWidget(row.underline);

        grid->addRow(tr(s.label), rowWidget);
        m_styleRows.push_back(row);
    }

    inner->setLayout(grid);
    scroll->setWidget(inner);
    outer->addWidget(scroll);
    tabs->addTab(stylesPage, tr("Styles"));
}

macpad::features::UdlDefinition UdlEditorDialog::collectDefinition() const
{
    macpad::features::UdlDefinition def;
    def.name = m_name->text().trimmed();
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    for (const QString &ext : m_extensions->text().split(sep, Qt::SkipEmptyParts))
        def.extensions << ext.toLower();

    def.keywordGroups.resize(kUdlMaxKeywordGroups);
    def.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
    for (int i = 0; i < kUdlMaxKeywordGroups && i < m_keywordGroups.size(); ++i) {
        for (const QString &kw : m_keywordGroups.at(i)->toPlainText().split(sep, Qt::SkipEmptyParts))
            def.keywordGroups[i].insert(kw);
        if (i < m_keywordGroupPrefix.size())
            def.keywordGroupPrefixMode[i] = m_keywordGroupPrefix.at(i)->isChecked();
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

    // 樣式外觀（③a UDL Styler）：僅收錄使用者實際設定過的欄位；
    // fg/bg 皆未設定且未勾選任何粗體/斜體/底線者，不寫入 styles（維持預設）。
    for (const StyleRow &row : m_styleRows) {
        UdlStyle st;
        st.fg = row.fg.isValid() ? row.fg.name() : QString();
        st.bg = row.bg.isValid() ? row.bg.name() : QString();
        st.bold = row.bold->isChecked();
        st.italic = row.italic->isChecked();
        st.underline = row.underline->isChecked();
        if (!st.fg.isEmpty() || !st.bg.isEmpty() || st.bold || st.italic || st.underline)
            def.styles.insert(row.styleId, st);
    }
    return def;
}

void UdlEditorDialog::loadDefinitionIntoUi(const macpad::features::UdlDefinition &def)
{
    m_name->setText(def.name);
    m_extensions->setText(def.extensions.join(QLatin1Char(' ')));

    for (int i = 0; i < kUdlMaxKeywordGroups && i < m_keywordGroups.size(); ++i) {
        const QStringList kws(def.keywordGroup(i).begin(), def.keywordGroup(i).end());
        m_keywordGroups.at(i)->setPlainText(kws.join(QLatin1Char(' ')));
        if (i < m_keywordGroupPrefix.size())
            m_keywordGroupPrefix.at(i)->setChecked(def.keywordGroupPrefix(i));
    }

    const QStringList ops(def.operators.begin(), def.operators.end());
    m_operators->setPlainText(ops.join(QLatin1Char(' ')));

    QStringList delimLines;
    for (const auto &d : def.delimiters)
        delimLines << d.open + QLatin1Char('|') + d.escape + QLatin1Char('|') + d.close;
    m_delimiters->setPlainText(delimLines.join(QLatin1Char('\n')));

    m_folderOpen->setText(def.folderTokens.open);
    m_folderMiddle->setText(def.folderTokens.middle);
    m_folderClose->setText(def.folderTokens.close);

    m_lineComment->setText(def.lineComment);
    m_blockStart->setText(def.blockCommentStart);
    m_blockEnd->setText(def.blockCommentEnd);
    m_caseSensitive->setChecked(def.caseSensitive);

    for (StyleRow &row : m_styleRows) {
        const auto it = def.styles.constFind(row.styleId);
        if (it != def.styles.constEnd()) {
            row.fg = it.value().fg.isEmpty() ? QColor() : QColor(it.value().fg);
            row.bg = it.value().bg.isEmpty() ? QColor() : QColor(it.value().bg);
            row.bold->setChecked(it.value().bold);
            row.italic->setChecked(it.value().italic);
            row.underline->setChecked(it.value().underline);
        } else {
            row.fg = QColor();
            row.bg = QColor();
            row.bold->setChecked(false);
            row.italic->setChecked(false);
            row.underline->setChecked(false);
        }
        updateColorButton(row.fgButton, row.fg);
        updateColorButton(row.bgButton, row.bg);
    }
}

void UdlEditorDialog::refreshLanguagePicker(const QString &selectName)
{
    if (!m_languagePicker)
        return;
    const QSignalBlocker blocker(m_languagePicker);
    m_languagePicker->clear();
    m_languagePicker->addItem(tr("(New Language)"));
    int selectIndex = 0;
    if (m_manager) {
        for (const auto &d : m_manager->definitions()) {
            m_languagePicker->addItem(d.name);
            if (!selectName.isEmpty() && d.name == selectName)
                selectIndex = m_languagePicker->count() - 1;
        }
    }
    m_languagePicker->setCurrentIndex(selectIndex);
}

void UdlEditorDialog::loadSelectedLanguage(int index)
{
    if (index <= 0 || !m_manager)
        return;
    const QString name = m_languagePicker->itemText(index);
    for (const auto &d : m_manager->definitions()) {
        if (d.name == name) {
            loadDefinitionIntoUi(d);
            break;
        }
    }
}

void UdlEditorDialog::renameLanguage()
{
    if (!m_manager || !m_languagePicker || m_languagePicker->currentIndex() <= 0)
        return;
    const QString oldName = m_languagePicker->currentText();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Language"),
        tr("New name:"), QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (!m_manager->rename(oldName, newName)) {
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法重新命名 UDL。"));
        return;
    }
    refreshLanguagePicker(newName);
}

void UdlEditorDialog::removeLanguage()
{
    if (!m_manager || !m_languagePicker || m_languagePicker->currentIndex() <= 0)
        return;
    const QString name = m_languagePicker->currentText();
    if (QMessageBox::question(this, tr("Define Your Language"),
            tr("確定要移除語言「%1」？").arg(name)) != QMessageBox::Yes)
        return;
    m_manager->remove(name);
    refreshLanguagePicker();
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
    refreshLanguagePicker(def.name);

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export User Defined Language"),
        def.name + QStringLiteral(".json"), tr("UDL JSON (*.json)"));
    if (path.isEmpty())
        return;

    if (!m_manager->exportToFile(def.name, path))
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法匯出 UDL。"));
}

void UdlEditorDialog::importFromNppXml()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Notepad++ User Defined Language"),
        QString(), tr("Notepad++ UDL XML (*.xml)"));
    if (path.isEmpty())
        return;

    const macpad::features::UdlDefinition def = macpad::features::UdlXmlIo::importFromXml(path);
    if (!def.isValid()) {
        QMessageBox::warning(this, tr("Define Your Language"),
                              tr("無法解析此 Notepad++ UDL XML 檔案。"));
        return;
    }
    loadDefinitionIntoUi(def);
    if (m_manager && m_manager->save(def))
        refreshLanguagePicker(def.name);
}

void UdlEditorDialog::exportToNppXml()
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
    refreshLanguagePicker(def.name);

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export to Notepad++ User Defined Language"),
        def.name + QStringLiteral(".xml"), tr("Notepad++ UDL XML (*.xml)"));
    if (path.isEmpty())
        return;

    if (!macpad::features::UdlXmlIo::exportToXml(def, path))
        QMessageBox::warning(this, tr("Define Your Language"), tr("無法匯出至 Notepad++ XML。"));
}

void UdlEditorDialog::toggleDocked()
{
    // 簡化版 Dock/Undock：切換視窗旗標在「一般浮動對話框」與「Qt::SubWindow 類停駐外觀」
    // 之間變化。真正嵌入 MainWindow 的 dock widget 容器由整合層（MainWindow）決定是否採用；
    // 此處僅切換旗標與按鈕文字，讓對話框在有需要時可由呼叫端重新 parent 至 dock 容器。
    m_docked = !m_docked;
    const bool wasVisible = isVisible();
    if (m_docked) {
        setWindowFlags(windowFlags() | Qt::SubWindow);
        m_dockToggleBtn->setText(tr("Undock"));
    } else {
        setWindowFlags((windowFlags() & ~Qt::SubWindow) | Qt::Dialog);
        m_dockToggleBtn->setText(tr("Dock"));
    }
    if (wasVisible)
        show();
    m_dockToggleBtn->setChecked(m_docked);
}

}  // namespace macpad::ui
