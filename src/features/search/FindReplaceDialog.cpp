#include "features/search/FindReplaceDialog.h"

#include "core/EditorWidget.h"
#include "persistence/SettingsStore.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>   // 尋找/取代歷史下拉；明確引入
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>   // pushHistory 期間擋下 lineEdit 訊號；明確引入
#include <QSlider>

using macpad::core::EditorWidget;
using macpad::persistence::SettingsStore;

namespace {
// QSettings 群組名稱，統一存放本對話框的搜尋選項勾選狀態
constexpr auto kSettingsGroup = "FindReplaceDialog";

// line[i] 起是否為一組完整的代理對（BMP 外字元在 QString 裡佔兩個 UTF-16 code unit）
bool isSurrogatePairAt(const QString &line, int i)
{
    return i + 1 < line.size() && line.at(i).isHighSurrogate()
           && line.at(i + 1).isLowSurrogate();
}
}  // namespace

namespace macpad::features {

FindReplaceDialog::FindReplaceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Find / Replace"));
    setModal(false);

    // 尋找/取代欄改用可編輯 QComboBox 以提供歷史紀錄下拉（複刻 Notepad++）。
    // m_findEdit/m_replaceEdit 仍指向 combo 的 lineEdit()，故既有 text()/setText()
    // 等呼叫完全不受影響——只是外觀多了一顆下拉箭頭。
    m_findCombo = new QComboBox(this);
    m_findCombo->setEditable(true);
    m_findCombo->setInsertPolicy(QComboBox::NoInsert);   // 由 pushHistory 自行管理順序
    m_findCombo->setMaxCount(kHistoryMax);
    m_findEdit = m_findCombo->lineEdit();

    m_replaceCombo = new QComboBox(this);
    m_replaceCombo->setEditable(true);
    m_replaceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_replaceCombo->setMaxCount(kHistoryMax);
    m_replaceEdit = m_replaceCombo->lineEdit();
    m_caseSensitive = new QCheckBox(tr("Match case"), this);
    m_wholeWord = new QCheckBox(tr("Whole word"), this);
    m_regex = new QCheckBox(tr("Regex"), this);
    m_wrap = new QCheckBox(tr("Wrap around"), this);
    m_wrap->setChecked(true);
    m_inSelection = new QCheckBox(tr("In selection"), this);
    m_dotMatchesNewline = new QCheckBox(tr("'.' matches newline"), this);
    m_extended = new QCheckBox(tr("Extended (\\n \\r \\t \\0 \\xNN)"), this);
    m_extended->setToolTip(tr("\\n \\r \\t \\0 \\b \\\\ \\xNN \\uXXXX \\u{XXXX} \\oNNN \\dNNN"));
    m_status = new QLabel(this);

    // 碼點範圍尋找：輸入下限/上限（接受 0x 前綴 16 進位或十進位），尋找下一個落在範圍內的字元
    m_cpLoEdit = new QLineEdit(this);
    m_cpLoEdit->setPlaceholderText(tr("lo，如 0x4E00"));
    m_cpHiEdit = new QLineEdit(this);
    m_cpHiEdit->setPlaceholderText(tr("hi，如 0x9FFF"));
    m_cpFindBtn = new QPushButton(tr("Find Codepoint"), this);

    // 還原上次使用的搜尋選項勾選狀態（在建立按鈕/連線之前，避免載入時觸發多餘的儲存）
    loadSearchOptions();
    // 載入 Preferences（FR-053）：keepFindDialogOpen/confirmReplaceAll/findInSelectionThreshold
    loadPreferences();

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
    grid->addWidget(m_findCombo, 0, 1, 1, 3);
    grid->addWidget(findBtn, 0, 4);
    grid->addWidget(countBtn, 0, 5);
    grid->addWidget(swapBtn, 0, 6);
    grid->addWidget(new QLabel(tr("Replace:"), this), 1, 0);
    grid->addWidget(m_replaceCombo, 1, 1, 1, 3);
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
    grid->addWidget(new QLabel(tr("Codepoint range:"), this), 6, 0);
    grid->addWidget(m_cpLoEdit, 6, 1);
    grid->addWidget(m_cpHiEdit, 6, 2);
    grid->addWidget(m_cpFindBtn, 6, 3);
    grid->addWidget(m_status, 7, 0, 1, 7);

    connect(findBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(markAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::markAll);
    connect(countBtn, &QPushButton::clicked, this, &FindReplaceDialog::countAll);
    connect(swapBtn, &QPushButton::clicked, this, &FindReplaceDialog::swapFindReplace);
    connect(findVolNextBtn, &QPushButton::clicked, this, &FindReplaceDialog::findNextVolatile);
    connect(findVolPrevBtn, &QPushButton::clicked, this, &FindReplaceDialog::findPreviousVolatile);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &FindReplaceDialog::opacityChanged);
    connect(m_cpFindBtn, &QPushButton::clicked, this, &FindReplaceDialog::findCodepointRange);
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

    // 勾選狀態變更即存檔（QSettings），下次開啟對話框/重啟程式時還原
    connect(m_caseSensitive, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("matchCase"), on); });
    connect(m_wholeWord, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("wholeWord"), on); });
    connect(m_regex, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("regex"), on); });
    connect(m_wrap, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("wrap"), on); });
    connect(m_inSelection, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("inSelection"), on); });
    connect(m_dotMatchesNewline, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("dotMatchesNewline"), on); });
    connect(m_extended, &QCheckBox::toggled, this,
           [this](bool on) { saveSearchOption(QStringLiteral("extended"), on); });
}

// 將 text 置頂加入 combo 歷史：已存在則先移除再插到最前（最近使用者優先），並限量。
// 為靜態且不碰其他狀態，方便單元測試。
void FindReplaceDialog::pushHistory(QComboBox *combo, const QString &text)
{
    if (!combo || text.isEmpty())
        return;

    // 整段擋掉 lineEdit 的訊號：removeItem 把該項移除時會一併清空 lineEdit，
    // 插回後又填了回來。這一來一往會發出 textChanged，連到 incrementalFind，
    // 於是「移動選取 + 重新武裝 rememberMatch」在使用者毫無察覺下發生——
    // 症狀是同一個搜尋詞按第二次 Find Next 時會先多跳一個匹配才開始搜尋。
    // 這裡進出的文字完全相同，中間的空字串狀態本來就不該被任何人看見。
    // combo 自身也一起擋：removeItem/insertItem/setCurrentIndex 還會發出
    // currentIndexChanged / currentTextChanged。目前沒有人接這些訊號，但只擋
    // lineEdit 等於把防護綁在「現在剛好只連了哪一條」上——日後有人接了 combo
    // 的訊號就會重蹈覆轍。兩個一起擋，防護才是對著成因而非症狀。
    // （combo 非可編輯時 lineEdit() 為 null，QSignalBlocker 對 null 是 no-op。
    //   blockSignals 只擋「由該物件送出」的訊號，不影響它自己的 slot 被呼叫，
    //   故 combo 內部更新 lineEdit 文字的路徑不受影響——已由本檔測試涵蓋。）
    const QSignalBlocker blockCombo(combo);
    const QSignalBlocker blockEdit(combo->lineEdit());

    const int existing = combo->findText(text);
    if (existing >= 0)
        combo->removeItem(existing);
    combo->insertItem(0, text);
    while (combo->count() > kHistoryMax)
        combo->removeItem(combo->count() - 1);
    // insertItem 會改動 currentIndex 進而覆寫使用者正在編輯的文字，故明確復原。
    combo->setCurrentIndex(0);
}

void FindReplaceDialog::loadSearchOptions()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    // 歷史紀錄跨 session 保留（複刻 Notepad++）。setCurrentIndex(-1) 讓欄位開啟時是空的，
    // 否則會自動填入上次的搜尋字串、蓋掉「以選取內容帶入」的行為。
    for (const auto &pair : {std::pair{m_findCombo,    QStringLiteral("findHistory")},
                             std::pair{m_replaceCombo, QStringLiteral("replaceHistory")}}) {
        const QStringList hist = settings.value(pair.second).toStringList();
        pair.first->addItems(hist.mid(0, kHistoryMax));
        pair.first->setCurrentIndex(-1);
        pair.first->clearEditText();
    }
    m_caseSensitive->setChecked(settings.value(QStringLiteral("matchCase"), false).toBool());
    m_wholeWord->setChecked(settings.value(QStringLiteral("wholeWord"), false).toBool());
    m_regex->setChecked(settings.value(QStringLiteral("regex"), false).toBool());
    m_wrap->setChecked(settings.value(QStringLiteral("wrap"), true).toBool());
    m_inSelection->setChecked(settings.value(QStringLiteral("inSelection"), false).toBool());
    m_dotMatchesNewline->setChecked(
        settings.value(QStringLiteral("dotMatchesNewline"), false).toBool());
    m_extended->setChecked(settings.value(QStringLiteral("extended"), false).toBool());
    settings.endGroup();
}

// 把目前欄位內容收進歷史。只在使用者「真的按下搜尋/取代」時呼叫——
// 若在 textChanged 就記錄，增量搜尋會把每個中間輸入狀態都塞進歷史。
void FindReplaceDialog::rememberSearchTerms()
{
    pushHistory(m_findCombo, m_findEdit->text());
    pushHistory(m_replaceCombo, m_replaceEdit->text());
    saveHistory();
}

// 將兩個 combo 的歷史寫回 QSettings（每次成功搜尋/取代後呼叫）
void FindReplaceDialog::saveHistory() const
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    for (const auto &pair : {std::pair{m_findCombo,    QStringLiteral("findHistory")},
                             std::pair{m_replaceCombo, QStringLiteral("replaceHistory")}}) {
        QStringList hist;
        for (int i = 0; i < pair.first->count(); ++i)
            hist << pair.first->itemText(i);
        settings.setValue(pair.second, hist);
    }
    settings.endGroup();
}

void FindReplaceDialog::saveSearchOption(const QString &key, bool value) const
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(key, value);
    settings.endGroup();
}

void FindReplaceDialog::setEditor(EditorWidget *editor)
{
    m_editor = editor;
}

void FindReplaceDialog::loadPreferences()
{
    const auto s = SettingsStore::load();
    m_keepFindDialogOpen = s.keepFindDialogOpen;
    m_confirmReplaceAll = s.confirmReplaceAll;
    m_findInSelectionThreshold = s.findInSelectionThreshold;
}

void FindReplaceDialog::closeIfNotKeptOpen()
{
    if (!m_keepFindDialogOpen)
        close();
}

void FindReplaceDialog::showFind(bool replaceMode)
{
    // 每次開啟時重新讀取 Preferences（可能在對話框關閉期間被使用者調整）
    loadPreferences();

    m_replaceEdit->setVisible(true);  // v1 一律顯示取代列；replaceMode 決定焦點
    m_status->clear();

    // 以選取文字預填尋找欄（常見便利）
    if (m_editor && m_editor->hasSelectedText())
        m_findEdit->setText(m_editor->selectedText());

    // findInSelectionThreshold（FR-053）：選取範圍跨越行數 >= 門檻時，預設勾選「In selection」
    if (m_editor && m_findInSelectionThreshold > 0 && m_editor->hasSelectedText()) {
        int lf = 0, if_ = 0, lt = 0, it = 0;
        m_editor->getSelection(&lf, &if_, &lt, &it);
        const int spanLines = lt - lf + 1;
        if (spanLines >= m_findInSelectionThreshold)
            m_inSelection->setChecked(true);
    }

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
    rememberSearchTerms();   // 使用者實際執行動作才收進歷史
    if (!m_editor)
        return;
    if (doFind(/*forward=*/true, /*fromStart=*/false)) {
        m_status->clear();
        closeIfNotKeptOpen();  // keepFindDialogOpen=false 時，尋找成功後自動關閉（FR-053）
    } else {
        report(tr("找不到「%1」").arg(m_findEdit->text()));
    }
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

void FindReplaceDialog::findCodepointRange()
{
    if (!m_editor)
        return;

    // 接受 "0x" 前綴 16 進位或純十進位（QString::toUInt base=0 自動判斷）
    bool okLo = false, okHi = false;
    const uint lo = m_cpLoEdit->text().trimmed().toUInt(&okLo, 0);
    const uint hi = m_cpHiEdit->text().trimmed().toUInt(&okHi, 0);
    if (!okLo || !okHi || lo > hi) {
        report(tr("碼點範圍無效（請輸入 lo <= hi，可用 0x 前綴）"));
        return;
    }

    int curLine = 0, curChar = 0;
    m_editor->getCursorPosition(&curLine, &curChar);
    const int totalLines = m_editor->lines();
    const bool wrap = m_wrap->isChecked();

    // QScintilla 的欄位索引是「Unicode 字元」數（positionFromLineIndex 逐次呼叫
    // SCI_POSITIONRELATIVE），而底下的掃描走的是 QString 的 UTF-16 code unit 索引。
    // BMP 字元兩者相同，但 BMP 外字元（emoji）在 QString 佔兩格、在 Scintilla 只算
    // 一格——行內只要出現過一個 emoji，其後所有位置就會整體偏移。故進出 Scintilla
    // 的邊界一律顯式換算。
    const auto u16ToCharIndex = [](const QString &line, int u16) {
        int chars = 0, i = 0;
        while (i < u16 && i < line.size()) {
            i += isSurrogatePairAt(line, i) ? 2 : 1;
            ++chars;
        }
        return chars;
    };
    const auto charToU16Index = [](const QString &line, int chars) {
        int i = 0, n = 0;
        while (n < chars && i < line.size()) {
            i += isSurrogatePairAt(line, i) ? 2 : 1;
            ++n;
        }
        return i;
    };

    // 游標位置是字元索引，換成起始行的 QString 索引後才能拿來掃描
    const int curIndex = charToU16Index(m_editor->text(curLine), curChar);

    // 由 [startLine,startIndex] 掃描至 endLineExclusive（不含）之前，找到第一個碼點落在 [lo,hi] 的字元；
    // 找到即 setSelection 並回傳 true。處理代理對（surrogate pair）以支援 BMP 外碼點。
    auto scanFrom = [&](int startLine, int startIndex, int endLineExclusive) -> bool {
        for (int line = startLine; line < endLineExclusive; ++line) {
            const QString lineText = m_editor->text(line);
            int i = (line == startLine) ? startIndex : 0;
            while (i < lineText.size()) {
                const QChar c = lineText.at(i);
                uint cp = c.unicode();
                int len = 1;
                if (isSurrogatePairAt(lineText, i)) {
                    cp = QChar::surrogateToUcs4(c, lineText.at(i + 1));
                    len = 2;
                }
                if (cp >= lo && cp <= hi) {
                    m_editor->setSelection(line, u16ToCharIndex(lineText, i),
                                           line, u16ToCharIndex(lineText, i + len));
                    m_editor->ensureLineVisible(line);
                    return true;
                }
                i += len;
            }
        }
        return false;
    };

    if (scanFrom(curLine, curIndex, totalLines)) {
        report(tr("找到碼點範圍 [%1,%2] 內字元").arg(lo).arg(hi));
        return;
    }
    if (wrap && scanFrom(0, 0, curLine + 1)) {
        report(tr("找到碼點範圍 [%1,%2] 內字元（已環繞）").arg(lo).arg(hi));
        return;
    }
    report(tr("找不到碼點範圍 [%1,%2] 內的字元").arg(lo).arg(hi));
}

void FindReplaceDialog::replaceOne()
{
    rememberSearchTerms();   // 使用者實際執行動作才收進歷史
    if (!m_editor)
        return;
    // 僅在目前選取正是最近一次尋找命中的匹配時才取代，避免誤刪手動選取內容；再找下一個
    if (m_editor->hasSelectedText() && selectionIsRememberedMatch())
        m_editor->replace(effectiveReplaceText());
    findNext();  // findNext() 內已依 keepFindDialogOpen 決定是否關閉
}

void FindReplaceDialog::replaceAll()
{
    rememberSearchTerms();   // 使用者實際執行動作才收進歷史
    if (!m_editor || m_findEdit->text().isEmpty())
        return;

    // confirmReplaceAll（FR-053）：執行前提示確認，避免誤觸大範圍取代
    if (m_confirmReplaceAll) {
        const auto ret = QMessageBox::question(
            this, tr("Replace All"),
            tr("即將把所有符合「%1」的內容取代為「%2」，是否繼續？")
                .arg(m_findEdit->text(), m_replaceEdit->text()),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

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
        closeIfNotKeptOpen();
        return;
    }

    // 走編輯核心的批次取代路徑（效能 NFR-004，單次 undo）；dotAll 傳給 replaceAll 多載
    const int count = m_editor->replaceAll(
        findText, replaceText,
        m_regex->isChecked(), m_caseSensitive->isChecked(), m_wholeWord->isChecked(),
        m_dotMatchesNewline->isChecked());

    report(tr("已取代 %1 處").arg(count));
    closeIfNotKeptOpen();
}

void FindReplaceDialog::markAll()
{
    rememberSearchTerms();   // 使用者實際執行動作才收進歷史
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
    // 自足實作：轉換 \n \r \t \0 \\ \xNN \b \uXXXX \u{XXXX} \oNNN(八進位) \dNNN(十進位)，
    // 其餘未知跳脫序列原樣保留。
    QString out;
    out.reserve(text.size());

    // 將 Unicode 碼點附加到 out；超出 BMP（>0xFFFF）時輸出代理對（surrogate pair）
    auto appendCodepoint = [&out](uint cp) {
        if (cp > 0xFFFFu) {
            cp -= 0x10000u;
            out += QChar(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out += QChar(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        } else {
            out += QChar(static_cast<char16_t>(cp));
        }
    };

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
            if (n == QLatin1Char('b')) {
                out += QChar(0x08);  // backspace
                ++i;
                continue;
            }
            // \u{XXXX...}：可變長度 16 進位碼點，直到對應的 '}'（先於固定長度 \uXXXX 判斷）
            if (n == QLatin1Char('u') && i + 2 < text.size()
                && text.at(i + 2) == QLatin1Char('{')) {
                int j = i + 3;
                while (j < text.size() && text.at(j) != QLatin1Char('}'))
                    ++j;
                if (j < text.size() && j > i + 3) {
                    bool ok = false;
                    const uint cp = text.mid(i + 3, j - (i + 3)).toUInt(&ok, 16);
                    if (ok && cp <= 0x10FFFFu) {
                        appendCodepoint(cp);
                        i = j;
                        continue;
                    }
                }
            }
            // \uXXXX：固定 4 位 16 進位碼點
            if (n == QLatin1Char('u') && i + 5 < text.size()) {
                bool ok = false;
                const uint cp = text.mid(i + 2, 4).toUInt(&ok, 16);
                if (ok) {
                    appendCodepoint(cp);
                    i += 5;
                    continue;
                }
            }
            // \oNNN：八進位字元碼（貪婪取連續八進位數字）
            if (n == QLatin1Char('o')) {
                int j = i + 2;
                while (j < text.size() && text.at(j) >= QLatin1Char('0')
                       && text.at(j) <= QLatin1Char('7'))
                    ++j;
                if (j > i + 2) {
                    bool ok = false;
                    const uint cp = text.mid(i + 2, j - (i + 2)).toUInt(&ok, 8);
                    if (ok && cp <= 0x10FFFFu) {
                        appendCodepoint(cp);
                        i = j - 1;
                        continue;
                    }
                }
            }
            // \dNNN：十進位字元碼（貪婪取連續十進位數字）
            if (n == QLatin1Char('d')) {
                int j = i + 2;
                while (j < text.size() && text.at(j).isDigit())
                    ++j;
                if (j > i + 2) {
                    bool ok = false;
                    const uint cp = text.mid(i + 2, j - (i + 2)).toUInt(&ok, 10);
                    if (ok && cp <= 0x10FFFFu) {
                        appendCodepoint(cp);
                        i = j - 1;
                        continue;
                    }
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
