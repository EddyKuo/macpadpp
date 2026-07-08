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
    void cancelCallTip();

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
    void selectAllOccurrences();     // 選取目前選取（或游標所在字詞）的所有相符項目
    void skipAndSelectNext();        // 略過目前最後加入的選取，改選下一個相符項目

    // === API 自動完成（FR-055 hook）===
    // entries 由上層 ApiDatabase 模組提供；無 lexer 時安全跳過（無 lexer 即無語言可套 API）。
    void applyApiCompletions(const QStringList &entries);

signals:
    void dirtyChanged(bool dirty);
    void metaChanged();     // 編碼/EOL 變更（狀態列更新）
    void lexerChanged();    // lexer 重建（供 MainWindow 重新套主題/降飽和）
    void callTipRequested(const QString &functionName);  // 鍵入 '(' 時發出，供上層查簽名

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers state);

private:
    void applyDefaultConfig();
    void applyLexerForPath(const QString &path);
    void applyEolMode(Eol eol);
    void toggleBookmarkAtLine(int line);

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
    bool m_autoClose = true;  // 自動配對符號 ( [ { " '（FR-050）
    bool m_changeHistoryEnabled = false;  // 變更歷史開關狀態（FR-057）
    bool m_virtualSpace = false;          // 虛擬空間開關狀態（FR-060）

    // API 自動完成資料（FR-055）——由 EditorWidget 持有（parent 改為 this，與 lexer 生命週期解耦），
    // 銷毀前主動收斂背景 prepare() 的 worker thread，避免 async 競態造成的懸空回呼（SIGBUS）。
    QsciAPIs *m_apis = nullptr;
};

}  // namespace macpad::core
