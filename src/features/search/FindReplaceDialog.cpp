#include "features/search/FindReplaceDialog.h"

#include "core/EditorWidget.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>

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
    m_inSelection = new QCheckBox(tr("In selection"), this);
    m_dotMatchesNewline = new QCheckBox(tr("'.' matches newline"), this);
    m_status = new QLabel(this);

    auto *findBtn = new QPushButton(tr("Find Next"), this);
    auto *replaceBtn = new QPushButton(tr("Replace"), this);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), this);
    auto *markAllBtn = new QPushButton(tr("Mark All"), this);
    auto *countBtn = new QPushButton(tr("Count"), this);
    auto *swapBtn = new QPushButton(tr("↕"), this);  // 交換 Find/Replace 欄位文字
    findBtn->setDefault(true);

    auto *grid = new QGridLayout(this);
    grid->addWidget(new QLabel(tr("Find:"), this), 0, 0);
    grid->addWidget(m_findEdit, 0, 1, 1, 3);
    grid->addWidget(findBtn, 0, 4);
    grid->addWidget(countBtn, 0, 5);
    grid->addWidget(swapBtn, 0, 6);
    grid->addWidget(new QLabel(tr("Replace:"), this), 1, 0);
    grid->addWidget(m_replaceEdit, 1, 1, 1, 3);
    grid->addWidget(replaceBtn, 1, 4);
    grid->addWidget(m_caseSensitive, 2, 0);
    grid->addWidget(m_wholeWord, 2, 1);
    grid->addWidget(m_regex, 2, 2);
    grid->addWidget(m_wrap, 2, 3);
    grid->addWidget(replaceAllBtn, 2, 4);
    grid->addWidget(markAllBtn, 2, 5);
    grid->addWidget(m_inSelection, 3, 0);
    grid->addWidget(m_dotMatchesNewline, 3, 1, 1, 2);
    grid->addWidget(m_status, 4, 0, 1, 7);

    connect(findBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(markAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::markAll);
    connect(countBtn, &QPushButton::clicked, this, &FindReplaceDialog::countAll);
    connect(swapBtn, &QPushButton::clicked, this, &FindReplaceDialog::swapFindReplace);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindReplaceDialog::findNext);
    // 增量搜尋（FR-012）：輸入即時定位第一個匹配
    connect(m_findEdit, &QLineEdit::textChanged, this, &FindReplaceDialog::incrementalFind);
    // 勾選「In selection」當下記錄目前選取範圍，作為後續 find/replaceAll 的限制邊界
    connect(m_inSelection, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_editor && m_editor->hasSelectedText()) {
            m_editor->getSelection(&m_selRestrictLineFrom, &m_selRestrictIndexFrom,
                                   &m_selRestrictLineTo, &m_selRestrictIndexTo);
        } else {
            m_selRestrictLineFrom = m_selRestrictIndexFrom = -1;
            m_selRestrictLineTo = m_selRestrictIndexTo = -1;
        }
    });
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

    // 「In selection」：限制搜尋於勾選當下記錄的選取範圍內（不環繞，避免跑到範圍外）——FR-047 minimal 實作
    if (m_inSelection->isChecked() && m_selRestrictLineFrom >= 0) {
        const bool found =
            fromStart ? m_editor->findFirst(m_findEdit->text(), re, cs, wo, false,
                                            forward, m_selRestrictLineFrom, m_selRestrictIndexFrom,
                                            true, false, re)
                      : m_editor->findFirst(m_findEdit->text(), re, cs, wo, false,
                                            forward, -1, -1, true, false, re);
        if (!found)
            return false;
        int mlf = 0, mif = 0, mlt = 0, mit = 0;
        m_editor->getSelection(&mlf, &mif, &mlt, &mit);
        const bool withinFrom = (mlf > m_selRestrictLineFrom)
            || (mlf == m_selRestrictLineFrom && mif >= m_selRestrictIndexFrom);
        const bool withinTo = (mlt < m_selRestrictLineTo)
            || (mlt == m_selRestrictLineTo && mit <= m_selRestrictIndexTo);
        if (!withinFrom || !withinTo)
            return false;  // 匹配超出選取邊界，視為選取內找不到
        rememberMatch();
        return true;
    }

    // cxx11=re：正則採 C++11 std::regex（支援 \d \w 與 \1 群組回填）——FR-011
    const bool found =
        fromStart ? m_editor->findFirst(m_findEdit->text(), re, cs, wo, wrap,
                                        forward, 0, 0, true, false, re)
                  : m_editor->findFirst(m_findEdit->text(), re, cs, wo, wrap,
                                        forward, -1, -1, true, false, re);
    if (found)
        rememberMatch();
    return found;
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
    // 僅在目前選取正是最近一次尋找命中的匹配時才取代，避免誤刪手動選取內容；再找下一個
    if (m_editor->hasSelectedText() && selectionIsRememberedMatch())
        m_editor->replace(m_replaceEdit->text());
    findNext();
}

void FindReplaceDialog::replaceAll()
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return;

    // 「In selection」：只在選取文字範圍內取代（記憶體內取代後整段寫回選取區，FR-047 minimal 實作）
    if (m_inSelection->isChecked() && m_editor->hasSelectedText()) {
        QString selText = m_editor->selectedText();
        int count = 0;
        if (m_regex->isChecked()) {
            QString pattern = m_findEdit->text();
            if (m_wholeWord->isChecked())
                pattern = QStringLiteral("\\b") + pattern + QStringLiteral("\\b");
            QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
            if (!m_caseSensitive->isChecked())
                opts |= QRegularExpression::CaseInsensitiveOption;
            if (m_dotMatchesNewline->isChecked())
                opts |= QRegularExpression::DotMatchesEverythingOption;
            const QRegularExpression re(pattern, opts);
            if (re.isValid()) {
                auto it = re.globalMatch(selText);
                while (it.hasNext()) { it.next(); ++count; }
                if (count > 0)
                    selText.replace(re, m_replaceEdit->text());
            }
        } else {
            const Qt::CaseSensitivity cs =
                m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
            count = selText.count(m_findEdit->text(), cs);
            if (count > 0)
                selText.replace(m_findEdit->text(), m_replaceEdit->text(), cs);
        }
        if (count > 0)
            m_editor->replaceSelectedText(selText);
        report(tr("已取代 %1 處").arg(count));
        return;
    }

    // 走編輯核心的批次取代路徑（效能 NFR-004，單次 undo）；dotAll 傳給 replaceAll 多載
    const int count = m_editor->replaceAll(
        m_findEdit->text(), m_replaceEdit->text(),
        m_regex->isChecked(), m_caseSensitive->isChecked(), m_wholeWord->isChecked(),
        m_dotMatchesNewline->isChecked());

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

void FindReplaceDialog::countAll()
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return;

    int count = 0;
    // 「In selection」：只在選取文字內計數，不使用 EditorWidget::countMatches（其為全文計數）
    if (m_inSelection->isChecked() && m_editor->hasSelectedText()) {
        const QString sel = m_editor->selectedText();
        if (m_regex->isChecked()) {
            QString pattern = m_findEdit->text();
            if (m_wholeWord->isChecked())
                pattern = QStringLiteral("\\b") + pattern + QStringLiteral("\\b");
            QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
            if (!m_caseSensitive->isChecked())
                opts |= QRegularExpression::CaseInsensitiveOption;
            const QRegularExpression re(pattern, opts);
            if (re.isValid()) {
                auto it = re.globalMatch(sel);
                while (it.hasNext()) { it.next(); ++count; }
            }
        } else {
            const Qt::CaseSensitivity cs =
                m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
            count = sel.count(m_findEdit->text(), cs);
        }
    } else {
        // 只計數不移動游標/選取（FR-047）
        count = m_editor->countMatches(m_findEdit->text(), m_regex->isChecked(),
                                       m_caseSensitive->isChecked(), m_wholeWord->isChecked());
    }
    report(tr("共 %1 處匹配").arg(count));
}

void FindReplaceDialog::swapFindReplace()
{
    const QString f = m_findEdit->text();
    m_findEdit->setText(m_replaceEdit->text());
    m_replaceEdit->setText(f);
}

void FindReplaceDialog::incrementalFind(const QString &text)
{
    if (!m_editor || text.isEmpty())
        return;
    // 從目前選取起點開始找，不改變錨點體驗（FR-012）
    int line = 0, index = 0;
    m_editor->getCursorPosition(&line, &index);
    const bool found =
        m_editor->findFirst(text, m_regex->isChecked(), m_caseSensitive->isChecked(),
                            m_wholeWord->isChecked(), m_wrap->isChecked(), true, line, index, true,
                            false, m_regex->isChecked());
    if (found)
        rememberMatch();
}

void FindReplaceDialog::rememberMatch()
{
    if (!m_editor)
        return;
    m_editor->getSelection(&m_matchLineFrom, &m_matchIndexFrom,
                           &m_matchLineTo, &m_matchIndexTo);
}

bool FindReplaceDialog::selectionIsRememberedMatch() const
{
    if (!m_editor)
        return false;
    int lf = -1, if_ = -1, lt = -1, it = -1;
    m_editor->getSelection(&lf, &if_, &lt, &it);
    // 需為非空選取，且與最近一次尋找命中的範圍完全一致
    if (lf == lt && if_ == it)
        return false;
    return lf == m_matchLineFrom && if_ == m_matchIndexFrom &&
           lt == m_matchLineTo && it == m_matchIndexTo;
}

void FindReplaceDialog::report(const QString &msg)
{
    m_status->setText(msg);
}

}  // namespace macpad::features
