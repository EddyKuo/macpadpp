#pragma once

// EditorWidget — QsciScintilla 的封裝（core 層）
// v1 起始：行號邊欄、折疊邊欄、括號配對、多游標、依副檔名語法高亮、載入/儲存。
// 對應 FR-003/004/005/007/008/014（部分）。後續拆分 buffer/view 模型見 Architecture §3。

#include <Qsci/qsciscintilla.h>
#include <QString>
#include <QStringList>
#include <QList>

#include "core/FileEncoding.h"

class QsciAPIs;

namespace macpad::core {

class EditorWidget : public QsciScintilla {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget *parent = nullptr);
    ~EditorWidget() override;

    // 從磁碟載入檔案內容（UTF-8）；成功回傳 true。失敗時 errorMessage 帶原因（FR-014 邊界）。
    bool loadFile(const QString &path, QString *errorMessage = nullptr);

    // 儲存至指定路徑（UTF-8）；成功回傳 true 並更新 filePath/dirty。
    bool saveFile(const QString &path, QString *errorMessage = nullptr);

    QString filePath() const { return m_filePath; }
    bool isUntitled() const { return m_filePath.isEmpty(); }

    // ── OS 層檔案唯讀屬性（複刻 Notepad++ Edit ▸ Clear Read-Only Flag）──────────────
    // 與 QsciScintilla::isReadOnly()（app 內編輯鎖）不同：這裡問的是「磁碟上的檔案本身
    // 是否唯讀」。Qt 的權限 API 本就跨平台——Windows 對應 FILE_ATTRIBUTE_READONLY、
    // macOS/Linux 對應 POSIX 寫入權限位元——故兩平台共用同一份實作，無需 #ifdef。
    // 純靜態、不碰 widget 狀態，便於無頭單元測試。
    static bool isFileReadOnly(const QString &path);
    // 設定/清除唯讀屬性；成功回傳 true，失敗時 errorMessage 帶原因。
    static bool setFileReadOnly(const QString &path, bool readOnly, QString *errorMessage = nullptr);

    // 目前分頁對應的磁碟檔案是否唯讀（untitled 或檔案不存在 → false）。
    bool isFileReadOnly() const { return isFileReadOnly(m_filePath); }

    // 「政策性唯讀」：因 -fullReadOnly 或監控（tail -f）模式而鎖定，而非因檔案屬性。
    // isReadOnly() 單一布林無法分辨鎖定原因，Clear Read-Only Flag 若無條件解鎖，會誤破
    // 這兩種鎖定的不變式。故政策鎖定另記一個旗標，清除檔案屬性時據此拒絕解鎖。
    void setPolicyReadOnly(bool on)
    {
        m_policyReadOnly = on;
        setReadOnly(on);
    }
    bool isPolicyReadOnly() const { return m_policyReadOnly; }

    // 從拖放/剪貼簿的 mime 資料取出「本機既有檔案」路徑清單（非本機 URL、目錄、
    // 不存在的路徑一律略過）。純函式、不碰 widget 狀態，供無頭單元測試。
    // 回傳空清單即代表「這不是檔案拖放」，呼叫端應讓事件回到原本的文字拖放行為。
    static QStringList localFilePathsFromMime(const QMimeData *mime);

    // 檔案拖放的實際處置：是檔案拖放就發出 filesDropped 並回傳 true（呼叫端應吃掉事件），
    // 否則回傳 false（呼叫端應把事件交還 Scintilla 的文字拖放）。
    // 之所以獨立成 public 方法而非只寫在 eventFilter 裡：Qt6 的 QApplication::notify()
    // 對拖放事件有特殊派送路徑，合成 QDropEvent 送不進 event filter，故無法用合成事件
    // 做端對端測試；把決策邏輯抽出來才能真正被單元測試涵蓋。
    bool handleFileDropMime(const QMimeData *mime);

    // 未命名分頁序號（複刻 Notepad++「new N」）：>0 時 displayName 顯示 untitled(N) 以區分多個未存分頁。
    // 由 MainWindow::addEditorTab 於建立時指派最小可用號；命名後（有 filePath）即不再顯示。
    int untitledNumber() const { return m_untitledNumber; }
    void setUntitledNumber(int n) { m_untitledNumber = n; }
    bool isDirty() const { return isModified() || m_metaDirty; }

    // 顯示用標題：檔名或 "Untitled"，dirty 時前綴 ●
    QString displayName() const;

    Encoding encoding() const { return m_encoding; }
    Eol eol() const { return m_eol; }

    // 狀態列統計（複刻 Notepad++ 狀態列各欄位）——一次算齊避免多次 IPC。
    // 長度/位置以「字元數」計（非位元組），與 Notepad++ 顯示一致。
    struct DocStats {
        long length = 0;      // 全文字元數
        int  lines = 1;       // 行數
        int  line = 1;        // 游標行（1-based）
        int  col = 1;         // 游標欄（1-based）
        long pos = 1;         // 游標字元位置（1-based）
        long selChars = 0;    // 選取字元數（0 = 無選取）
        int  selLines = 0;    // 選取涵蓋行數
        bool overtype = false;// 覆寫模式（OVR）
    };
    DocStats stats();

    // 變更目標編碼（下次儲存生效；標記 dirty）——FR-019
    void setEncoding(Encoding enc);

    // 狀態列顯示用編碼名稱（具名 codec 優先，否則 enum 名稱）
    QString encodingLabel() const;

    // 以具名 codec（Big5/Shift-JIS…）重新解讀磁碟上的檔案內容（複刻 NP++ Character sets）。
    // 有檔路徑時：重讀原始位元組並以該 codec 解碼取代內容；無檔時：僅設為存檔 codec。
    bool reinterpretWithCodec(const QString &codecName, QString *errorMessage = nullptr);
    // 僅設定存檔用 codec（不重讀），標記 dirty
    void setSaveCodec(const QString &codecName);
    QString saveCodec() const { return m_codecName; }
    // 轉換 EOL（立即套用到內容 + 編輯器模式；標記 dirty）——FR-020
    void convertEol(Eol eol);

    // 新建空白文件套用偏好之預設 EOL/編碼（不標記 dirty，因尚無使用者變更）——FR-053
    void applyNewDocumentDefaults(Eol eol, Encoding enc);

    // 全域取代（FR-010/011）——以 Scintilla target API 批次執行，單次 undo。
    // 效能路徑（NFR-004）：避免逐一 findNext/replace 的高階呼叫開銷。
    // 回傳取代次數。
    int replaceAll(const QString &find, const QString &replaceStr,
                   bool regex, bool caseSensitive, bool wholeWord);

    // 多載：額外接受 dotAll（regex 下 . 是否匹配換行，FR-047「. matches newline」）。
    // 保留原簽名不變，供既有呼叫端相容；新呼叫端可用本多載開啟 dotAll。
    int replaceAll(const QString &find, const QString &replaceStr,
                   bool regex, bool caseSensitive, bool wholeWord, bool dotAll);

    // Mark：高亮所有匹配（FR-012），回傳匹配數；clearMarks 清除。
    int markAll(const QString &find, bool regex, bool caseSensitive, bool wholeWord);
    void clearMarks();
    static constexpr int kMarkIndicator = 0;

    // 書籤（FR-008）
    void toggleBookmark();               // 切換目前行書籤
    void nextBookmark();                 // 跳至下一書籤（循環）
    void prevBookmark();                 // 跳至上一書籤（循環）

    // 書籤 marker 編號（margin 1 符號邊欄）
    static constexpr int kBookmarkMarker = 1;

    // 以目前檔名重新套用內建 lexer（產生全新預設色的 lexer，供主題重新上色，避免疊加降飽和）
    void reapplyLexer() { applyLexerForPath(m_filePath); }

    // 手動指定 lexer（Language 選單）；設等寬字型並發 lexerChanged 供重新上主題色。nullptr = 純文字。
    void setLanguageLexer(QsciLexer *lexer);

    // === 折疊（View ▸ Fold）===
    void foldAllBlocks(bool contract);   // true=全部收合；false=全部展開（避免與 QsciScintilla::foldAll 衝突）
    void foldCurrent(bool contract);     // 收合/展開目前區塊
    void foldToLevel(int level);         // 收合到第 level 層（1..8）

    // === 縮排（Edit ▸ Indent）===
    void indentSelection();
    void unindentSelection();

    // === 行操作補充（Edit ▸ Line Operations）===
    void joinSelectedLines();
    void splitSelectedLines();

    // === 區塊註解（Edit ▸ Comment）===
    // 依目前 lexer 的語言用對應區塊符號（/* */、<!-- -->、# 等）包住選取；無選取則作用於當前行。
    void toggleBlockComment();

    // === 書籤進階（Search ▸ Bookmark）===
    QList<int> bookmarkedLines() const;
    void clearAllBookmarks();
    void removeBookmarkedLines();        // 刪除所有書籤行
    void removeNonBookmarkedLines();     // 刪除所有未加書籤的行
    void inverseBookmarks();             // 反轉書籤（有↔無）
    QString bookmarkedText() const;      // 所有書籤行文字（供複製）

    // === 選取（Search ▸ Select All In-between braces）===
    void selectBetweenBraces();

    // === 唯讀 ===
    // 直接用 QsciScintilla::setReadOnly/isReadOnly

    // === Call tip（函式參數提示）===
    void showCallTip(const QString &text);   // 於游標處顯示 call tip
    // 多載版本：複刻 Notepad++ 的多載切換——同時只顯示一個簽名，並在首行加上
    // 「▲ n of m ▼」箭頭，點擊箭頭可循環切換（Scintilla 的 SCN_CALLTIPCLICK）。
    // 只有一個簽名時不加箭頭，外觀與單一簽名版本相同。
    void showCallTips(const QStringList &overloads);
    void cancelCallTip();
    // 目前顯示的是第幾個多載（0-based）；未顯示多載時為 -1。
    // Scintilla 會自行關閉 call tip（Esc、失焦、繼續打字）且不通知本 widget，
    // 故此處以 SCI_CALLTIPACTIVE 實際查詢，避免回報「已經關掉的提示」的陳舊索引。
    int currentCallTipOverload() const;
    // 強制對游標前識別字發出 callTipRequested（不需鍵入 '(' 觸發），供上層綁定快捷鍵手動觸發
    void triggerCallTip();

    // === 尋找計數（FR-047，供 Find/Replace 對話框「Count」按鈕）===
    // 計算全文匹配數，不移動游標、不改變選取（以 SCI_SEARCHINTARGET 掃描後還原游標）。
    int countMatches(const QString &find, bool regex, bool caseSensitive, bool wholeWord);

    // === 以內建 Encoding 重新解讀磁碟內容（FR-048，類 reinterpretWithCodec 但用 enum）===
    // 有檔路徑時：重讀原始位元組並以 enc 解碼取代內容；無檔時：僅設為目標編碼。
    bool reinterpretAsEncoding(Encoding enc, QString *errorMessage = nullptr);

    // === 書籤剪貼簿操作（FR-049）===
    void cutBookmarkedLines();              // 複製書籤行文字到剪貼簿後刪除
    void pasteReplaceBookmarkedLines();      // 以剪貼簿逐行文字依序取代各書籤行內容

    // === 自動配對符號（FR-050）===
    void setAutoClose(bool enabled) { m_autoClose = enabled; }
    bool autoClose() const { return m_autoClose; }
    // 依開符號回傳對應閉符號；不支援則回傳空字元（供測試與 keyPressEvent 共用）
    static QChar closerFor(QChar opener);

    // === 變更歷史（Edit ▸ Change History，FR-057）===
    // 依 Scintilla build 是否支援優雅降級：不支援時 SendScintilla 對未知訊息一律 no-op，
    // 標記位仍會更新，但邊欄/跳轉不會有實際效果。
    void setChangeHistoryEnabled(bool enabled);
    bool changeHistoryEnabled() const { return m_changeHistoryEnabled; }
    void goToNextChange();   // 跳至下一處變更（依 change markers）
    void goToPrevChange();   // 跳至上一處變更（依 change markers）

    // === 虛擬空間（Edit ▸ Virtual Space，FR-060）===
    void setVirtualSpace(bool enabled);
    bool virtualSpace() const { return m_virtualSpace; }

    // === 多重選取指令（Edit ▸ Multi-select，FR-060）===
    void selectNextOccurrence();     // 將下一個相符項目加入選取
    // matchCase/wholeWord 預設值與原行為相同（區分大小寫、不限整詞），供 MainWindow 提供 4 種變化
    void selectAllOccurrences(bool matchCase = true, bool wholeWord = false);
    void skipAndSelectNext();        // 略過目前最後加入的選取，改選下一個相符項目
    // 丟棄最近一次加入的多重選取區域，不加選下一個相符項目（skipAndSelectNext 的反向操作）
    void undoLastMultiSelect();

    // === API 自動完成（FR-055 hook）===
    // entries 由上層 ApiDatabase 模組提供；無 lexer 時安全跳過（無 lexer 即無語言可套 API）。
    void applyApiCompletions(const QStringList &entries);

    // === 即時套用之偏好設定（Preferences ▸ 套用到已開啟編輯器）===
    // 行號邊欄顯示/隱藏；true 時依 applyDefaultConfig 相同公式重算動態寬度。
    void setShowLineNumbers(bool show);
    bool showLineNumbers() const { return m_showLineNumbers; }

    // 插入點（caret）寬度（像素，0..3）。覆寫 QsciScintilla::setCaretWidth 以額外記錄目前值。
    void setCaretWidth(int px) override;
    int caretWidth() const { return m_caretWidth; }

    // 字詞自動完成開關（文件內字詞來源）；關閉時完全停用自動完成彈出。
    void setWordCompletionEnabled(bool enabled);
    bool wordCompletionEnabled() const { return m_wordCompletion; }

    // 無視 autoCompletionThreshold，強制立即顯示自動完成清單（有 API 來源時含 API，否則僅文件字詞）
    void triggerWordCompletion();

    // Call tip（函式參數提示）開關；關閉時鍵入 '(' 不再發出 callTipRequested。
    void setCallTipsEnabled(bool enabled) { m_callTips = enabled; }
    bool callTipsEnabled() const { return m_callTips; }

    // === 路徑自動完成（手動觸發，Ctrl+Alt+Space）===
    // 取游標前的路徑片段，交由 ApiDatabase::completePath 查詢候選並以 showUserList 顯示。
    void triggerPathCompletion();

    // 由完整文字與游標前字元位置（0-based）萃取路徑片段（向前掃描路徑字元，含 '/'）。
    // 純函式，供 keyPressEvent/triggerPathCompletion 與單元測試共用。
    static QString pathFragmentBefore(const QString &text, int pos);

    // === 兩段式選取（Begin/End Select，串流模式）===
    // beginSelect 記錄目前游標為選取錨點；endSelect 以錨點到目前游標設定選取。
    // 未先呼叫 beginSelect（無錨點）時 endSelect 為 no-op。
    void beginSelect();
    void endSelect();
    // 欄位（矩形）版本：與上相同流程但以矩形選取模式（SC_SEL_RECTANGLE）呈現。
    void beginColumnSelect();
    void endColumnSelect();

    // === 遮蔽選取（Redact Selection）===
    // 將目前所有選取範圍內的字元以遮罩字元（U+25CF ●）取代，換行字元保留；單次 undo。
    void redactSelection();

    // === 智慧高亮（Smart Highlighting）===
    // 開啟後，游標移動時自動標記游標所在字詞的所有出現處（專用指示器 kSmartIndicator）。
    // 每次游標移動先清除再重標；游標不在字詞內時只清除。
    void setSmartHighlight(bool enabled);
    bool smartHighlight() const { return m_smartHighlight; }

    // === 標示相符標籤（Highlight Matching Tags）===
    // 開啟後，游標移入 HTML/XML 標籤時，以專用指示器同時標示開啟與閉合標籤配對
    // （自閉合標籤只標示自身）。每次游標移動先清除再重標。
    void setHighlightMatchingTags(bool enabled);
    bool highlightMatchingTags() const { return m_highlightMatchingTags; }

    // 純函式：由全文與游標「字元位置」找出游標所在標籤及其配對標籤的字元範圍。
    // 命中回傳 true，並以 [start,end) 字元範圍填入開啟/閉合標籤（自閉合時兩者相同）；
    // 未落在可配對標籤上回傳 false。供 onCursorPositionChanged 與單元測試共用。
    static bool matchingTagRanges(const QString &text, int caretChar,
                                  int *openStart, int *openEnd,
                                  int *closeStart, int *closeEnd);

    // === Ctrl（macOS ⌘）+雙擊選整個字（Delimiter 頁 ctrlDoubleClickWholeWord）===
    // 開啟後，按住 Ctrl/⌘ 雙擊時以「預設字元集」（忽略 delimiter 覆寫）選取整個字；
    // 未按修飾鍵或關閉時維持預設雙擊（依 delimiterChars 斷字）行為。
    void setCtrlDoubleClickWholeWord(bool enabled) { m_ctrlDoubleClickWholeWord = enabled; }
    bool ctrlDoubleClickWholeWord() const { return m_ctrlDoubleClickWholeWord; }

    // 摺疊邊界樣式（對應 persistence::FoldMarginStyle 序位：0=None 1=Simple 2=Arrow 3=Circle 4=Box）
    void setFoldMarginStyle(int style);

    // === 詞彙上色（5 色 Style Token）===
    // 以 5 種不同指示器（kTokenIndicatorBase..+4）標記目前字詞（或選取）的所有出現處。
    // 與 markAll 不同，這些標記持續存在直到 clearStyledTokens 清除。
    void styleTokenOccurrences(int colorIndex /* 0..4 */);
    void clearStyledTokens();

    // 智慧高亮專用指示器；詞彙上色指示器基底（連續 5 個：base..base+4）。
    static constexpr int kSmartIndicator = 2;
    static constexpr int kTokenIndicatorBase = 3;
    // 標籤配對高亮指示器（避開 kTokenIndicatorBase..+4 = 3..7）
    static constexpr int kTagMatchIndicator = 8;

    // 查詢某指示器目前標記了幾個獨立範圍（供測試/UI 統計）。
    int indicatorRangeCount(int indicator) const;

    // === HTML/XML 自動閉合標籤 ===
    // 由游標前（同一行）的文字判斷是否應補上閉合標籤；
    // 回傳 "</tag>"（可補），或空字串（自閉合、閉合標籤、非標籤）。純函式，供測試共用。
    static QString closingTagFor(const QString &textBeforeCaret);

    // === 貼上為純文字（Edit ▸ Paste Special）===
    // 讀取剪貼簿的 text/html 負載，盡力去除標籤後以純文字插入（取代目前選取）；
    // 無 HTML 負載時退回一般純文字貼上。
    void pasteAsHtml();
    // 讀取剪貼簿的 text/rtf（或 application/rtf）負載，盡力去除 RTF 控制詞後以純文字插入；
    // 無 RTF 負載時退回一般純文字貼上。
    void pasteAsRtf();

    // === 欄位選取轉多重編輯（Preferences ▸ Enable Column Selection to Multi-Editing）===
    // 開啟後，endColumnSelect() 不再產生矩形選取，而是在錨點與目前游標涵蓋的每一行、
    // 相同欄位處各放一個插入點（多重編輯游標），供直接輸入同步套用到多行。
    void setColumnSelectionToMultiEdit(bool enabled) { m_columnSelectionToMultiEdit = enabled; }
    bool columnSelectionToMultiEdit() const { return m_columnSelectionToMultiEdit; }

    // === 插入日期／時間（Edit ▸ Insert Date/Time）===
    // 取代目前選取（或於游標處插入）系統當地時區的日期／時間文字。
    void insertDateShort();                       // 依 QLocale 短格式（如 3/14/26）
    void insertDateLong();                        // 依 QLocale 長格式（如 Saturday, March 14, 2026 9:41:00 AM）
    void insertDateCustom(const QString &format);  // 依 QDateTime::toString(format) 自訂格式字串

    // === Undo / Redo 強化（複刻 Notepad++ v8.8.9、v8.8.1）===
    // 取代直接呼叫 QsciScintilla::undo()/redo()：
    //  1) 一律在操作後還原原本的垂直捲動位置（v8.8.9），避免視野被拉走；
    //  2) 啟用選取歷史時（v8.8.1），若上次文件修改之後只發生過「選取變更」，
    //     則先逐步還原選取，選取歷史用盡才真正回退文件內容。
    void undoWithHistory();
    void redoWithHistory();
    // 選取歷史開關（對應 Preferences 的 undoSelectionHistory）
    void setUndoSelectionHistory(bool on) { m_undoSelectionHistory = on; }
    bool undoSelectionHistory() const { return m_undoSelectionHistory; }
    // 供測試/診斷：目前尚可回退的選取歷史筆數
    int selectionHistoryDepth() const { return m_selHistoryPos; }

    // 進階自動縮排（複刻 Notepad++ v8.7 可停用 C-like 自動縮排、v8.7.5 擴充語言）：
    // 除了沿用上一行縮排，還會依語言在 `{`／`:` 之後追加一級縮排。
    void setAdvancedAutoIndent(bool on) { m_advancedAutoIndent = on; }
    bool advancedAutoIndent() const { return m_advancedAutoIndent; }

    // 縮放並發出 zoomChanged（供跨檢視同步縮放；直接呼叫 QsciScintilla::zoomIn 不會通知）
    void applyZoomIn();
    void applyZoomOut();
    void applyZoomTo(int level);

    // 停用「拖放選取文字」（複刻 Notepad++ v8.9.3）：關閉時在選取範圍內按下滑鼠
    // 會直接改為重新選取，而不是把選取內容拖走。
    void setSelectionDragDropEnabled(bool on) { m_selectionDragDrop = on; }
    bool selectionDragDropEnabled() const { return m_selectionDragDrop; }

signals:
    void dirtyChanged(bool dirty);
    // 縮放層級改變（選單縮放或 Ctrl+滾輪）；供跨檢視同步縮放（Notepad++ v8.9.5）
    void zoomChanged(int zoom);
    void metaChanged();     // 編碼/EOL 變更（狀態列更新）
    void lexerChanged();    // lexer 重建（供 MainWindow 重新套主題/降飽和）
    void callTipRequested(const QString &functionName);  // 鍵入 '(' 時發出，供上層查簽名
    // 右鍵選單請求（複刻 Notepad++ 編輯區右鍵選單）：停用 Scintilla 內建 popup 後，
    // 由 EditorWidget 攔截 contextMenuEvent 並轉發全域座標，交由 MainWindow 建構完整選單。
    void contextMenuRequested(const QPoint &globalPos);
    // 自檔案總管/Finder 拖入檔案（複刻 Notepad++ 拖放開檔）：Scintilla 自身的文字拖放
    // 仍照常運作，只有「本機檔案 URL」會被攔下來轉發，交由 MainWindow 開成分頁。
    void filesDropped(const QStringList &paths);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // Ctrl+滾輪縮放：QsciScintilla 內部處理，於此攔截以發出 zoomChanged（跨檢視同步縮放用）
    void wheelEvent(QWheelEvent *event) override;
    // 停用拖放選取文字時，於選取範圍內按下滑鼠先清掉選取（見 setSelectionDragDropEnabled）
    void mousePressEvent(QMouseEvent *event) override;
    // 攔截 viewport 的雙擊事件以支援 Ctrl/⌘+雙擊選整個字（見 m_ctrlDoubleClickWholeWord）。
    bool eventFilter(QObject *watched, QEvent *event) override;
    // 右鍵：停用 Scintilla 內建 popup（見建構子 SCI_USEPOPUP），改發 contextMenuRequested。
    void contextMenuEvent(QContextMenuEvent *event) override;
    // 註：拖放不在此以 dropEvent() override 處理——QsciScintilla 繼承自 QAbstractScrollArea，
    // 拖放事件實際落在 viewport() 上，widget 層的 override 不會被呼叫（與 Sprint 8 右鍵選單
    // 同一個坑）。改於 eventFilter() 中攔截 viewport 的 DragEnter/DragMove/Drop。

private slots:
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers state);
    void onUserListActivated(int id, const QString &string);
    void onCursorPositionChanged();  // 智慧高亮：游標移動時重標

private:
    void applyDefaultConfig();
    // 智慧高亮/詞彙上色共用：清除指定指示器全文範圍。
    void clearIndicatorRange(int indicator);
    // 以整詞、區分大小寫搜尋 word，並以指定指示器填色；回傳出現次數。
    int fillWordOccurrences(const QString &word, int indicator);
    // 取游標所在字詞（無字詞回空字串）。
    QString wordUnderCaret() const;
    void applyLexerForPath(const QString &path);
    // 依 m_highlightMatchingTags 標示游標所在標籤與其配對標籤（清除舊標記後重標）。
    void updateTagMatchHighlight();
    void applyEolMode(Eol eol);
    void toggleBookmarkAtLine(int line);
    // 依 m_columnSelectionToMultiEdit 開關，將 [anchorPos, caretPos] 矩形涵蓋的每一行、
    // 相同欄位處各放一個插入點（供 endColumnSelect 呼叫）。
    void applyMultiEditCaretsForRectangle(long anchorPos, long caretPos);

    // 標記/清除「純中繼資料（編碼/EOL/codec）」造成的 dirty。
    // QScintilla 的 setModified(true) 在 save-point 為 no-op，無法反映這類變更，
    // 故以 m_metaDirty 補足，並確保 dirtyChanged 不重複發送（FR-019/020）。
    void markMetaDirty();
    void clearDirty();

    QString m_filePath;
    Encoding m_encoding = Encoding::Utf8;
    QString m_codecName;   // 非空 = 用具名 codec 存檔（Character sets 選的傳統編碼）
    Eol m_eol = Eol::Lf;
    bool m_metaDirty = false;  // 編碼/EOL/codec 變更造成的 dirty（文字內容未變）
    bool m_policyReadOnly = false;  // 因 -fullReadOnly / 監控模式而鎖定（非檔案屬性所致）
    bool m_autoClose = true;  // 自動配對符號 ( [ { " '（FR-050）

    // 兩段式選取錨點（Begin/End Select）
    long m_selectAnchorPos = 0;
    bool m_hasSelectAnchor = false;
    bool m_smartHighlight = false;  // 智慧高亮開關
    bool m_highlightMatchingTags = false;  // 標示相符 HTML/XML 標籤開關
    bool m_ctrlDoubleClickWholeWord = true;  // Ctrl/⌘+雙擊選整個字開關
    bool m_changeHistoryEnabled = false;  // 變更歷史開關狀態（FR-057）
    bool m_virtualSpace = false;          // 虛擬空間開關狀態（FR-060）

    // Preferences 即時套用相關狀態
    int  m_untitledNumber = 0;       // 未命名分頁序號（0=未指派/已命名，>0 顯示 untitled(N)）
    bool m_showLineNumbers = true;   // 行號邊欄顯示狀態
    int  m_caretWidth = 1;           // 插入點寬度（像素）
    bool m_wordCompletion = true;    // 字詞自動完成開關
    bool m_callTips = true;          // call tip 開關
    // 多載切換狀態（Notepad++ 的 ▲ n of m ▼）：目前顯示中的多載清單與索引
    QStringList m_callTipOverloads;
    int m_callTipIndex = -1;
    // 依 m_callTipIndex 重新繪製 call tip（含箭頭列）
    void renderCallTip();
    bool m_columnSelectionToMultiEdit = false;  // 欄位選取轉多重編輯偏好開關

    // === Undo/Redo 選取歷史（Notepad++ v8.8.1）===
    // 一筆選取快照：anchor/caret 各自的行與欄
    struct SelSnapshot { int aLine = 0, aIdx = 0, cLine = 0, cIdx = 0; };
    bool m_undoSelectionHistory = false;
    QVector<SelSnapshot> m_selHistory;   // 自上次文件修改以來的選取變化（由舊到新）
    int m_selHistoryPos = 0;             // 尚可回退的筆數（= m_selHistory 已生效的長度）
    bool m_restoringSelection = false;   // 還原選取期間不再記錄，避免自我遞迴
    void recordSelectionSnapshot();
    bool restorePreviousSelection();     // 有可回退的選取則還原並回傳 true

    bool m_selectionDragDrop = true;     // 允許拖放選取文字（Notepad++ v8.9.3 可停用）
    bool m_advancedAutoIndent = true;    // 進階自動縮排（Notepad++ v8.7 起可停用）
    // 按下 Enter 後依語言追加一級縮排；prevLine 為 Enter 之前游標左側的那段文字
    void applyAdvancedAutoIndent(const QString &prevLine);

    // API 自動完成資料（FR-055）——由 EditorWidget 持有（parent 改為 this，與 lexer 生命週期解耦），
    // 銷毀前主動收斂背景 prepare() 的 worker thread，避免 async 競態造成的懸空回呼（SIGBUS）。
    QsciAPIs *m_apis = nullptr;

    // 路徑自動完成（手動觸發）：user list id 與觸發當下已輸入前綴長度（字元數＝位元組數，
    // 路徑片段限定 ASCII 字元，見 pathFragmentBefore），供 onUserListActivated 決定要刪除多少字元。
    static constexpr int kPathCompletionListId = 1;
    int m_pathCompletionPrefixLen = 0;
};

}  // namespace macpad::core
