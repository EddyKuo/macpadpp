#pragma once

// FindReplaceDialog — 尋找/取代對話框（features/search）
// FR-010 一般尋找取代、FR-011 regex（含群組回填 \1）。
// 模式：非模態；作用於 MainWindow 目前的 EditorWidget。

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;

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

private slots:
    void findNext();
    void replaceOne();
    void replaceAll();
    void markAll();
    void incrementalFind(const QString &text);

private:
    bool doFind(bool forward, bool fromStart);
    void report(const QString &msg);
    // 記錄最近一次成功尋找所選取的匹配範圍，供 replaceOne 驗證（避免誤取代手動選取）
    void rememberMatch();
    bool selectionIsRememberedMatch() const;

    macpad::core::EditorWidget *m_editor = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_wrap = nullptr;
    QLabel *m_status = nullptr;

    // 最近一次成功尋找的選取範圍（-1 表示尚無）
    int m_matchLineFrom = -1;
    int m_matchIndexFrom = -1;
    int m_matchLineTo = -1;
    int m_matchIndexTo = -1;
};

}  // namespace macpad::features
