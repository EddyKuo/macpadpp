#pragma once

// PreferencesDialog — 偏好設定 UI（暴露 SettingsStore；FR-009/015/021/034/053）
// 分類頁籤：General / Editing / New Document / Backup / Auto-Completion / Performance / Search
//          / Highlighting / Dark Mode（Appearance）/ Toolbar / Tab Bar / Margins-Border-Edge
//          / Default Directory / Recent Files History / Language / Multi-Instance & Date
//          / Delimiter / MISC

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
    QWidget *buildToolbarPage();
    QWidget *buildTabBarPage();
    QWidget *buildMarginsPage();
    QWidget *buildDefaultDirectoryPage();
    QWidget *buildRecentFilesPage();
    QWidget *buildLanguagePage();
    QWidget *buildMultiInstanceDatePage();
    QWidget *buildDelimiterPage();
    QWidget *buildMiscPage();

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
    QCheckBox *m_columnSelectionToMultiEdit = nullptr;

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

    // Toolbar
    QComboBox *m_toolbarIconSize = nullptr;

    // Tab Bar
    QCheckBox *m_tabBarMultiLine = nullptr;
    QCheckBox *m_tabBarVertical = nullptr;
    QCheckBox *m_tabBarShowCloseButton = nullptr;
    QCheckBox *m_tabBarDoubleClickCloses = nullptr;

    // Margins / Border / Edge
    QComboBox *m_edgeMode = nullptr;
    QComboBox *m_foldMarginStyle = nullptr;
    QCheckBox *m_lineNumberMargin = nullptr;

    // Default Directory
    QComboBox *m_defaultDirPolicy = nullptr;
    QLineEdit *m_defaultDirFixedPath = nullptr;

    // Recent Files History
    QSpinBox *m_recentFilesMaxEntries = nullptr;
    QCheckBox *m_recentFilesShowFullPath = nullptr;
    QCheckBox *m_recentFilesInSubmenu = nullptr;

    // Language / per-language indentation
    QLineEdit *m_disabledLanguages = nullptr;       // 逗號分隔的語言名稱
    QLineEdit *m_perLangTabWidth = nullptr;          // "lang=width,lang=width" 格式

    // Multi-Instance & Date
    QComboBox *m_multiInstanceMode = nullptr;
    QLineEdit *m_dateFormat = nullptr;
    QLineEdit *m_customDateFormat = nullptr;

    // Delimiter
    QLineEdit *m_delimiterChars = nullptr;
    QCheckBox *m_ctrlDoubleClickWholeWord = nullptr;

    // MISC
    QCheckBox *m_docSwitcherEnabled = nullptr;
    QCheckBox *m_docPeekerEnabled = nullptr;
    QComboBox *m_fileStatusAutoDetect = nullptr;
    QCheckBox *m_autoUpdater = nullptr;
    QCheckBox *m_enableSound = nullptr;
};

}  // namespace macpad::ui
