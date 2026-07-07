#include "core/EditorWidget.h"

#include "core/LexerFactory.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QStringConverter>
#include <QTextStream>

#include <Qsci/qscilexer.h>

namespace macpad::core {

EditorWidget::EditorWidget(QWidget *parent)
    : QsciScintilla(parent)
{
    applyDefaultConfig();

    // dirty 狀態變化轉發（FR-014：分頁未存標記 ●）
    connect(this, &QsciScintilla::modificationChanged,
            this, &EditorWidget::dirtyChanged);
}

void EditorWidget::applyDefaultConfig()
{
    // 等寬字型（DR-001 預設 Menlo 13；正式值待 settings 載入）
    QFont font(QStringLiteral("Menlo"), 13);
    font.setStyleHint(QFont::Monospace);
    setFont(font);

    // 行號邊欄（FR-008）——寬度依行數動態設定
    setMarginType(0, QsciScintilla::NumberMargin);
    setMarginLineNumbers(0, true);
    const QFontMetrics fm(font);
    setMarginWidth(0, fm.horizontalAdvance(QStringLiteral("0000")) + 8);

    // 折疊邊欄（FR-004）
    setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);

    // 括號配對高亮（FR-007）
    setBraceMatching(QsciScintilla::SloppyBraceMatch);

    // 縮排參考線與 Tab（FR-008/009）
    setIndentationGuides(true);
    setTabWidth(4);
    setIndentationsUseTabs(false);
    setAutoIndent(true);

    // 目前行高亮
    setCaretLineVisible(true);

    // 自動完成（FR-027）：以文件內字詞為來源
    setAutoCompletionSource(QsciScintilla::AcsDocument);
    setAutoCompletionThreshold(2);          // 輸入 2 字元起提示
    setAutoCompletionCaseSensitivity(false);
    setAutoCompletionReplaceWord(true);

    // UTF-8 內部表示（FR-019）
    setUtf8(true);

    // 多游標 / 多重選取（FR-005）——QScintilla 原生能力，Spike R1 於此驗證
    SendScintilla(SCI_SETMULTIPLESELECTION, 1);
    SendScintilla(SCI_SETADDITIONALSELECTIONTYPING, 1);
    SendScintilla(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);

    // 欄位/區塊選取（FR-006）：Option(⌥)+拖曳做矩形選取。
    // Scintilla 以 SCMOD_ALT 觸發矩形拖曳；macOS 的 Option 對映 Alt（CON-004）。
    SendScintilla(SCI_SETRECTANGULARSELECTIONMODIFIER, SCMOD_ALT);
    SendScintilla(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, 1);

    // Mark 高亮指示器（FR-012）
    SendScintilla(SCI_INDICSETSTYLE, static_cast<unsigned long>(kMarkIndicator), INDIC_ROUNDBOX);
    SendScintilla(SCI_INDICSETFORE, static_cast<unsigned long>(kMarkIndicator), 0x00B478FFUL);
    SendScintilla(SCI_INDICSETALPHA, static_cast<unsigned long>(kMarkIndicator), 100);

    // 書籤符號邊欄（margin 1，FR-008）
    setMarginType(1, QsciScintilla::SymbolMargin);
    setMarginWidth(1, 16);
    setMarginSensitivity(1, true);  // 可點擊切換書籤
    setMarginMarkerMask(1, 1 << kBookmarkMarker);
    markerDefine(QsciScintilla::Circle, kBookmarkMarker);
    setMarkerBackgroundColor(QColor(66, 133, 244), kBookmarkMarker);
    connect(this, &QsciScintilla::marginClicked, this, &EditorWidget::onMarginClicked);
}

void EditorWidget::applyLexerForPath(const QString &path)
{
    // 舊 lexer 由 setLexer 接管刪除；設 nullptr 為純文字
    QsciLexer *lexer = LexerFactory::createForFileName(path, this);
    setLexer(lexer);
    if (lexer) {
        // lexer 會覆寫字型，統一回等寬
        QFont font(QStringLiteral("Menlo"), 13);
        font.setStyleHint(QFont::Monospace);
        lexer->setDefaultFont(font);
        lexer->setFont(font);
    }
    emit lexerChanged();  // 供 MainWindow 重新上主題色（降飽和）
}

void EditorWidget::setLanguageLexer(QsciLexer *lexer)
{
    setLexer(lexer);
    if (lexer) {
        QFont font(QStringLiteral("Menlo"), 13);
        font.setStyleHint(QFont::Monospace);
        lexer->setDefaultFont(font);
        lexer->setFont(font);
    }
    emit lexerChanged();
}

bool EditorWidget::loadFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {  // 二進位讀取；編碼自行偵測（FR-019）
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    // 偵測只讀開頭 buffer（CON-005）；EOL 亦由開頭判定
    const DetectResult det = FileEncoding::detect(raw.left(65536));
    m_encoding = det.encoding;
    m_eol = det.eol;

    m_codecName.clear();   // 重新載入 → 回到自動偵測，清掉先前手選的 codec
    const QString content = FileEncoding::decode(raw, m_encoding);
    setText(content);
    applyEolMode(m_eol);

    m_filePath = QFileInfo(path).absoluteFilePath();
    applyLexerForPath(m_filePath);
    setModified(false);
    emit metaChanged();
    return true;
}

bool EditorWidget::saveFile(const QString &path, QString *errorMessage)
{
    // 原子寫入（temp + rename）以免寫入失敗遺失內容——FR-014 邊界、ADR-5 精神
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {  // 二進位；依編碼輸出（FR-019）
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    // QScintilla text() 依目前 EOL 模式回傳；再依編碼轉位元組（FR-019/020）
    // 具名 codec（Character sets 選的傳統編碼）優先，否則用 enum 編碼。
    const QByteArray bytes = m_codecName.isEmpty()
                                 ? FileEncoding::encode(text(), m_encoding)
                                 : FileEncoding::encodeWithCodec(text(), m_codecName);
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    m_filePath = QFileInfo(path).absoluteFilePath();
    applyLexerForPath(m_filePath);
    setModified(false);
    return true;
}

QString EditorWidget::displayName() const
{
    const QString base = isUntitled()
        ? QStringLiteral("Untitled")
        : QFileInfo(m_filePath).fileName();
    return isDirty() ? QStringLiteral("● ") + base : base;
}

void EditorWidget::applyEolMode(Eol eol)
{
    switch (eol) {
    case Eol::Lf:   setEolMode(QsciScintilla::EolUnix); break;
    case Eol::CrLf: setEolMode(QsciScintilla::EolWindows); break;
    case Eol::Cr:   setEolMode(QsciScintilla::EolMac); break;
    }
}

void EditorWidget::setEncoding(Encoding enc)
{
    // 選 Unicode 編碼會覆蓋先前手選的傳統 codec
    if (m_encoding == enc && m_codecName.isEmpty())
        return;
    m_encoding = enc;
    m_codecName.clear();
    setModified(true);
    emit metaChanged();
}

QString EditorWidget::encodingLabel() const
{
    return m_codecName.isEmpty() ? FileEncoding::encodingName(m_encoding) : m_codecName;
}

void EditorWidget::setSaveCodec(const QString &codecName)
{
    m_codecName = codecName;
    setModified(true);
    emit metaChanged();
}

bool EditorWidget::reinterpretWithCodec(const QString &codecName, QString *errorMessage)
{
    if (m_filePath.isEmpty()) {
        // 未存檔：無原始位元組可重讀 → 僅設為存檔 codec
        setSaveCodec(codecName);
        return true;
    }
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    const DetectResult det = FileEncoding::detect(raw.left(65536));
    m_eol = det.eol;
    setText(FileEncoding::decodeWithCodec(raw, codecName));
    applyEolMode(m_eol);
    m_codecName = codecName;
    setModified(false);   // 純重新解讀磁碟內容，視為未變更
    emit metaChanged();
    return true;
}

void EditorWidget::convertEol(Eol eol)
{
    m_eol = eol;
    applyEolMode(eol);
    convertEols(eolMode());  // 立即轉換既有內容（FR-020）
    setModified(true);
    emit metaChanged();
}

int EditorWidget::replaceAll(const QString &find, const QString &replaceStr,
                             bool regex, bool caseSensitive, bool wholeWord)
{
    if (find.isEmpty())
        return 0;

    // 效能路徑（NFR-004）：在記憶體以單次掃描完成取代，再以一次 target 寫回整份文件
    // （單次 undo，FR-010 AC2）。相較逐一 SCI_REPLACETARGET，高匹配密度下快數個量級。
    QString content = text();
    int count = 0;

    if (regex || wholeWord) {
        QString pattern = regex ? find : QRegularExpression::escape(find);
        if (wholeWord)
            pattern = QStringLiteral("\\b") + pattern + QStringLiteral("\\b");
        QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
        if (!caseSensitive)
            opts |= QRegularExpression::CaseInsensitiveOption;
        const QRegularExpression re(pattern, opts);
        if (!re.isValid())
            return 0;
        // 計數
        auto it = re.globalMatch(content);
        while (it.hasNext()) { it.next(); ++count; }
        if (count > 0)
            content.replace(re, replaceStr);  // \1 群組回填（FR-011）
    } else {
        const Qt::CaseSensitivity cs =
            caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        count = content.count(find, cs);
        if (count > 0)
            content.replace(find, replaceStr, cs);
    }

    if (count > 0) {
        const QByteArray bytes = content.toUtf8();
        beginUndoAction();
        SendScintilla(SCI_SETTARGETSTART, 0UL);
        SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(length()));
        SendScintilla(SCI_REPLACETARGET,
                      static_cast<unsigned long>(bytes.size()), bytes.constData());
        endUndoAction();
    }
    return count;
}

int EditorWidget::markAll(const QString &find, bool regex, bool caseSensitive, bool wholeWord)
{
    clearMarks();
    if (find.isEmpty())
        return 0;

    const QByteArray fb = find.toUtf8();
    int flags = 0;
    if (caseSensitive) flags |= SCFIND_MATCHCASE;
    if (wholeWord)     flags |= SCFIND_WHOLEWORD;
    if (regex)         flags |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
    SendScintilla(SCI_SETSEARCHFLAGS, flags);
    SendScintilla(SCI_SETINDICATORCURRENT, static_cast<unsigned long>(kMarkIndicator));

    int count = 0;
    long start = 0;
    const long end = length();
    while (start <= end) {
        SendScintilla(SCI_SETTARGETSTART, static_cast<unsigned long>(start));
        SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(end));
        const long found = SendScintilla(SCI_SEARCHINTARGET,
                                         static_cast<unsigned long>(fb.size()), fb.constData());
        if (found < 0)
            break;
        const long ms = SendScintilla(SCI_GETTARGETSTART);
        const long me = SendScintilla(SCI_GETTARGETEND);
        SendScintilla(SCI_INDICATORFILLRANGE, static_cast<unsigned long>(ms),
                      static_cast<unsigned long>(me - ms));
        ++count;
        start = (me > ms) ? me : me + 1;  // 空匹配前進一位
    }
    return count;
}

void EditorWidget::clearMarks()
{
    SendScintilla(SCI_SETINDICATORCURRENT, static_cast<unsigned long>(kMarkIndicator));
    SendScintilla(SCI_INDICATORCLEARRANGE, 0UL, static_cast<unsigned long>(length()));
}

// --- 書籤（FR-008）-------------------------------------------------------

void EditorWidget::onMarginClicked(int margin, int line, Qt::KeyboardModifiers)
{
    if (margin == 1)
        toggleBookmarkAtLine(line);
}

void EditorWidget::toggleBookmarkAtLine(int line)
{
    const unsigned mask = markersAtLine(line);
    if (mask & (1 << kBookmarkMarker))
        markerDelete(line, kBookmarkMarker);
    else
        markerAdd(line, kBookmarkMarker);
}

void EditorWidget::toggleBookmark()
{
    int line = 0, index = 0;
    getCursorPosition(&line, &index);
    toggleBookmarkAtLine(line);
}

void EditorWidget::nextBookmark()
{
    int line = 0, index = 0;
    getCursorPosition(&line, &index);
    const int mask = 1 << kBookmarkMarker;
    int found = markerFindNext(line + 1, mask);
    if (found < 0)
        found = markerFindNext(0, mask);  // 循環
    if (found >= 0)
        setCursorPosition(found, 0);
}

void EditorWidget::prevBookmark()
{
    int line = 0, index = 0;
    getCursorPosition(&line, &index);
    const int mask = 1 << kBookmarkMarker;
    int found = markerFindPrevious(line - 1, mask);
    if (found < 0)
        found = markerFindPrevious(lines() - 1, mask);  // 循環
    if (found >= 0)
        setCursorPosition(found, 0);
}

// === 折疊 ===
void EditorWidget::foldAllBlocks(bool contract)
{
    SendScintilla(SCI_FOLDALL, contract ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
}

void EditorWidget::foldCurrent(bool contract)
{
    int line = 0, col = 0;
    getCursorPosition(&line, &col);
    // 找到包含此行的可折疊標頭
    int header = line;
    while (header > 0 && !(SendScintilla(SCI_GETFOLDLEVEL, static_cast<unsigned long>(header))
                           & SC_FOLDLEVELHEADERFLAG))
        --header;
    const bool expanded = SendScintilla(SCI_GETFOLDEXPANDED, static_cast<unsigned long>(header)) != 0;
    if (expanded == contract)  // 需要改變狀態時才切換
        SendScintilla(SCI_TOGGLEFOLD, static_cast<unsigned long>(header));
}

void EditorWidget::foldToLevel(int level)
{
    // 先全部展開，再把 >= 指定層的標頭收合
    SendScintilla(SCI_FOLDALL, SC_FOLDACTION_EXPAND);
    const int total = static_cast<int>(SendScintilla(SCI_GETLINECOUNT));
    for (int i = 0; i < total; ++i) {
        const long lvl = SendScintilla(SCI_GETFOLDLEVEL, static_cast<unsigned long>(i));
        if (!(lvl & SC_FOLDLEVELHEADERFLAG))
            continue;
        const int depth = static_cast<int>(lvl & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE;
        if (depth >= level - 1
            && SendScintilla(SCI_GETFOLDEXPANDED, static_cast<unsigned long>(i)))
            SendScintilla(SCI_TOGGLEFOLD, static_cast<unsigned long>(i));
    }
}

// === 縮排 ===
void EditorWidget::indentSelection()   { SendScintilla(SCI_TAB); }
void EditorWidget::unindentSelection() { SendScintilla(SCI_BACKTAB); }

// === 行操作 ===
void EditorWidget::joinSelectedLines()
{
    SendScintilla(SCI_TARGETFROMSELECTION);
    SendScintilla(SCI_LINESJOIN);
}

void EditorWidget::splitSelectedLines()
{
    SendScintilla(SCI_TARGETFROMSELECTION);
    // 以目前編輯寬度為換行像素寬（0 = 依 edge column）
    SendScintilla(SCI_LINESSPLIT, 0);
}

// === 區塊註解 ===
// 依語言選區塊註解符號；回傳 {開, 閉}，不支援則空。
static QPair<QString, QString> blockCommentTokens(const QString &lang)
{
    const QString l = lang.toLower();
    if (l == "html" || l == "xml")
        return {QStringLiteral("<!--"), QStringLiteral("-->")};
    if (l == "css")
        return {QStringLiteral("/*"), QStringLiteral("*/")};
    if (l == "python" || l == "ruby" || l == "bash" || l == "perl"
        || l == "makefile" || l == "yaml" || l == "properties" || l == "cmake"
        || l == "tcl" || l == "r")
        return {};  // 這些語言無標準區塊註解，交給行註解
    // C 家族與多數語言預設 /* */
    return {QStringLiteral("/*"), QStringLiteral("*/")};
}

void EditorWidget::toggleBlockComment()
{
    QsciLexer *lex = lexer();
    const QString lang = lex ? QString::fromLatin1(lex->language()) : QString();
    const auto tk = blockCommentTokens(lang);
    if (tk.first.isEmpty())
        return;  // 無區塊註解符號

    if (!hasSelectedText()) {
        // 無選取：包住當前行
        int line = 0, col = 0;
        getCursorPosition(&line, &col);
        setSelection(line, 0, line, lineLength(line));
    }
    QString sel = selectedText();
    const bool commented = sel.trimmed().startsWith(tk.first)
                           && sel.trimmed().endsWith(tk.second);
    if (commented) {
        QString t = sel;
        const int a = t.indexOf(tk.first);
        t.remove(a, tk.first.length());
        const int b = t.lastIndexOf(tk.second);
        if (b >= 0)
            t.remove(b, tk.second.length());
        replaceSelectedText(t);
    } else {
        replaceSelectedText(tk.first + sel + tk.second);
    }
}

// === 書籤進階 ===
QList<int> EditorWidget::bookmarkedLines() const
{
    QList<int> out;
    const int mask = 1 << kBookmarkMarker;
    auto *self = const_cast<EditorWidget *>(this);
    int line = self->markerFindNext(0, mask);
    while (line >= 0) {
        out.append(line);
        line = self->markerFindNext(line + 1, mask);
    }
    return out;
}

void EditorWidget::clearAllBookmarks()
{
    markerDeleteAll(kBookmarkMarker);
}

void EditorWidget::removeBookmarkedLines()
{
    const QList<int> lines = bookmarkedLines();
    beginUndoAction();
    for (int i = lines.size() - 1; i >= 0; --i) {  // 由下往上刪，位置不位移
        const int ln = lines.at(i);
        const long start = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(ln));
        const long end = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(ln + 1));
        SendScintilla(SCI_DELETERANGE, static_cast<unsigned long>(start),
                      static_cast<long>(end - start));
    }
    endUndoAction();
}

void EditorWidget::removeNonBookmarkedLines()
{
    const QList<int> keep = bookmarkedLines();
    QList<int> keepSet = keep;
    const int total = static_cast<int>(lines());
    beginUndoAction();
    for (int ln = total - 1; ln >= 0; --ln) {
        if (keepSet.contains(ln))
            continue;
        const long start = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(ln));
        const long end = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(ln + 1));
        SendScintilla(SCI_DELETERANGE, static_cast<unsigned long>(start),
                      static_cast<long>(end - start));
    }
    endUndoAction();
}

void EditorWidget::inverseBookmarks()
{
    const QList<int> marked = bookmarkedLines();
    QList<int> markedList = marked;
    const int total = static_cast<int>(lines());
    const int mask = 1 << kBookmarkMarker;
    for (int ln = 0; ln < total; ++ln) {
        if (markedList.contains(ln))
            markerDelete(ln, kBookmarkMarker);
        else
            markerAdd(ln, kBookmarkMarker);
    }
    Q_UNUSED(mask);
}

QString EditorWidget::bookmarkedText() const
{
    QStringList parts;
    for (int ln : bookmarkedLines())
        parts << text(ln).trimmed();
    return parts.join(QChar('\n'));
}

// === 選取括號之間 ===
void EditorWidget::selectBetweenBraces()
{
    const long caret = SendScintilla(SCI_GETCURRENTPOS);
    // 往回找開括號
    long open = caret;
    while (open > 0) {
        --open;
        const char c = static_cast<char>(SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(open)));
        if (c == '{' || c == '(' || c == '[') {
            const long match = SendScintilla(SCI_BRACEMATCH, static_cast<unsigned long>(open), 0L);
            if (match > caret - 1) {
                SendScintilla(SCI_SETSELECTION, static_cast<unsigned long>(open + 1),
                              static_cast<long>(match));
                return;
            }
        }
    }
}

void EditorWidget::showCallTip(const QString &text)
{
    if (text.isEmpty())
        return;
    const long pos = SendScintilla(SCI_GETCURRENTPOS);
    // SCI_CALLTIPSHOW 會複製字串，臨時 QByteArray 即可
    const QByteArray bytes = text.toUtf8();
    SendScintilla(SCI_CALLTIPSHOW, static_cast<unsigned long>(pos), bytes.constData());
}

void EditorWidget::cancelCallTip()
{
    SendScintilla(SCI_CALLTIPCANCEL);
}

void EditorWidget::keyPressEvent(QKeyEvent *event)
{
    QsciScintilla::keyPressEvent(event);
    // 鍵入 '(' → 取其前的識別字，請上層查函式簽名顯示 call tip
    if (event->text() == QLatin1String("(")) {
        const long pos = SendScintilla(SCI_GETCURRENTPOS);
        const long paren = pos - 1;
        if (paren <= 0)
            return;
        const long ws = SendScintilla(SCI_WORDSTARTPOSITION,
                                      static_cast<unsigned long>(paren), 1L);
        if (ws >= paren)
            return;
        QString name;
        for (long p = ws; p < paren; ++p)
            name += QChar(static_cast<char>(
                SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p))));
        if (!name.trimmed().isEmpty())
            emit callTipRequested(name);
    }
}

EditorWidget::DocStats EditorWidget::stats()
{
    DocStats s;
    const long docBytes = SendScintilla(SCI_GETLENGTH);
    // 以字元數計（多位元組編碼下與位元組數不同）——Notepad++ 的「length」欄即字元數
    s.length = SendScintilla(SCI_COUNTCHARACTERS, 0UL, static_cast<long>(docBytes));
    s.lines  = static_cast<int>(SendScintilla(SCI_GETLINECOUNT));

    int line = 0, col = 0;
    getCursorPosition(&line, &col);
    s.line = line + 1;
    s.col  = col + 1;

    const long cur = SendScintilla(SCI_GETCURRENTPOS);
    s.pos = SendScintilla(SCI_COUNTCHARACTERS, 0UL, static_cast<long>(cur)) + 1;

    const long selStart = SendScintilla(SCI_GETSELECTIONSTART);
    const long selEnd   = SendScintilla(SCI_GETSELECTIONEND);
    if (selEnd > selStart) {
        s.selChars = SendScintilla(SCI_COUNTCHARACTERS,
                                   static_cast<unsigned long>(selStart),
                                   static_cast<long>(selEnd));
        const int l0 = static_cast<int>(
            SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(selStart)));
        const int l1 = static_cast<int>(
            SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(selEnd)));
        s.selLines = l1 - l0 + 1;
    }

    s.overtype = SendScintilla(SCI_GETOVERTYPE) != 0;
    return s;
}

}  // namespace macpad::core
