#include "features/search/FindReplaceDialog.h"

#include "core/EditorWidget.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>

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
    m_extended = new QCheckBox(tr("Extended (\\n \\r \\t \\0 \\xNN)"), this);
    m_status = new QLabel(this);

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(30, 100);  // 對應 setWindowOpacity() 0.3..1.0
    m_opacitySlider->setValue(100);
    m_opacitySlider->setToolTip(tr("Window opacity"));

    auto *findBtn = new QPushButton(tr("Find Next"), this);
    auto *replaceBtn = new QPushButton(tr("Replace"), this);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), this);
    auto *markAllBtn = new QPushButton(tr("Mark All"), this);
    auto *countBtn = new QPushButton(tr("Count"), this);
    auto *swapBtn = new QPushButton(tr("↕"), this);  // 交換 Find/Replace 欄位文字
    auto *findVolNextBtn = new QPushButton(tr("Find (Volatile) Next"), this);
    auto *findVolPrevBtn = new QPushButton(tr("Find (Volatile) Previous"), this);
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
    grid->addWidget(m_extended, 3, 3, 1, 2);
    grid->addWidget(findVolNextBtn, 4, 4);
    grid->addWidget(findVolPrevBtn, 4, 5);
    grid->addWidget(new QLabel(tr("Opacity:"), this), 5, 0);
    grid->addWidget(m_opacitySlider, 5, 1, 1, 3);
    grid->addWidget(m_status, 6, 0, 1, 7);

    connect(findBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(markAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::markAll);
    connect(countBtn, &QPushButton::clicked, this, &FindReplaceDialog::countAll);
    connect(swapBtn, &QPushButton::clicked, this, &FindReplaceDialog::swapFindReplace);
    connect(findVolNextBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNextVolatile);
    connect(findVolPrevBtn, &QPushButton::clicked, this, &FindReplaceDialog::findPreviousVolatile);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &FindReplaceDialog::opacityChanged);
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

bool FindReplaceDialog::doFind(bool forward, bool fromStart, bool remember)
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return false;

    const QString findText = effectiveFindText();
    const bool re = m_regex->isChecked();
    const bool cs = m_caseSensitive->isChecked();
    const bool wo = m_wholeWord->isChecked();
    const bool wrap = m_wrap->isChecked();

    // 「In selection」：限制搜尋於勾選當下記錄的選取範圍內（不環繞，避免跑到範圍外）——FR-047 minimal 實作
    if (m_inSelection->isChecked() && m_selRestrictLineFrom >= 0) {
        const bool found =
            fromStart ? m_editor->findFirst(findText, re, cs, wo, false,
                                            forward, m_selRestrictLineFrom, m_selRestrictIndexFrom,
                                            true, false, re)
                      : m_editor->findFirst(findText, re, cs, wo, false,
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
        if (remember)
            rememberMatch();
        return true;
    }

    // cxx11=re：正則採 C++11 std::regex（支援 \d \w 與 \1 群組回填）——FR-011
    const bool found =
        fromStart ? m_editor->findFirst(findText, re, cs, wo, wrap,
                                        forward, 0, 0, true, false, re)
                  : m_editor->findFirst(findText, re, cs, wo, wrap,
                                        forward, -1, -1, true, false, re);
    if (found && remember)
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

void FindReplaceDialog::findNextVolatile()
{
    if (!m_editor)
        return;
    // Volatile：搜尋但不呼叫 rememberMatch，不覆寫最近一次「正式」命中記錄
    if (doFind(/*forward=*/true, /*fromStart=*/false, /*remember=*/false))
        m_status->clear();
    else
        report(tr("找不到「%1」").arg(m_findEdit->text()));
}

void FindReplaceDialog::findPreviousVolatile()
{
    if (!m_editor)
        return;
    if (doFind(/*forward=*/false, /*fromStart=*/false, /*remember=*/false))
        m_status->clear();
    else
        report(tr("找不到「%1」").arg(m_findEdit->text()));
}

void FindReplaceDialog::opacityChanged(int value)
{
    setWindowOpacity(static_cast<qreal>(value) / 100.0);
}

void FindReplaceDialog::replaceOne()
{
    if (!m_editor)
        return;
    // 僅在目前選取正是最近一次尋找命中的匹配時才取代，避免誤刪手動選取內容；再找下一個
    if (m_editor->hasSelectedText() && selectionIsRememberedMatch())
        m_editor->replace(effectiveReplaceText());
    findNext();
}

void FindReplaceDialog::replaceAll()
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return;

    const QString findText = effectiveFindText();
    const QString replaceText = effectiveReplaceText();

    // 「In selection」：只在選取文字範圍內取代（記憶體內取代後整段寫回選取區，FR-047 minimal 實作）
    if (m_inSelection->isChecked() && m_editor->hasSelectedText()) {
        QString selText = m_editor->selectedText();
        int count = 0;
        if (m_regex->isChecked()) {
            QString pattern = findText;
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
                    selText.replace(re, replaceText);
            }
        } else {
            const Qt::CaseSensitivity cs =
                m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
            count = selText.count(findText, cs);
            if (count > 0)
                selText.replace(findText, replaceText, cs);
        }
        if (count > 0)
            m_editor->replaceSelectedText(selText);
        report(tr("已取代 %1 處").arg(count));
        return;
    }

    // 走編輯核心的批次取代路徑（效能 NFR-004，單次 undo）；dotAll 傳給 replaceAll 多載
    const int count = m_editor->replaceAll(
        findText, replaceText,
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

QString FindReplaceDialog::effectiveFindText() const
{
    const QString text = m_findEdit->text();
    if (m_extended && m_extended->isChecked() && !m_regex->isChecked())
        return unescapeExtended(text);
    return text;
}

QString FindReplaceDialog::effectiveReplaceText() const
{
    const QString text = m_replaceEdit->text();
    if (m_extended && m_extended->isChecked() && !m_regex->isChecked())
        return unescapeExtended(text);
    return text;
}

QString FindReplaceDialog::unescapeExtended(const QString &text)
{
    // 自足實作：轉換 \n \r \t \0 \\ \xNN，其餘未知跳脫序列原樣保留。
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar n = text.at(i + 1);
            if (n == QLatin1Char('n')) {
                out += QLatin1Char('\n');
                ++i;
                continue;
            }
            if (n == QLatin1Char('r')) {
                out += QLatin1Char('\r');
                ++i;
                continue;
            }
            if (n == QLatin1Char('t')) {
                out += QLatin1Char('\t');
                ++i;
                continue;
            }
            if (n == QLatin1Char('0')) {
                out += QChar(QChar::Null);
                ++i;
                continue;
            }
            if (n == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
                ++i;
                continue;
            }
            if (n == QLatin1Char('x') && i + 3 < text.size()) {
                bool ok = false;
                const QString hex = text.mid(i + 2, 2);
                const int val = hex.toInt(&ok, 16);
                if (ok) {
                    out += QChar(static_cast<char16_t>(val));
                    i += 3;
                    continue;
                }
            }
            // 未知跳脫序列，原樣保留反斜線與該字元
            out += c;
            continue;
        }
        out += c;
    }
    return out;
}

}  // namespace macpad::features
