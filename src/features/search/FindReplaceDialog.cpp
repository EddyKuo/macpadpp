#include "features/search/FindReplaceDialog.h"

#include "core/EditorWidget.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

using macpad::core::EditorWidget;

namespace macpad::features {

FindReplaceDialog::FindReplaceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Find / Replace"));
    setModal(false);

    m_findEdit = new QLineEdit(this);
    m_replaceEdit = new QLineEdit(this);
    m_caseSensitive = new QCheckBox(tr("Match case"), this);
    m_wholeWord = new QCheckBox(tr("Whole word"), this);
    m_regex = new QCheckBox(tr("Regex"), this);
    m_wrap = new QCheckBox(tr("Wrap around"), this);
    m_wrap->setChecked(true);
    m_status = new QLabel(this);

    auto *findBtn = new QPushButton(tr("Find Next"), this);
    auto *replaceBtn = new QPushButton(tr("Replace"), this);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), this);
    auto *markAllBtn = new QPushButton(tr("Mark All"), this);
    findBtn->setDefault(true);

    auto *grid = new QGridLayout(this);
    grid->addWidget(new QLabel(tr("Find:"), this), 0, 0);
    grid->addWidget(m_findEdit, 0, 1, 1, 3);
    grid->addWidget(findBtn, 0, 4);
    grid->addWidget(new QLabel(tr("Replace:"), this), 1, 0);
    grid->addWidget(m_replaceEdit, 1, 1, 1, 3);
    grid->addWidget(replaceBtn, 1, 4);
    grid->addWidget(m_caseSensitive, 2, 0);
    grid->addWidget(m_wholeWord, 2, 1);
    grid->addWidget(m_regex, 2, 2);
    grid->addWidget(m_wrap, 2, 3);
    grid->addWidget(replaceAllBtn, 2, 4);
    grid->addWidget(markAllBtn, 2, 5);
    grid->addWidget(m_status, 3, 0, 1, 6);

    connect(findBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(markAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::markAll);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindReplaceDialog::findNext);
    // 增量搜尋（FR-012）：輸入即時定位第一個匹配
    connect(m_findEdit, &QLineEdit::textChanged, this, &FindReplaceDialog::incrementalFind);
}

void FindReplaceDialog::setEditor(EditorWidget *editor)
{
    m_editor = editor;
}

void FindReplaceDialog::showFind(bool replaceMode)
{
    m_replaceEdit->setVisible(true);  // v1 一律顯示取代列；replaceMode 決定焦點
    m_status->clear();

    // 以選取文字預填尋找欄（常見便利）
    if (m_editor && m_editor->hasSelectedText())
        m_findEdit->setText(m_editor->selectedText());

    show();
    raise();
    activateWindow();
    if (replaceMode)
        m_replaceEdit->setFocus();
    else
        m_findEdit->setFocus();
    m_findEdit->selectAll();
}

bool FindReplaceDialog::doFind(bool forward, bool fromStart)
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return false;

    const bool re = m_regex->isChecked();
    const bool cs = m_caseSensitive->isChecked();
    const bool wo = m_wholeWord->isChecked();
    const bool wrap = m_wrap->isChecked();

    // cxx11=re：正則採 C++11 std::regex（支援 \d \w 與 \1 群組回填）——FR-011
    if (fromStart)
        return m_editor->findFirst(m_findEdit->text(), re, cs, wo, wrap,
                                   forward, 0, 0, true, false, re);
    return m_editor->findFirst(m_findEdit->text(), re, cs, wo, wrap,
                               forward, -1, -1, true, false, re);
}

void FindReplaceDialog::findNext()
{
    if (!m_editor)
        return;
    if (doFind(/*forward=*/true, /*fromStart=*/false))
        m_status->clear();
    else
        report(tr("找不到「%1」").arg(m_findEdit->text()));
}

void FindReplaceDialog::replaceOne()
{
    if (!m_editor)
        return;
    // 若目前已選取到匹配則取代之，再找下一個；否則先找
    if (m_editor->hasSelectedText())
        m_editor->replace(m_replaceEdit->text());
    findNext();
}

void FindReplaceDialog::replaceAll()
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return;

    // 走編輯核心的批次取代路徑（效能 NFR-004，單次 undo）
    const int count = m_editor->replaceAll(
        m_findEdit->text(), m_replaceEdit->text(),
        m_regex->isChecked(), m_caseSensitive->isChecked(), m_wholeWord->isChecked());

    report(tr("已取代 %1 處").arg(count));
}

void FindReplaceDialog::markAll()
{
    if (!m_editor)
        return;
    const int n = m_editor->markAll(m_findEdit->text(), m_regex->isChecked(),
                                    m_caseSensitive->isChecked(), m_wholeWord->isChecked());
    report(tr("已標記 %1 處").arg(n));
}

void FindReplaceDialog::incrementalFind(const QString &text)
{
    if (!m_editor || text.isEmpty())
        return;
    // 從目前選取起點開始找，不改變錨點體驗（FR-012）
    int line = 0, index = 0;
    m_editor->getCursorPosition(&line, &index);
    m_editor->findFirst(text, m_regex->isChecked(), m_caseSensitive->isChecked(),
                        m_wholeWord->isChecked(), true, true, line, index, true,
                        false, m_regex->isChecked());
}

void FindReplaceDialog::report(const QString &msg)
{
    m_status->setText(msg);
}

}  // namespace macpad::features
