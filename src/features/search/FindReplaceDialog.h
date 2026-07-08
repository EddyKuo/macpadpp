#pragma once

// FindReplaceDialog — 尋找/取代對話框（features/search）
// FR-010 一般尋找取代、FR-011 regex（含群組回填 \1）。
// 模式：非模態；作用於 MainWindow 目前的 EditorWidget。

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;
class QSlider;
class QPushButton;

namespace macpad::core { class EditorWidget; }

namespace macpad::features {

class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(QWidget *parent = nullptr);

    // 綁定目前作用中的編輯器（MainWindow 於分頁切換/開啟對話框時更新）
    void setEditor(macpad::core::EditorWidget *editor);

    // 開啟並聚焦；replaceMode 決定是否顯示取代列
    void showFind(bool replaceMode);

public slots:
    // 「Find (Volatile) Next/Previous」：搜尋但不記錄為最近命中，不覆寫 replaceOne 依賴的匹配記錄。
    // 公開供 MainWindow 綁定全域快速鍵（如 Ctrl+Alt+F3 / Ctrl+Alt+Shift+F3）。
    void findNextVolatile();
    void findPreviousVolatile();

private slots:
    void findNext();
    void replaceOne();
    void replaceAll();
    void markAll();
    void incrementalFind(const QString &text);
    void countAll();       // FR-047「Count」按鈕：只計數不移動
    void swapFindReplace(); // 交換 Find/Replace 欄位文字
    void opacityChanged(int value); // 視窗透明度滑桿變更
    void findCodepointRange(); // 依 [lo,hi] 碼點範圍尋找下一個相符字元

private:
    // 從 SettingsStore 載入 keepFindDialogOpen/confirmReplaceAll/findInSelectionThreshold 等偏好，
    // 於每次 showFind() 呼叫（設定可能在對話框開啟期間被 Preferences 修改）。
    void loadPreferences();
    // keepFindDialogOpen=false 時，尋找/取代成功後自動關閉對話框。
    void closeIfNotKeptOpen();
    // remember=false 時略過 rememberMatch（供 volatile find 使用，不覆寫最近命中記錄）
    bool doFind(bool forward, bool fromStart, bool remember = true);
    void report(const QString &msg);
    // 記錄最近一次成功尋找所選取的匹配範圍，供 replaceOne 驗證（避免誤取代手動選取）
    void rememberMatch();
    bool selectionIsRememberedMatch() const;

    // 依「Extended」勾選狀態（且非 Regex 模式）回傳跳脫轉換後的尋找/取代文字
    QString effectiveFindText() const;
    QString effectiveReplaceText() const;

    // 將 \n \r \t \0 \\ \xNN \b \uXXXX \u{XXXX} \oNNN(八進位) \dNNN(十進位) 跳脫序列轉換為對應字元；
    // 不認得的序列原樣保留。自足實作，不依賴其他模組。
    static QString unescapeExtended(const QString &text);

    // 從 QSettings 載入/儲存搜尋選項勾選狀態（沿用 app 預設 organization/application scope）
    void loadSearchOptions();
    void saveSearchOption(const QString &key, bool value) const;

    macpad::core::EditorWidget *m_editor = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_wrap = nullptr;
    QCheckBox *m_inSelection = nullptr;  // 「In selection」：限制 find/replaceAll 於選取範圍內
    QCheckBox *m_dotMatchesNewline = nullptr;  // 「. matches newline」：regex dot-all（僅 replaceAll 生效）
    QCheckBox *m_extended = nullptr;  // 「Extended (\n \r \t \0 \xNN)」：尋找/取代文字跳脫轉換
    QSlider *m_opacitySlider = nullptr;  // 視窗透明度（30%~100%）
    QLabel *m_status = nullptr;

    // 碼點範圍尋找（modest 功能）：輸入 16 進位/十進位碼點下限/上限，尋找下一個落在範圍內的字元
    QLineEdit *m_cpLoEdit = nullptr;
    QLineEdit *m_cpHiEdit = nullptr;
    QPushButton *m_cpFindBtn = nullptr;

    // 從 SettingsStore 載入的偏好（FR-053）
    bool m_keepFindDialogOpen = true;
    bool m_confirmReplaceAll = true;
    int m_findInSelectionThreshold = 0;

    // 最近一次成功尋找的選取範圍（-1 表示尚無）
    int m_matchLineFrom = -1;
    int m_matchIndexFrom = -1;
    int m_matchLineTo = -1;
    int m_matchIndexTo = -1;

    // 「In selection」勾選當下記錄的限制範圍（-1 表示尚未記錄）；用於限制 find/replaceAll
    int m_selRestrictLineFrom = -1;
    int m_selRestrictIndexFrom = -1;
    int m_selRestrictLineTo = -1;
    int m_selRestrictIndexTo = -1;
};

}  // namespace macpad::features
