#pragma once

// PreferencesDialog — 偏好設定 UI（暴露 SettingsStore；FR-009/015/021/034/053）
// 分類頁籤：General / Editing / New Document / Backup / Auto-Completion / Performance / Search
//          / Highlighting / Dark Mode（Appearance）

#include <QDialog>

#include "persistence/SettingsStore.h"

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QTabWidget;
class QWidget;

namespace macpad::ui {

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const macpad::persistence::Settings &current,
                               QWidget *parent = nullptr);

    macpad::persistence::Settings result() const;

private:
    QWidget *buildGeneralPage();
    QWidget *buildEditingPage();
    QWidget *buildNewDocumentPage();
    QWidget *buildBackupPage();
    QWidget *buildAutoCompletionPage();
    QWidget *buildPerformancePage();
    QWidget *buildSearchPage();
    QWidget *buildHighlightingPage();
    QWidget *buildDarkModePage();

    macpad::persistence::Settings m_original;  // 保留傳入設定，result() 以此為底避免遺失未暴露欄位

    QTabWidget *m_tabs = nullptr;

    // General
    QComboBox *m_theme = nullptr;
    QSpinBox *m_tabWidth = nullptr;
    QCheckBox *m_restore = nullptr;
    QCheckBox *m_autosave = nullptr;
    QSpinBox *m_autosaveInterval = nullptr;
    QCheckBox *m_singleInstance = nullptr;

    // Editing
    QCheckBox *m_showLineNumbers = nullptr;
    QCheckBox *m_showIndentGuides = nullptr;
    QCheckBox *m_wordWrap = nullptr;
    QCheckBox *m_showWhitespace = nullptr;
    QSpinBox *m_caretWidth = nullptr;
    QCheckBox *m_currentLineHighlight = nullptr;
    QCheckBox *m_enableVirtualSpace = nullptr;
    QCheckBox *m_copyLineWithoutSelection = nullptr;

    // New Document
    QComboBox *m_defaultEol = nullptr;
    QComboBox *m_defaultEncoding = nullptr;
    QCheckBox *m_autoDetectFileStatus = nullptr;
    QLineEdit *m_sessionFileExt = nullptr;

    // Backup
    QComboBox *m_backupMode = nullptr;
    QLineEdit *m_backupDir = nullptr;
    QCheckBox *m_autosaveOnFocusLoss = nullptr;
    QCheckBox *m_enableSessionSnapshot = nullptr;
    QSpinBox *m_snapshotIntervalSec = nullptr;

    // Auto-Completion
    QCheckBox *m_autoInsertPairs = nullptr;
    QCheckBox *m_wordAutoComplete = nullptr;
    QSpinBox *m_acThreshold = nullptr;
    QCheckBox *m_showCallTips = nullptr;

    // Performance
    QSpinBox *m_largeFileMB = nullptr;
    QSpinBox *m_disableAutoCompleteOverMB = nullptr;

    // Search
    QLineEdit *m_searchEngineUrl = nullptr;
    QCheckBox *m_keepFindDialogOpen = nullptr;
    QCheckBox *m_confirmReplaceAll = nullptr;
    QSpinBox *m_findInSelectionThreshold = nullptr;

    // Highlighting
    QCheckBox *m_smartHighlight = nullptr;
    QCheckBox *m_highlightMatchingTags = nullptr;
    QSpinBox *m_edgeColumn = nullptr;
    QCheckBox *m_showWrapSymbol = nullptr;
    QCheckBox *m_showEol = nullptr;
    QCheckBox *m_multiEdgeEnabled = nullptr;

    // Dark Mode / Appearance
    QComboBox *m_darkModeTheme = nullptr;
    QCheckBox *m_showToolbar = nullptr;
    QCheckBox *m_showStatusBar = nullptr;
    QCheckBox *m_showTabBar = nullptr;
    QSpinBox *m_caretBlinkRate = nullptr;
};

}  // namespace macpad::ui
