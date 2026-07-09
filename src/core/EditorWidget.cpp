#include "core/EditorWidget.h"

#include "core/LexerFactory.h"
#include "features/autocomplete/ApiDatabase.h"

#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPair>
#include <QVector>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QStringConverter>
#include <QTextCodec>
#include <QTextStream>

#include <Qsci/qscilexer.h>
#include <Qsci/qsciapis.h>

#include <algorithm>

namespace macpad::core {

namespace {
// 變更歷史（Change History）——本專案鎖定之 QScintilla/Scintilla 版本未匯出這組常數，
// 依 Scintilla 官方文件手動定義訊息碼與旗標值（FR-057）。若執行期的 Scintilla build
// 不認得這些訊息，SendScintilla 對未知訊息一律安全地 no-op（回傳 0），故本機制可優雅降級。
constexpr int kSciSetChangeHistory = 2780;   // SCI_SETCHANGEHISTORY
constexpr int kScChangeHistoryDisabled  = 0x0;  // SC_CHANGE_HISTORY_DISABLED
constexpr int kScChangeHistoryEnabled   = 0x1;  // SC_CHANGE_HISTORY_ENABLED
constexpr int kScChangeHistoryMarkers   = 0x2;  // SC_CHANGE_HISTORY_MARKERS
constexpr int kScChangeHistoryIndicators = 0x4; // SC_CHANGE_HISTORY_INDICATORS

// 變更歷史 marker 編號（依 Scintilla 文件保留區段，避開本檔已用的書籤(1)/折疊(25-31)）
constexpr int kMarkerHistoryRevertedToOrigin = 21;
constexpr int kMarkerHistorySaved            = 22;
constexpr int kMarkerHistoryModified         = 23;
constexpr int kMarkerHistoryRevertedToModified = 24;
constexpr int kChangeHistoryMarkerMask =
    (1 << kMarkerHistoryRevertedToOrigin) | (1 << kMarkerHistorySaved)
    | (1 << kMarkerHistoryModified) | (1 << kMarkerHistoryRevertedToModified);

constexpr int kChangeHistoryMargin = 2;  // margin 0=行號、1=書籤、2=變更歷史

// 選取／指示器相關 Scintilla 訊息與旗標一律使用 QsciScintillaBase 已匯出的繼承 enum
// （SCI_GETSELECTIONNSTART / SCI_INDICATORVALUEAT / SC_SEL_STREAM…），不再手動定義，
// 以免與鎖定版本的實際訊息碼不符而被 SendScintilla 當成未知訊息 no-op。

constexpr ushort kRedactMaskChar = 0x25CF;  // U+25CF ● 遮罩字元

// 路徑片段允許的字元（限定 ASCII，確保「字元數＝UTF-8 位元組數」，
// 讓 onUserListActivated 能直接以字元數當作 Scintilla byte-range 刪除長度）。
bool isPathChar(QChar c)
{
    if (c.unicode() > 127)
        return false;
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
        || c == QLatin1Char('.') || c == QLatin1Char('/') || c == QLatin1Char('~');
}

// === 貼上為純文字（Paste Special）共用 ===
// 盡力去除 HTML 標籤：移除 <script>/<style> 區塊、所有標籤，並將常見具名/數值實體解回字元。
QString stripHtmlToPlainText(QString html)
{
    static const QRegularExpression scriptOrStyle(
        QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    html.remove(scriptOrStyle);
    static const QRegularExpression brTag(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    html.replace(brTag, QStringLiteral("\n"));
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    html.remove(tag);
    html.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    html.replace(QLatin1String("&amp;"), QLatin1String("&"));
    html.replace(QLatin1String("&lt;"), QLatin1String("<"));
    html.replace(QLatin1String("&gt;"), QLatin1String(">"));
    html.replace(QLatin1String("&quot;"), QLatin1String("\""));
    html.replace(QLatin1String("&#39;"), QLatin1String("'"));
    return html;
}

// 盡力去除 RTF 控制詞/群組，保留純文字內容（不支援完整 RTF 語法，僅供 best-effort 貼上）。
QString stripRtfToPlainText(const QString &rtf)
{
    QString out;
    out.reserve(rtf.size());
    int depth = 0;
    for (int i = 0; i < rtf.size(); ++i) {
        const QChar c = rtf.at(i);
        if (c == QLatin1Char('\\')) {
            // 控制詞：\word[数字] 後接可選一個空白；控制符號：\ 後接單一非字母字元
            int j = i + 1;
            if (j < rtf.size() && rtf.at(j).isLetter()) {
                while (j < rtf.size() && rtf.at(j).isLetter())
                    ++j;
                while (j < rtf.size() && (rtf.at(j).isDigit() || rtf.at(j) == QLatin1Char('-')))
                    ++j;
                if (j < rtf.size() && rtf.at(j) == QLatin1Char(' '))
                    ++j;
            } else if (j < rtf.size()) {
                if (rtf.at(j) == QLatin1Char('\'') && j + 2 < rtf.size()) {
                    // \'hh 十六進位跳脫字元
                    bool ok = false;
                    const int code = rtf.mid(j + 1, 2).toInt(&ok, 16);
                    if (ok)
                        out += QChar(code);
                    j += 3;
                } else {
                    ++j;  // 略過控制符號本身
                }
            }
            i = j - 1;
            continue;
        }
        if (c == QLatin1Char('{')) { ++depth; continue; }
        if (c == QLatin1Char('}')) { if (depth > 0) --depth; continue; }
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n'))
            continue;
        out += c;
    }
    return out;
}
}  // namespace

EditorWidget::EditorWidget(QWidget *parent)
    : QsciScintilla(parent)
{
    applyDefaultConfig();

    // dirty 狀態變化轉發（FR-014：分頁未存標記 ●）
    connect(this, &QsciScintilla::modificationChanged,
            this, &EditorWidget::dirtyChanged);

    // 路徑自動完成候選被選取（Ctrl+Alt+Space 觸發，見 triggerPathCompletion）
    connect(this, &QsciScintilla::userListActivated,
            this, &EditorWidget::onUserListActivated);

    // 智慧高亮：游標移動時重標游標所在字詞的所有出現處（僅在開啟時作動）
    connect(this, &QsciScintilla::cursorPositionChanged,
            this, &EditorWidget::onCursorPositionChanged);

    // 攔截 viewport 雙擊事件：支援 Ctrl/⌘+雙擊選整個字（ctrlDoubleClickWholeWord）
    viewport()->installEventFilter(this);

    // 停用 Scintilla 內建右鍵 popup（SCI_USEPOPUP, SC_POPUP_NEVER=0），
    // 改由 contextMenuEvent 轉發給 MainWindow 建構完整的 Notepad++ 風格右鍵選單。
    SendScintilla(SCI_USEPOPUP, 0UL);
}

EditorWidget::~EditorWidget()
{
    // 銷毀前先收斂 API 準備：cancelPreparation() 通知背景 worker 提早中止，
    // 再 delete（~QsciAPIs 會等待 worker thread 結束）——確保沒有 in-flight 的
    // worker 於物件釋放後回呼造成 SIGBUS。必須在 QsciScintilla base dtor 之前完成。
    if (m_apis) {
        m_apis->cancelPreparation();
        delete m_apis;
        m_apis = nullptr;
    }
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

    // 智慧高亮指示器（kSmartIndicator）——柔和黃綠底框，與 Mark 區隔
    SendScintilla(SCI_INDICSETSTYLE, static_cast<unsigned long>(kSmartIndicator), INDIC_ROUNDBOX);
    SendScintilla(SCI_INDICSETFORE, static_cast<unsigned long>(kSmartIndicator), 0x0000D7FFUL);
    SendScintilla(SCI_INDICSETALPHA, static_cast<unsigned long>(kSmartIndicator), 80);

    // 詞彙上色 5 色指示器（kTokenIndicatorBase..+4）——各具不同顏色，供多詞彙同時區別
    static const unsigned long kTokenColors[5] = {
        0x00F08072UL,  // salmon
        0x0090EE90UL,  // light green
        0x00DDA0DDUL,  // plum
        0x00FFD700UL,  // gold
        0x0087CEFAUL,  // light sky blue
    };
    for (int i = 0; i < 5; ++i) {
        const int ind = kTokenIndicatorBase + i;
        SendScintilla(SCI_INDICSETSTYLE, static_cast<unsigned long>(ind), INDIC_ROUNDBOX);
        SendScintilla(SCI_INDICSETFORE, static_cast<unsigned long>(ind), kTokenColors[i]);
        SendScintilla(SCI_INDICSETALPHA, static_cast<unsigned long>(ind), 90);
    }

    // 標籤配對高亮指示器（kTagMatchIndicator）——實線外框，橘色，明顯區隔於智慧高亮
    SendScintilla(SCI_INDICSETSTYLE, static_cast<unsigned long>(kTagMatchIndicator), INDIC_STRAIGHTBOX);
    SendScintilla(SCI_INDICSETFORE, static_cast<unsigned long>(kTagMatchIndicator), 0x0000A5FFUL);
    SendScintilla(SCI_INDICSETALPHA, static_cast<unsigned long>(kTagMatchIndicator), 70);
    SendScintilla(SCI_INDICSETOUTLINEALPHA, static_cast<unsigned long>(kTagMatchIndicator), 180);

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
    // setLexer 只切換內部指標、不刪舊 lexer；舊 lexer 掛在 this 下會累積，故手動刪除。設 nullptr 為純文字。
    QsciLexer *old = QsciScintilla::lexer();
    QsciLexer *lexer = LexerFactory::createForFileName(path, this);
    setLexer(lexer);
    if (old && old != lexer)
        delete old;
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
    // setLexer 不刪舊 lexer；手動刪除以免掛在 this 下累積
    QsciLexer *old = QsciScintilla::lexer();
    setLexer(lexer);
    if (old && old != lexer)
        delete old;
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
    clearDirty();
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
    clearDirty();
    return true;
}

QString EditorWidget::displayName() const
{
    const QString base = isUntitled()
        ? (m_untitledNumber > 0 ? QStringLiteral("untitled(%1)").arg(m_untitledNumber)
                                : QStringLiteral("Untitled"))
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

void EditorWidget::markMetaDirty()
{
    // setModified(true) 在 save-point 為 no-op，無法反映純中繼資料變更，
    // 故以 m_metaDirty 補足。僅在由乾淨轉髒時補發 dirtyChanged(true)，避免重複。
    const bool wasDirty = isDirty();
    m_metaDirty = true;
    if (!wasDirty)
        emit dirtyChanged(true);
}

void EditorWidget::clearDirty()
{
    // 文件轉為乾淨（載入/儲存/重讀）。setModified(false) 會在文字曾被修改時
    // 透過 modificationChanged→dirtyChanged 自動發送 false；此處僅補發
    // 「只有 m_metaDirty 為髒、文字未修改」的情況，避免與自動訊號重複發送。
    const bool metaWasDirty = m_metaDirty;
    const bool wasModified = isModified();
    m_metaDirty = false;
    setModified(false);
    if (metaWasDirty && !wasModified)
        emit dirtyChanged(false);
}

void EditorWidget::setEncoding(Encoding enc)
{
    // 選 Unicode 編碼會覆蓋先前手選的傳統 codec
    if (m_encoding == enc && m_codecName.isEmpty())
        return;
    m_encoding = enc;
    m_codecName.clear();
    markMetaDirty();   // 標記 dirty（FR-019）
    emit metaChanged();
}

QString EditorWidget::encodingLabel() const
{
    return m_codecName.isEmpty() ? FileEncoding::encodingName(m_encoding) : m_codecName;
}

void EditorWidget::setSaveCodec(const QString &codecName)
{
    m_codecName = codecName;
    markMetaDirty();   // 標記 dirty（FR-019）
    emit metaChanged();
}

bool EditorWidget::reinterpretWithCodec(const QString &codecName, QString *errorMessage)
{
    // codec 不存在則明確失敗（IL-4 失敗快失敗明），避免靜默回退 UTF-8 卻回報成功
    if (!QTextCodec::codecForName(codecName.toLatin1())) {
        if (errorMessage)
            *errorMessage = QStringLiteral("不支援的編碼：") + codecName;
        return false;
    }
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
    clearDirty();   // 純重新解讀磁碟內容，視為未變更
    emit metaChanged();
    return true;
}

bool EditorWidget::reinterpretAsEncoding(Encoding enc, QString *errorMessage)
{
    // 與 reinterpretWithCodec 對稱，但走內建 Encoding enum（Unicode 系列），不查 codec（FR-048）
    if (m_filePath.isEmpty()) {
        // 未存檔：無原始位元組可重讀 → 僅設為目標編碼
        setEncoding(enc);
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
    setText(FileEncoding::decode(raw, enc));
    applyEolMode(m_eol);
    m_encoding = enc;
    m_codecName.clear();   // 改以內建 enum 編碼存檔，清掉先前手選的傳統 codec
    clearDirty();   // 純重新解讀磁碟內容，視為未變更
    emit metaChanged();
    return true;
}

void EditorWidget::convertEol(Eol eol)
{
    m_eol = eol;
    applyEolMode(eol);
    convertEols(eolMode());  // 立即轉換既有內容（FR-020）
    markMetaDirty();   // 標記 dirty（FR-020）
    emit metaChanged();
}

void EditorWidget::applyNewDocumentDefaults(Eol eol, Encoding enc)
{
    // 新建空白文件：套用偏好預設但不標記 dirty（尚無使用者變更，不應顯示 ●）
    m_eol = eol;
    m_encoding = enc;
    m_codecName.clear();
    applyEolMode(eol);
    emit metaChanged();
}

int EditorWidget::replaceAll(const QString &find, const QString &replaceStr,
                             bool regex, bool caseSensitive, bool wholeWord)
{
    return replaceAll(find, replaceStr, regex, caseSensitive, wholeWord, /*dotAll=*/false);
}

int EditorWidget::replaceAll(const QString &find, const QString &replaceStr,
                             bool regex, bool caseSensitive, bool wholeWord, bool dotAll)
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
        if (regex && dotAll)
            opts |= QRegularExpression::DotMatchesEverythingOption;  // 「. matches newline」（FR-047）
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
        // 整份 target 取代會刪除全域範圍、連帶清掉行標記；先記錄書籤行號，取代後還原（FR-008）
        const QList<int> marks = bookmarkedLines();
        const QByteArray bytes = content.toUtf8();
        beginUndoAction();
        SendScintilla(SCI_SETTARGETSTART, 0UL);
        SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(length()));
        SendScintilla(SCI_REPLACETARGET,
                      static_cast<unsigned long>(bytes.size()), bytes.constData());
        endUndoAction();
        const int total = static_cast<int>(lines());
        for (int ln : marks) {
            if (ln < total)  // 取代可能改變行數，跳過已不存在的行
                markerAdd(ln, kBookmarkMarker);
        }
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

int EditorWidget::countMatches(const QString &find, bool regex, bool caseSensitive, bool wholeWord)
{
    // 僅操作 SCI target（SCI_SEARCHINTARGET），不觸碰選取/游標，故天然不移動游標、不變更選取（FR-047）
    if (find.isEmpty())
        return 0;

    const QByteArray fb = find.toUtf8();
    int flags = 0;
    if (caseSensitive) flags |= SCFIND_MATCHCASE;
    if (wholeWord)     flags |= SCFIND_WHOLEWORD;
    if (regex)         flags |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
    SendScintilla(SCI_SETSEARCHFLAGS, flags);

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
        ++count;
        start = (me > ms) ? me : me + 1;  // 空匹配前進一位
    }
    return count;
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
        markerDelete(ln, kBookmarkMarker);  // 先清書籤標記，避免刪行後 Scintilla 殘留 marker
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

void EditorWidget::cutBookmarkedLines()
{
    // 先複製書籤行文字到剪貼簿，再刪除（FR-049）
    const QString txt = bookmarkedText();
    if (!txt.isEmpty())
        QGuiApplication::clipboard()->setText(txt);
    removeBookmarkedLines();
}

void EditorWidget::pasteReplaceBookmarkedLines()
{
    // 以剪貼簿文字逐行依序取代各書籤行內容（FR-049）。
    // 剪貼簿行數不足時，多出的書籤行清空（簡化行為，於此註記說明）。
    const QList<int> marks = bookmarkedLines();
    if (marks.isEmpty())
        return;
    const QStringList clipLines = QGuiApplication::clipboard()->text().split(QChar('\n'));

    beginUndoAction();
    for (int i = 0; i < marks.size(); ++i) {
        const int ln = marks.at(i);
        const QString replacement = (i < clipLines.size()) ? clipLines.at(i) : QString();
        const long start = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(ln));
        const long lineEnd = SendScintilla(SCI_GETLINEENDPOSITION, static_cast<unsigned long>(ln));
        SendScintilla(SCI_SETTARGETSTART, static_cast<unsigned long>(start));
        SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(lineEnd));
        const QByteArray bytes = replacement.toUtf8();
        SendScintilla(SCI_REPLACETARGET,
                      static_cast<unsigned long>(bytes.size()), bytes.constData());
    }
    endUndoAction();
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
                // 以高階 setSelection() 設定選取，才會同步更新 QsciScintilla 的
                // 選取快取（hasSelectedText()/selectedText()）——低階 SCI_SETSELECTION
                // 只改 Scintilla 內部選取，Copy/Cut 取不到內容。
                int lineFrom = 0, indexFrom = 0, lineTo = 0, indexTo = 0;
                lineIndexFromPosition(static_cast<int>(open + 1), &lineFrom, &indexFrom);
                lineIndexFromPosition(static_cast<int>(match), &lineTo, &indexTo);
                setSelection(lineFrom, indexFrom, lineTo, indexTo);
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

void EditorWidget::triggerCallTip()
{
    // 手動觸發（如快捷鍵）：取游標前的識別字並發出 callTipRequested，
    // 邏輯與 keyPressEvent 鍵入 '(' 時相同，但以目前游標位置為終點，無需剛鍵入 '('。
    const long pos = SendScintilla(SCI_GETCURRENTPOS);
    if (pos <= 0)
        return;
    const long ws = SendScintilla(SCI_WORDSTARTPOSITION, static_cast<unsigned long>(pos), 1L);
    if (ws >= pos)
        return;
    // setUtf8(true) → 位置為位元組偏移；先收集原始位元組再以 UTF-8 解碼（同 keyPressEvent 註解）
    QByteArray nameBytes;
    for (long p = ws; p < pos; ++p)
        nameBytes += static_cast<char>(
            SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p)));
    const QString name = QString::fromUtf8(nameBytes);
    if (!name.trimmed().isEmpty())
        emit callTipRequested(name);
}

QChar EditorWidget::closerFor(QChar opener)
{
    // 供 keyPressEvent 與測試共用（自動配對符號，FR-050）
    switch (opener.unicode()) {
    case '(': return QChar(u')');
    case '[': return QChar(u']');
    case '{': return QChar(u'}');
    case '"': return QChar(u'"');
    case '\'': return QChar(u'\'');
    default:  return QChar();
    }
}

void EditorWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // 停用 Scintilla 內建 popup（見建構子 SCI_USEPOPUP）後，右鍵改由此攔截並轉發
    // 全域座標給上層（MainWindow）建構完整的 Notepad++ 風格右鍵選單（複刻 contextMenu.xml）。
    event->accept();
    emit contextMenuRequested(event->globalPos());
}

void EditorWidget::keyPressEvent(QKeyEvent *event)
{
    // 路徑自動完成手動觸發（Ctrl+Alt+Space）：攔截於 base 處理之前，
    // 避免 Option+Space 被當一般輸入插入字元（如 macOS IM 的不斷行空白）。
    if (event->key() == Qt::Key_Space
        && (event->modifiers() & Qt::ControlModifier)
        && (event->modifiers() & Qt::AltModifier)) {
        triggerPathCompletion();
        event->accept();
        return;
    }

    const QString typed = event->text();
    QsciScintilla::keyPressEvent(event);

    // 自動配對符號（FR-050）：鍵入開符號後，緊接插入對應閉符號並讓游標留在中間
    if (m_autoClose && typed.size() == 1) {
        const QChar opener = typed.at(0);
        const QChar closer = closerFor(opener);
        if (!closer.isNull()) {
            bool insertCloser = true;
            if (opener == QLatin1Char('"') || opener == QLatin1Char('\'')) {
                // 引號：若開引號前一字元是文字/數字/底線，視為在字詞內部，不自動配對
                const long pos = SendScintilla(SCI_GETCURRENTPOS);
                const long beforeOpener = pos - 2;  // pos-1 為剛輸入的開引號本身
                if (beforeOpener >= 0) {
                    const char c = static_cast<char>(
                        SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(beforeOpener)));
                    if (QChar(QLatin1Char(c)).isLetterOrNumber() || c == '_')
                        insertCloser = false;
                }
            }
            if (insertCloser) {
                const long pos = SendScintilla(SCI_GETCURRENTPOS);
                const QByteArray cb = QString(closer).toUtf8();
                SendScintilla(SCI_INSERTTEXT, static_cast<unsigned long>(pos), cb.constData());
                SendScintilla(SCI_GOTOPOS, static_cast<unsigned long>(pos));  // 游標留在兩符號之間
            }
        }
    }

    // HTML/XML 自動閉合標籤：鍵入 '>' 後，若游標前為完整開啟標籤（<tag ...>），
    // 自動於游標後補上 </tag>，並讓游標停留在標籤之間（同樣受 m_autoClose 節制）。
    if (m_autoClose && typed == QLatin1String(">")) {
        const long pos = SendScintilla(SCI_GETCURRENTPOS);
        const long line = SendScintilla(SCI_LINEFROMPOSITION, static_cast<unsigned long>(pos));
        const long lineStart = SendScintilla(SCI_POSITIONFROMLINE, static_cast<unsigned long>(line));
        QByteArray beforeBytes;
        for (long p = lineStart; p < pos; ++p)
            beforeBytes += static_cast<char>(
                SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p)));
        const QString closing = closingTagFor(QString::fromUtf8(beforeBytes));
        if (!closing.isEmpty()) {
            const QByteArray cb = closing.toUtf8();
            SendScintilla(SCI_INSERTTEXT, static_cast<unsigned long>(pos), cb.constData());
            SendScintilla(SCI_GOTOPOS, static_cast<unsigned long>(pos));  // 游標留在標籤之間
        }
    }

    // 鍵入 '(' → 取其前的識別字，請上層查函式簽名顯示 call tip（FR-N/A，可由偏好設定關閉）
    if (m_callTips && typed == QLatin1String("(")) {
        const long pos = SendScintilla(SCI_GETCURRENTPOS);
        const long paren = pos - 1;
        if (paren <= 0)
            return;
        const long ws = SendScintilla(SCI_WORDSTARTPOSITION,
                                      static_cast<unsigned long>(paren), 1L);
        if (ws >= paren)
            return;
        // setUtf8(true) → 位置為位元組偏移；先收集原始位元組再以 UTF-8 解碼，
        // 避免把多位元組字元逐位元組當成 Latin-1 而破壞國際化識別字
        QByteArray nameBytes;
        for (long p = ws; p < paren; ++p)
            nameBytes += static_cast<char>(
                SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p)));
        const QString name = QString::fromUtf8(nameBytes);
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

// === 變更歷史（FR-057）===
void EditorWidget::setChangeHistoryEnabled(bool enabled)
{
    m_changeHistoryEnabled = enabled;
    if (enabled) {
        SendScintilla(kSciSetChangeHistory,
                      static_cast<unsigned long>(kScChangeHistoryEnabled
                                                  | kScChangeHistoryMarkers
                                                  | kScChangeHistoryIndicators));
        // 變更歷史邊欄：以符號邊欄顯示異動標記；不支援的 build 下邊欄僅維持空白，無害。
        setMarginType(kChangeHistoryMargin, QsciScintilla::SymbolMargin);
        setMarginWidth(kChangeHistoryMargin, 4);
        setMarginSensitivity(kChangeHistoryMargin, false);
        setMarginMarkerMask(kChangeHistoryMargin,
                             static_cast<unsigned int>(kChangeHistoryMarkerMask));
    } else {
        SendScintilla(kSciSetChangeHistory,
                      static_cast<unsigned long>(kScChangeHistoryDisabled));
        setMarginWidth(kChangeHistoryMargin, 0);
    }
}

void EditorWidget::goToNextChange()
{
    if (!m_changeHistoryEnabled)
        return;  // 未啟用時安全跳過（優雅降級）
    int line = 0, col = 0;
    getCursorPosition(&line, &col);
    int found = markerFindNext(line + 1, kChangeHistoryMarkerMask);
    if (found >= 0)
        setCursorPosition(found, 0);
}

void EditorWidget::goToPrevChange()
{
    if (!m_changeHistoryEnabled)
        return;
    int line = 0, col = 0;
    getCursorPosition(&line, &col);
    int found = markerFindPrevious(line - 1, kChangeHistoryMarkerMask);
    if (found >= 0)
        setCursorPosition(found, 0);
}

// === 虛擬空間（FR-060）===
void EditorWidget::setVirtualSpace(bool enabled)
{
    m_virtualSpace = enabled;
    SendScintilla(SCI_SETVIRTUALSPACEOPTIONS,
                  enabled ? (SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE) : SCVS_NONE);
}

// === 多重選取指令（FR-060）===
void EditorWidget::selectNextOccurrence()
{
    SendScintilla(SCI_MULTIPLESELECTADDNEXT);
}

void EditorWidget::selectAllOccurrences(bool matchCase, bool wholeWord)
{
    if (!hasSelectedText()) {
        // 無選取：以游標所在字詞為搜尋依據
        const long pos = SendScintilla(SCI_GETCURRENTPOS);
        const long wordStart = SendScintilla(SCI_WORDSTARTPOSITION, static_cast<unsigned long>(pos), 1L);
        const long wordEnd = SendScintilla(SCI_WORDENDPOSITION, static_cast<unsigned long>(pos), 1L);
        if (wordEnd <= wordStart)
            return;  // 游標不在字詞內，無可選取目標
        int lf = 0, iff = 0, lt = 0, it = 0;
        lineIndexFromPosition(static_cast<int>(wordStart), &lf, &iff);
        lineIndexFromPosition(static_cast<int>(wordEnd), &lt, &it);
        setSelection(lf, iff, lt, it);
    }
    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    SendScintilla(SCI_SETSEARCHFLAGS, static_cast<unsigned long>(flags));
    SendScintilla(SCI_SETTARGETSTART, 0UL);
    SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(length()));
    SendScintilla(SCI_MULTIPLESELECTADDEACH);
}

void EditorWidget::skipAndSelectNext()
{
    // 類似 VSCode 的「skip」：丟棄目前（最後加入的）選取，改選下一個相符項目
    const int n = static_cast<int>(SendScintilla(SCI_GETSELECTIONS));
    if (n > 1)
        SendScintilla(SCI_DROPSELECTIONN, static_cast<unsigned long>(n - 1));
    SendScintilla(SCI_MULTIPLESELECTADDNEXT);
}

void EditorWidget::undoLastMultiSelect()
{
    // skipAndSelectNext 的反向操作：只丟棄最後加入的選取區域，不加選下一個相符項目
    const int n = static_cast<int>(SendScintilla(SCI_GETSELECTIONS));
    if (n > 1)
        SendScintilla(SCI_DROPSELECTIONN, static_cast<unsigned long>(n - 1));
}

// === Preferences 即時套用 ===
void EditorWidget::setShowLineNumbers(bool show)
{
    m_showLineNumbers = show;
    if (show) {
        // 與 applyDefaultConfig 相同公式重算動態寬度（依目前字型量測，非固定值）
        setMarginLineNumbers(0, true);
        const QFontMetrics fm(font());
        setMarginWidth(0, fm.horizontalAdvance(QStringLiteral("0000")) + 8);
    } else {
        setMarginLineNumbers(0, false);
        setMarginWidth(0, 0);
    }
}

void EditorWidget::setCaretWidth(int px)
{
    // Scintilla 僅接受 0..3 像素（SC_CARETSTYLE 無關的獨立設定）；夾限避免非法值
    m_caretWidth = qBound(0, px, 3);
    SendScintilla(SCI_SETCARETWIDTH, static_cast<unsigned long>(m_caretWidth));
}

void EditorWidget::setWordCompletionEnabled(bool enabled)
{
    m_wordCompletion = enabled;
    if (enabled) {
        // 若已套用 API 自動完成（m_apis 非空），改用文件+API 合併來源；否則僅文件字詞。
        setAutoCompletionSource(m_apis ? QsciScintilla::AcsAll : QsciScintilla::AcsDocument);
        setAutoCompletionThreshold(2);
    } else {
        setAutoCompletionSource(QsciScintilla::AcsNone);
    }
}

void EditorWidget::triggerWordCompletion()
{
    // 手動強制觸發（如快捷鍵）：無視 autoCompletionThreshold，立即顯示清單。
    // 來源與 setWordCompletionEnabled 相同規則：有 API 來源時文件字詞+API 合併，否則僅文件字詞。
    if (m_apis)
        autoCompleteFromAll();
    else
        autoCompleteFromDocument();
}

// === API 自動完成（FR-055 hook）===
void EditorWidget::applyApiCompletions(const QStringList &entries)
{
    QsciLexer *lex = lexer();
    if (!lex)
        return;  // 無 lexer 時無語言可套用 API，安全跳過

    // 先取消並刪除前一組 QsciAPIs（避免多次呼叫累積洩漏、以及舊 worker thread 懸空）。
    // ~QsciAPIs 會等待背景 worker 收斂，delete 前先 cancelPreparation() 讓其提早中止。
    if (m_apis) {
        m_apis->cancelPreparation();
        delete m_apis;
        m_apis = nullptr;
    }

    auto *apis = new QsciAPIs(lex);  // 建構時 parent 為 lex
    // 改由 EditorWidget 持有：與 lexer 生命週期解耦，換 lexer/刪舊 lexer 時 m_apis 不會被連帶刪除而懸空。
    apis->setParent(this);
    for (const QString &entry : entries)
        apis->add(entry);
    apis->prepare();       // 於背景 thread 進行；銷毀時由 dtor 負責收斂，避免 SIGBUS
    lex->setAPIs(apis);
    m_apis = apis;

    setAutoCompletionSource(QsciScintilla::AcsAll);  // 文件字詞 + API 合併來源
    setAutoCompletionThreshold(2);
}

// === 路徑自動完成（手動觸發）===
QString EditorWidget::pathFragmentBefore(const QString &text, int pos)
{
    const int clamped = qBound(0, pos, text.size());
    int start = clamped;
    while (start > 0 && isPathChar(text.at(start - 1)))
        --start;
    return text.mid(start, clamped - start);
}

void EditorWidget::triggerPathCompletion()
{
    // 游標為位元組偏移（setUtf8(true)）；先換算為字元索引，才能與 text() 的 QString 對齊。
    const long cur = SendScintilla(SCI_GETCURRENTPOS);
    const int charPos = static_cast<int>(SendScintilla(SCI_COUNTCHARACTERS, 0UL, cur));
    const QString fragment = pathFragmentBefore(text(), charPos);
    if (fragment.isEmpty())
        return;

    const QStringList results = macpad::features::ApiDatabase::completePath(fragment);
    if (results.isEmpty())
        return;

    // completePath 回傳的是候選「檔名」（不含目錄），選取後只需取代已輸入的檔名前綴部分。
    const QString prefix = QFileInfo(fragment).fileName();
    m_pathCompletionPrefixLen = prefix.size();
    showUserList(kPathCompletionListId, results);
}

void EditorWidget::onUserListActivated(int id, const QString &string)
{
    if (id != kPathCompletionListId)
        return;

    const long pos = SendScintilla(SCI_GETCURRENTPOS);
    if (m_pathCompletionPrefixLen > 0) {
        const long delStart = pos - m_pathCompletionPrefixLen;
        if (delStart >= 0)
            SendScintilla(SCI_DELETERANGE, static_cast<unsigned long>(delStart),
                          static_cast<long>(m_pathCompletionPrefixLen));
    }
    m_pathCompletionPrefixLen = 0;

    const long insertPos = SendScintilla(SCI_GETCURRENTPOS);
    const QByteArray bytes = string.toUtf8();
    SendScintilla(SCI_INSERTTEXT, static_cast<unsigned long>(insertPos), bytes.constData());
    SendScintilla(SCI_GOTOPOS, static_cast<unsigned long>(insertPos + bytes.size()));
}

// === 兩段式選取（Begin/End Select）===
void EditorWidget::beginSelect()
{
    m_selectAnchorPos = SendScintilla(SCI_GETCURRENTPOS);
    m_hasSelectAnchor = true;
}

void EditorWidget::endSelect()
{
    if (!m_hasSelectAnchor)
        return;  // 未先 beginSelect → no-op
    // 確保串流模式（避免殘留矩形模式影響），再以高階 setSelection 同步選取快取
    SendScintilla(SCI_SETSELECTIONMODE, static_cast<unsigned long>(SC_SEL_STREAM));
    const long caret = SendScintilla(SCI_GETCURRENTPOS);
    int lf = 0, iff = 0, lt = 0, it = 0;
    lineIndexFromPosition(static_cast<int>(m_selectAnchorPos), &lf, &iff);
    lineIndexFromPosition(static_cast<int>(caret), &lt, &it);
    setSelection(lf, iff, lt, it);
}

void EditorWidget::beginColumnSelect()
{
    // 與 beginSelect 相同：記錄錨點（模式差異在 end 端呈現）
    m_selectAnchorPos = SendScintilla(SCI_GETCURRENTPOS);
    m_hasSelectAnchor = true;
}

void EditorWidget::endColumnSelect()
{
    if (!m_hasSelectAnchor)
        return;
    const long caret = SendScintilla(SCI_GETCURRENTPOS);
    if (m_columnSelectionToMultiEdit) {
        // 偏好開啟：不產生矩形選取，改為每行相同欄位各放一個多重編輯插入點
        applyMultiEditCaretsForRectangle(m_selectAnchorPos, caret);
        return;
    }
    SendScintilla(SCI_SETSELECTIONMODE, static_cast<unsigned long>(SC_SEL_RECTANGLE));
    SendScintilla(SCI_SETRECTANGULARSELECTIONANCHOR, static_cast<unsigned long>(m_selectAnchorPos));
    SendScintilla(SCI_SETRECTANGULARSELECTIONCARET, static_cast<unsigned long>(caret));
}

void EditorWidget::applyMultiEditCaretsForRectangle(long anchorPos, long caretPos)
{
    int la = 0, ca = 0, lc = 0, cc = 0;
    lineIndexFromPosition(static_cast<int>(anchorPos), &la, &ca);
    lineIndexFromPosition(static_cast<int>(caretPos), &lc, &cc);
    const int firstLine = std::min(la, lc);
    const int lastLine = std::max(la, lc);
    const int col = cc;  // 以終點（目前游標）欄位為準，逐行放置插入點

    SendScintilla(SCI_SETSELECTIONMODE, static_cast<unsigned long>(SC_SEL_STREAM));
    bool first = true;
    for (int ln = firstLine; ln <= lastLine; ++ln) {
        const int useCol = std::min(col, lineLength(ln));
        const long pos = positionFromLineIndex(ln, useCol);
        if (first) {
            SendScintilla(SCI_SETSELECTION, static_cast<unsigned long>(pos), static_cast<unsigned long>(pos));
            first = false;
        } else {
            SendScintilla(SCI_ADDSELECTION, static_cast<unsigned long>(pos), static_cast<unsigned long>(pos));
        }
    }
}

// === 遮蔽選取（Redact Selection）===
void EditorWidget::redactSelection()
{
    const int n = static_cast<int>(SendScintilla(SCI_GETSELECTIONS));
    if (n <= 0)
        return;

    // 收集所有選取範圍（位元組偏移），依起點由後往前排序，取代時不位移前面尚未處理的範圍。
    QList<QPair<long, long>> ranges;
    for (int i = 0; i < n; ++i) {
        const long s = SendScintilla(SCI_GETSELECTIONNSTART, static_cast<unsigned long>(i));
        const long e = SendScintilla(SCI_GETSELECTIONNEND, static_cast<unsigned long>(i));
        if (e > s)
            ranges.append(qMakePair(s, e));
    }
    if (ranges.isEmpty())
        return;
    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<long, long> &a, const QPair<long, long> &b) {
                  return a.first > b.first;
              });

    beginUndoAction();
    for (const auto &r : ranges) {
        // 讀取範圍原始位元組並解碼，逐字元換成遮罩（保留換行）
        QByteArray raw;
        for (long p = r.first; p < r.second; ++p)
            raw += static_cast<char>(SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p)));
        const QString original = QString::fromUtf8(raw);
        QString masked;
        masked.reserve(original.size());
        for (const QChar ch : original) {
            if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
                masked.append(ch);  // 換行保留
            else
                masked.append(QChar(kRedactMaskChar));
        }
        const QByteArray mb = masked.toUtf8();
        SendScintilla(SCI_SETTARGETSTART, static_cast<unsigned long>(r.first));
        SendScintilla(SCI_SETTARGETEND, static_cast<unsigned long>(r.second));
        SendScintilla(SCI_REPLACETARGET,
                      static_cast<unsigned long>(mb.size()), mb.constData());
    }
    endUndoAction();
}

// === 智慧高亮 / 詞彙上色 共用 ===
void EditorWidget::clearIndicatorRange(int indicator)
{
    SendScintilla(SCI_SETINDICATORCURRENT, static_cast<unsigned long>(indicator));
    SendScintilla(SCI_INDICATORCLEARRANGE, 0UL, static_cast<unsigned long>(length()));
}

int EditorWidget::fillWordOccurrences(const QString &word, int indicator)
{
    if (word.isEmpty())
        return 0;
    const QByteArray fb = word.toUtf8();
    SendScintilla(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
    SendScintilla(SCI_SETINDICATORCURRENT, static_cast<unsigned long>(indicator));

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

QString EditorWidget::wordUnderCaret() const
{
    auto *self = const_cast<EditorWidget *>(this);
    const long pos = self->SendScintilla(SCI_GETCURRENTPOS);
    const long ws = self->SendScintilla(SCI_WORDSTARTPOSITION, static_cast<unsigned long>(pos), 1L);
    const long we = self->SendScintilla(SCI_WORDENDPOSITION, static_cast<unsigned long>(pos), 1L);
    if (we <= ws)
        return QString();
    QByteArray raw;
    for (long p = ws; p < we; ++p)
        raw += static_cast<char>(self->SendScintilla(SCI_GETCHARAT, static_cast<unsigned long>(p)));
    return QString::fromUtf8(raw);
}

void EditorWidget::setSmartHighlight(bool enabled)
{
    m_smartHighlight = enabled;
    if (enabled)
        onCursorPositionChanged();  // 立即依目前游標標記一次
    else
        clearIndicatorRange(kSmartIndicator);  // 關閉時清除既有標記
}

void EditorWidget::onCursorPositionChanged()
{
    // 智慧高亮與標籤配對高亮為獨立開關，各自處理（避免其一關閉時影響另一）。
    if (m_smartHighlight) {
        clearIndicatorRange(kSmartIndicator);
        const QString word = wordUnderCaret();
        if (!word.isEmpty())  // 游標不在字詞內：僅清除
            fillWordOccurrences(word, kSmartIndicator);
    }
    if (m_highlightMatchingTags)
        updateTagMatchHighlight();
}

// === 標示相符標籤（Highlight Matching Tags）===
void EditorWidget::setHighlightMatchingTags(bool enabled)
{
    m_highlightMatchingTags = enabled;
    if (enabled)
        updateTagMatchHighlight();  // 立即依目前游標標記一次
    else
        clearIndicatorRange(kTagMatchIndicator);  // 關閉時清除既有標記
}

void EditorWidget::updateTagMatchHighlight()
{
    clearIndicatorRange(kTagMatchIndicator);
    const QString content = text();
    const QByteArray bytes = content.toUtf8();
    const long caretByte = SendScintilla(SCI_GETCURRENTPOS);
    // Scintilla 位置以位元組計、matchingTagRanges 以字元計：先把游標位元組位置轉為字元索引。
    const long clamped = qBound<long>(0, caretByte, static_cast<long>(bytes.size()));
    const int caretChar = QString::fromUtf8(bytes.constData(), static_cast<int>(clamped)).size();

    int os = 0, oe = 0, cs = 0, ce = 0;
    if (!matchingTagRanges(content, caretChar, &os, &oe, &cs, &ce))
        return;

    SendScintilla(SCI_SETINDICATORCURRENT, static_cast<unsigned long>(kTagMatchIndicator));
    // 字元範圍 → 位元組範圍後填色
    auto fill = [&](int cStart, int cEnd) {
        if (cEnd <= cStart)
            return;
        const int bStart = content.left(cStart).toUtf8().size();
        const int bLen = content.mid(cStart, cEnd - cStart).toUtf8().size();
        SendScintilla(SCI_INDICATORFILLRANGE, static_cast<unsigned long>(bStart),
                      static_cast<unsigned long>(bLen));
    };
    fill(os, oe);
    if (cs != os || ce != oe)  // 自閉合標籤兩範圍相同：只填一次
        fill(cs, ce);
}

bool EditorWidget::matchingTagRanges(const QString &text, int caretChar,
                                     int *openStart, int *openEnd,
                                     int *closeStart, int *closeEnd)
{
    // 標籤 token：[start,end) 為含 '<' 與 '>' 的字元範圍；kind：0=開啟 1=閉合 2=自閉合/其他（不配對）
    struct Tag { int start; int end; QString name; int kind; };

    auto tagName = [](const QString &s) -> QString {
        int j = 0;
        while (j < s.size() && s.at(j).isSpace())
            ++j;
        int k = j;
        if (k < s.size() && (s.at(k).isLetter() || s.at(k) == QLatin1Char('_'))) {
            ++k;
            while (k < s.size()) {
                const QChar c = s.at(k);
                if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')
                    || c == QLatin1Char(':'))
                    ++k;
                else
                    break;
            }
        }
        return s.mid(j, k - j);
    };

    QVector<Tag> tags;
    const int n = text.size();
    int i = 0;
    while (i < n) {
        if (text.at(i) != QLatin1Char('<')) {
            ++i;
            continue;
        }
        const int lt = i;
        // 略過註解 <!-- -->（其內部的 '<'/'>' 不視為標籤）
        if (text.mid(lt, 4) == QLatin1String("<!--")) {
            const int endc = text.indexOf(QLatin1String("-->"), lt + 4);
            i = (endc < 0) ? n : endc + 3;
            continue;
        }
        const int gt = text.indexOf(QLatin1Char('>'), lt + 1);
        if (gt < 0)
            break;
        const QString inner = text.mid(lt + 1, gt - lt - 1);
        int kind;
        QString name;
        if (inner.startsWith(QLatin1Char('!')) || inner.startsWith(QLatin1Char('?'))) {
            kind = 2;  // 宣告/PI：不配對
        } else if (inner.startsWith(QLatin1Char('/'))) {
            kind = 1;  // 閉合標籤
            name = tagName(inner.mid(1));
        } else if (inner.trimmed().endsWith(QLatin1Char('/'))) {
            kind = 2;  // 自閉合標籤：只標示自身
            name = tagName(inner);
        } else {
            kind = 0;  // 開啟標籤
            name = tagName(inner);
        }
        tags.append({lt, gt + 1, name, kind});
        i = gt + 1;
    }

    // 找出游標所在標籤（範圍不重疊；允許游標剛好落在 '>' 之後）
    int cur = -1;
    for (int t = 0; t < tags.size(); ++t) {
        if (tags.at(t).start <= caretChar && caretChar <= tags.at(t).end) {
            cur = t;
            break;
        }
    }
    if (cur < 0 || tags.at(cur).name.isEmpty())
        return false;

    const Tag ct = tags.at(cur);
    auto setResult = [&](const Tag &open, const Tag &close) {
        *openStart = open.start;
        *openEnd = open.end;
        *closeStart = close.start;
        *closeEnd = close.end;
    };

    if (ct.kind == 2) {  // 自閉合：開啟/閉合範圍相同
        setResult(ct, ct);
        return true;
    }
    if (ct.kind == 0) {  // 開啟標籤：往後找配對閉合，計深度
        int depth = 0;
        for (int t = cur; t < tags.size(); ++t) {
            if (tags.at(t).name != ct.name)
                continue;
            if (tags.at(t).kind == 0) {
                ++depth;
            } else if (tags.at(t).kind == 1) {
                --depth;
                if (depth == 0) {
                    setResult(ct, tags.at(t));
                    return true;
                }
            }
        }
        return false;
    }
    // ct.kind == 1 閉合標籤：往前找配對開啟，計深度
    int depth = 0;
    for (int t = cur; t >= 0; --t) {
        if (tags.at(t).name != ct.name)
            continue;
        if (tags.at(t).kind == 1) {
            ++depth;
        } else if (tags.at(t).kind == 0) {
            --depth;
            if (depth == 0) {
                setResult(tags.at(t), ct);
                return true;
            }
        }
    }
    return false;
}

// === Ctrl/⌘+雙擊選整個字 / 摺疊邊界樣式 ===
bool EditorWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(event);
        const bool wantWholeWord =
            m_ctrlDoubleClickWholeWord && me->button() == Qt::LeftButton
            && (me->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
        if (wantWholeWord) {
            const QPoint p = me->position().toPoint();
            const long pos = SendScintilla(SCI_POSITIONFROMPOINT,
                                           static_cast<unsigned long>(p.x()),
                                           static_cast<long>(p.y()));
            if (pos >= 0) {
                // 以「預設字元集」（英數 + '_'，忽略 delimiter 覆寫）於字元空間計算整詞邊界。
                const QString content = text();
                const QByteArray bytes = content.toUtf8();
                const long clamped = qBound<long>(0, pos, static_cast<long>(bytes.size()));
                const int caretChar =
                    QString::fromUtf8(bytes.constData(), static_cast<int>(clamped)).size();
                auto isWord = [](QChar c) {
                    return c.isLetterOrNumber() || c == QLatin1Char('_');
                };
                int ws = caretChar;
                int we = caretChar;
                while (ws > 0 && isWord(content.at(ws - 1)))
                    --ws;
                while (we < content.size() && isWord(content.at(we)))
                    ++we;
                if (we > ws) {
                    const int bws = content.left(ws).toUtf8().size();
                    const int bwe = content.left(we).toUtf8().size();
                    SendScintilla(SCI_SETSELECTION, static_cast<unsigned long>(bws),
                                  static_cast<long>(bwe));
                    return true;  // 已自行處理，消化事件避免預設雙擊覆寫選取
                }
            }
        }
    }
    return QsciScintilla::eventFilter(watched, event);
}

void EditorWidget::setFoldMarginStyle(int style)
{
    // 對應 persistence::FoldMarginStyle 序位；折疊邊欄固定為 margin 2（見 applyDefaultConfig）。
    switch (style) {
    case 0:  // None：停用折疊邊欄
        setFolding(QsciScintilla::NoFoldStyle, 2);
        break;
    case 2:  // Arrow：以箭頭符號覆寫折疊 marker
        setFolding(QsciScintilla::PlainFoldStyle, 2);
        SendScintilla(SCI_MARKERDEFINE, static_cast<unsigned long>(SC_MARKNUM_FOLDER),
                      static_cast<long>(SC_MARK_ARROW));
        SendScintilla(SCI_MARKERDEFINE, static_cast<unsigned long>(SC_MARKNUM_FOLDEROPEN),
                      static_cast<long>(SC_MARK_ARROWDOWN));
        break;
    case 3:  // Circle：圓形樹狀
        setFolding(QsciScintilla::CircledTreeFoldStyle, 2);
        break;
    case 4:  // Box：方框樹狀
        setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
        break;
    case 1:  // Simple：加減號
    default:
        setFolding(QsciScintilla::PlainFoldStyle, 2);
        break;
    }
}

// === 詞彙上色（5 色）===
void EditorWidget::styleTokenOccurrences(int colorIndex)
{
    const int idx = qBound(0, colorIndex, 4);
    const int indicator = kTokenIndicatorBase + idx;
    // 優先使用選取文字，否則取游標所在字詞
    const QString word = hasSelectedText() ? selectedText() : wordUnderCaret();
    if (word.isEmpty())
        return;
    clearIndicatorRange(indicator);  // 同色重標：先清該色再填
    fillWordOccurrences(word, indicator);
}

void EditorWidget::clearStyledTokens()
{
    for (int i = 0; i < 5; ++i)
        clearIndicatorRange(kTokenIndicatorBase + i);
}

int EditorWidget::indicatorRangeCount(int indicator) const
{
    auto *self = const_cast<EditorWidget *>(this);
    const long end = self->length();
    int count = 0;
    long pos = 0;
    while (pos < end) {
        const long on = self->SendScintilla(SCI_INDICATORVALUEAT,
                                            static_cast<unsigned long>(indicator),
                                            static_cast<long>(pos));
        const long rangeEnd = self->SendScintilla(SCI_INDICATOREND,
                                                  static_cast<unsigned long>(indicator),
                                                  static_cast<long>(pos));
        if (rangeEnd <= pos)
            break;  // 安全防護：避免不前進而無限迴圈
        if (on)
            ++count;
        pos = rangeEnd;
    }
    return count;
}

// === HTML/XML 自動閉合標籤（純函式）===
QString EditorWidget::closingTagFor(const QString &textBeforeCaret)
{
    // 取游標前最後一個 '<' 起的片段，須以 '>' 結尾方視為完整標籤
    const int lt = textBeforeCaret.lastIndexOf(QLatin1Char('<'));
    if (lt < 0)
        return QString();
    const int gt = textBeforeCaret.indexOf(QLatin1Char('>'), lt);
    // '>' 必須是游標前的最後一字元（即剛鍵入者）
    if (gt < 0 || gt != textBeforeCaret.size() - 1)
        return QString();

    QString inner = textBeforeCaret.mid(lt + 1, gt - lt - 1);
    if (inner.isEmpty())
        return QString();
    // 閉合標籤 </...>、宣告/註解 <!... 或 <?...、自閉合 <.../> 一律不補
    const QChar first = inner.at(0);
    if (first == QLatin1Char('/') || first == QLatin1Char('!') || first == QLatin1Char('?'))
        return QString();
    if (inner.trimmed().endsWith(QLatin1Char('/')))
        return QString();

    // 擷取標籤名稱：起始字母，其後可含字母/數字/'-'/'_'/':'（涵蓋 XML 命名空間）
    if (!first.isLetter())
        return QString();
    int i = 1;
    while (i < inner.size()) {
        const QChar c = inner.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')
            || c == QLatin1Char(':'))
            ++i;
        else
            break;
    }
    const QString name = inner.left(i);
    if (name.isEmpty())
        return QString();
    return QStringLiteral("</") + name + QStringLiteral(">");
}

// === 貼上為純文字（Edit ▸ Paste Special）===
void EditorWidget::pasteAsHtml()
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    QString text;
    if (md && md->hasHtml())
        text = stripHtmlToPlainText(md->html());
    else if (md && md->hasText())
        text = md->text();  // 無 HTML 負載 → 退回一般純文字貼上
    else
        return;
    if (text.isEmpty())
        return;
    beginUndoAction();
    replaceSelectedText(text);
    endUndoAction();
}

void EditorWidget::pasteAsRtf()
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    QString text;
    if (md && (md->hasFormat(QStringLiteral("text/rtf")) || md->hasFormat(QStringLiteral("application/rtf")))) {
        const QByteArray raw = md->hasFormat(QStringLiteral("text/rtf"))
                                    ? md->data(QStringLiteral("text/rtf"))
                                    : md->data(QStringLiteral("application/rtf"));
        text = stripRtfToPlainText(QString::fromLatin1(raw));  // RTF 控制層為 ASCII，內文以 \'hh 跳脫非 ASCII
    } else if (md && md->hasText()) {
        text = md->text();  // 無 RTF 負載 → 退回一般純文字貼上
    } else {
        return;
    }
    if (text.isEmpty())
        return;
    beginUndoAction();
    replaceSelectedText(text);
    endUndoAction();
}

// === 插入日期／時間（Edit ▸ Insert Date/Time）===
void EditorWidget::insertDateShort()
{
    const QString text = QLocale::system().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
    beginUndoAction();
    replaceSelectedText(text);
    endUndoAction();
}

void EditorWidget::insertDateLong()
{
    const QString text = QLocale::system().toString(QDateTime::currentDateTime(), QLocale::LongFormat);
    beginUndoAction();
    replaceSelectedText(text);
    endUndoAction();
}

void EditorWidget::insertDateCustom(const QString &format)
{
    if (format.isEmpty())
        return;
    const QString text = QDateTime::currentDateTime().toString(format);
    beginUndoAction();
    replaceSelectedText(text);
    endUndoAction();
}

}  // namespace macpad::core
