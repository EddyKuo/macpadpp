#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

using macpad::core::Encoding;
using macpad::core::Eol;
using macpad::persistence::BackupMode;
using macpad::persistence::Settings;
using macpad::persistence::ThemeMode;

namespace macpad::ui {

PreferencesDialog::PreferencesDialog(const Settings &current, QWidget *parent)
    : QDialog(parent), m_original(current)
{
    setWindowTitle(tr("Preferences"));

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildGeneralPage(), tr("General"));
    m_tabs->addTab(buildEditingPage(), tr("Editing"));
    m_tabs->addTab(buildNewDocumentPage(), tr("New Document"));
    m_tabs->addTab(buildBackupPage(), tr("Backup"));
    m_tabs->addTab(buildAutoCompletionPage(), tr("Auto-Completion"));
    m_tabs->addTab(buildPerformancePage(), tr("Performance"));
    m_tabs->addTab(buildSearchPage(), tr("Search"));
    m_tabs->addTab(buildHighlightingPage(), tr("Highlighting"));
    m_tabs->addTab(buildDarkModePage(), tr("Dark Mode"));

    // General 頁與 Dark Mode 頁的主題選單同步（同一份 ThemeMode 設定，兩處均可編輯）
    connect(m_theme, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
                if (m_darkModeTheme->currentIndex() != idx)
                    m_darkModeTheme->setCurrentIndex(idx);
            });
    connect(m_darkModeTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
                if (m_theme->currentIndex() != idx)
                    m_theme->setCurrentIndex(idx);
            });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);
}

QWidget *PreferencesDialog::buildGeneralPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_theme = new QComboBox(page);
    m_theme->addItems({tr("Follow System"), tr("Light"), tr("Dark")});
    m_theme->setCurrentIndex(int(current.theme));

    m_tabWidth = new QSpinBox(page);
    m_tabWidth->setRange(1, 16);
    m_tabWidth->setValue(current.tabWidth);

    m_restore = new QCheckBox(tr("啟動時還原上次 session"), page);
    m_restore->setChecked(current.restoreOnLaunch);

    m_autosave = new QCheckBox(tr("啟用自動儲存"), page);
    m_autosave->setChecked(current.autosaveEnabled);

    m_autosaveInterval = new QSpinBox(page);
    m_autosaveInterval->setRange(5, 3600);
    m_autosaveInterval->setSuffix(tr(" 秒"));
    m_autosaveInterval->setValue(current.autosaveIntervalSec);

    m_singleInstance = new QCheckBox(tr("單一執行個體模式"), page);
    m_singleInstance->setChecked(current.singleInstance);

    auto *form = new QFormLayout(page);
    form->addRow(tr("主題"), m_theme);
    form->addRow(tr("Tab 寬度"), m_tabWidth);
    form->addRow(m_restore);
    form->addRow(m_autosave);
    form->addRow(tr("自動儲存間隔"), m_autosaveInterval);
    form->addRow(m_singleInstance);
    return page;
}

QWidget *PreferencesDialog::buildEditingPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_showLineNumbers = new QCheckBox(tr("顯示行號"), page);
    m_showLineNumbers->setChecked(current.showLineNumbers);

    m_showIndentGuides = new QCheckBox(tr("顯示縮排參考線"), page);
    m_showIndentGuides->setChecked(current.showIndentGuides);

    m_wordWrap = new QCheckBox(tr("自動換行"), page);
    m_wordWrap->setChecked(current.wordWrap);

    m_showWhitespace = new QCheckBox(tr("顯示空白字元"), page);
    m_showWhitespace->setChecked(current.showWhitespace);

    m_caretWidth = new QSpinBox(page);
    m_caretWidth->setRange(1, 4);
    m_caretWidth->setValue(current.caretWidth);

    m_currentLineHighlight = new QCheckBox(tr("高亮目前所在行"), page);
    m_currentLineHighlight->setChecked(current.currentLineHighlight);

    m_enableVirtualSpace = new QCheckBox(tr("允許插入點移至行尾之後（虛擬空白）"), page);
    m_enableVirtualSpace->setChecked(current.enableVirtualSpace);

    m_copyLineWithoutSelection = new QCheckBox(tr("無選取時複製/剪下整行"), page);
    m_copyLineWithoutSelection->setChecked(current.copyLineWithoutSelection);

    auto *form = new QFormLayout(page);
    form->addRow(m_showLineNumbers);
    form->addRow(m_showIndentGuides);
    form->addRow(m_wordWrap);
    form->addRow(m_showWhitespace);
    form->addRow(tr("插入點寬度"), m_caretWidth);
    form->addRow(m_currentLineHighlight);
    form->addRow(m_enableVirtualSpace);
    form->addRow(m_copyLineWithoutSelection);
    return page;
}

QWidget *PreferencesDialog::buildNewDocumentPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_defaultEol = new QComboBox(page);
    m_defaultEol->addItems({tr("Unix (LF)"), tr("Windows (CR LF)"), tr("Old Mac (CR)")});
    m_defaultEol->setCurrentIndex(int(current.defaultEol));

    m_defaultEncoding = new QComboBox(page);
    m_defaultEncoding->addItems(
        {tr("UTF-8"), tr("UTF-8 BOM"), tr("UTF-16 LE"), tr("UTF-16 BE"), tr("ANSI (Latin-1)")});
    m_defaultEncoding->setCurrentIndex(int(current.defaultEncoding));

    m_autoDetectFileStatus = new QCheckBox(tr("偵測檔案於外部被異動/刪除"), page);
    m_autoDetectFileStatus->setChecked(current.autoDetectFileStatus);

    m_sessionFileExt = new QLineEdit(page);
    m_sessionFileExt->setText(current.sessionFileExt);
    m_sessionFileExt->setPlaceholderText(QStringLiteral(".session"));

    auto *form = new QFormLayout(page);
    form->addRow(tr("預設換行字元"), m_defaultEol);
    form->addRow(tr("預設編碼"), m_defaultEncoding);
    form->addRow(m_autoDetectFileStatus);
    form->addRow(tr("Session 檔案副檔名"), m_sessionFileExt);
    return page;
}

QWidget *PreferencesDialog::buildBackupPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_backupMode = new QComboBox(page);
    m_backupMode->addItems({tr("不備份"), tr("簡易備份"), tr("詳細備份")});
    m_backupMode->setCurrentIndex(int(current.backupMode));

    m_backupDir = new QLineEdit(page);
    m_backupDir->setText(current.backupDir);

    m_autosaveOnFocusLoss = new QCheckBox(tr("失去焦點時自動儲存"), page);
    m_autosaveOnFocusLoss->setChecked(current.autosaveOnFocusLoss);

    m_enableSessionSnapshot = new QCheckBox(tr("啟用當機還原快照"), page);
    m_enableSessionSnapshot->setChecked(current.enableSessionSnapshot);

    m_snapshotIntervalSec = new QSpinBox(page);
    m_snapshotIntervalSec->setRange(5, 3600);
    m_snapshotIntervalSec->setSuffix(tr(" 秒"));
    m_snapshotIntervalSec->setValue(current.snapshotIntervalSec);

    auto *form = new QFormLayout(page);
    form->addRow(tr("備份模式"), m_backupMode);
    form->addRow(tr("備份目錄"), m_backupDir);
    form->addRow(m_autosaveOnFocusLoss);
    form->addRow(m_enableSessionSnapshot);
    form->addRow(tr("快照間隔"), m_snapshotIntervalSec);
    return page;
}

QWidget *PreferencesDialog::buildAutoCompletionPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_autoInsertPairs = new QCheckBox(tr("自動插入配對符號（括號/引號）"), page);
    m_autoInsertPairs->setChecked(current.autoInsertPairs);

    m_wordAutoComplete = new QCheckBox(tr("文字自動完成"), page);
    m_wordAutoComplete->setChecked(current.wordAutoComplete);

    m_acThreshold = new QSpinBox(page);
    m_acThreshold->setRange(1, 10);
    m_acThreshold->setValue(current.acThreshold);

    m_showCallTips = new QCheckBox(tr("顯示函式提示"), page);
    m_showCallTips->setChecked(current.showCallTips);

    auto *form = new QFormLayout(page);
    form->addRow(m_autoInsertPairs);
    form->addRow(m_wordAutoComplete);
    form->addRow(tr("觸發字元數"), m_acThreshold);
    form->addRow(m_showCallTips);
    return page;
}

QWidget *PreferencesDialog::buildPerformancePage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_largeFileMB = new QSpinBox(page);
    m_largeFileMB->setRange(1, 10000);
    m_largeFileMB->setSuffix(tr(" MB"));
    m_largeFileMB->setValue(current.largeFileMB);

    m_disableAutoCompleteOverMB = new QSpinBox(page);
    m_disableAutoCompleteOverMB->setRange(1, 10000);
    m_disableAutoCompleteOverMB->setSuffix(tr(" MB"));
    m_disableAutoCompleteOverMB->setValue(current.disableAutoCompleteOverMB);

    auto *form = new QFormLayout(page);
    form->addRow(tr("大型檔案門檻"), m_largeFileMB);
    form->addRow(tr("超過此大小停用自動完成"), m_disableAutoCompleteOverMB);
    return page;
}

QWidget *PreferencesDialog::buildSearchPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_searchEngineUrl = new QLineEdit(page);
    m_searchEngineUrl->setText(current.searchEngineUrl);
    m_searchEngineUrl->setPlaceholderText(
        QStringLiteral("https://www.google.com/search?q=%s"));

    m_keepFindDialogOpen = new QCheckBox(tr("搜尋後保持「尋找」對話框開啟"), page);
    m_keepFindDialogOpen->setChecked(current.keepFindDialogOpen);

    m_confirmReplaceAll = new QCheckBox(tr("「全部取代」前顯示確認"), page);
    m_confirmReplaceAll->setChecked(current.confirmReplaceAll);

    m_findInSelectionThreshold = new QSpinBox(page);
    m_findInSelectionThreshold->setRange(0, 1000000);
    m_findInSelectionThreshold->setSpecialValueText(tr("關閉"));
    m_findInSelectionThreshold->setSuffix(tr(" 字元"));
    m_findInSelectionThreshold->setValue(current.findInSelectionThreshold);

    auto *form = new QFormLayout(page);
    form->addRow(tr("網路搜尋引擎網址"), m_searchEngineUrl);
    form->addRow(m_keepFindDialogOpen);
    form->addRow(m_confirmReplaceAll);
    form->addRow(tr("自動啟用「在選取範圍內尋找」門檻"), m_findInSelectionThreshold);
    return page;
}

QWidget *PreferencesDialog::buildHighlightingPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_smartHighlight = new QCheckBox(tr("智慧高亮（自動標示與選取字詞相同的字串）"), page);
    m_smartHighlight->setChecked(current.smartHighlight);

    m_highlightMatchingTags = new QCheckBox(tr("標示相符的 HTML/XML 標籤"), page);
    m_highlightMatchingTags->setChecked(current.highlightMatchingTags);

    m_edgeColumn = new QSpinBox(page);
    m_edgeColumn->setRange(0, 500);
    m_edgeColumn->setSpecialValueText(tr("關閉"));
    m_edgeColumn->setValue(current.edgeColumn);

    m_multiEdgeEnabled = new QCheckBox(tr("啟用多重邊界線"), page);
    m_multiEdgeEnabled->setChecked(current.multiEdgeEnabled);

    m_showWrapSymbol = new QCheckBox(tr("換行處顯示折行符號"), page);
    m_showWrapSymbol->setChecked(current.showWrapSymbol);

    m_showEol = new QCheckBox(tr("顯示換行字元"), page);
    m_showEol->setChecked(current.showEol);

    auto *form = new QFormLayout(page);
    form->addRow(m_smartHighlight);
    form->addRow(m_highlightMatchingTags);
    form->addRow(tr("垂直邊界線欄位"), m_edgeColumn);
    form->addRow(m_multiEdgeEnabled);
    form->addRow(m_showWrapSymbol);
    form->addRow(m_showEol);
    return page;
}

QWidget *PreferencesDialog::buildDarkModePage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_darkModeTheme = new QComboBox(page);
    m_darkModeTheme->addItems({tr("Follow System"), tr("Light"), tr("Dark")});
    m_darkModeTheme->setCurrentIndex(int(current.theme));

    m_showToolbar = new QCheckBox(tr("顯示工具列"), page);
    m_showToolbar->setChecked(current.showToolbar);

    m_showStatusBar = new QCheckBox(tr("顯示狀態列"), page);
    m_showStatusBar->setChecked(current.showStatusBar);

    m_showTabBar = new QCheckBox(tr("顯示分頁列"), page);
    m_showTabBar->setChecked(current.showTabBar);

    m_caretBlinkRate = new QSpinBox(page);
    m_caretBlinkRate->setRange(0, 5000);
    m_caretBlinkRate->setSuffix(tr(" ms"));
    m_caretBlinkRate->setSpecialValueText(tr("不閃爍"));
    m_caretBlinkRate->setValue(current.caretBlinkRate);

    auto *form = new QFormLayout(page);
    form->addRow(tr("主題"), m_darkModeTheme);
    form->addRow(m_showToolbar);
    form->addRow(m_showStatusBar);
    form->addRow(m_showTabBar);
    form->addRow(tr("插入點閃爍週期"), m_caretBlinkRate);
    return page;
}

Settings PreferencesDialog::result() const
{
    Settings s = m_original;  // 以原設定為底，僅覆寫對話框實際編輯的欄位（保留未暴露欄位）
    s.theme = ThemeMode(m_theme->currentIndex());
    s.tabWidth = m_tabWidth->value();
    s.restoreOnLaunch = m_restore->isChecked();
    s.autosaveEnabled = m_autosave->isChecked();
    s.autosaveIntervalSec = m_autosaveInterval->value();
    s.singleInstance = m_singleInstance->isChecked();

    s.showLineNumbers = m_showLineNumbers->isChecked();
    s.showIndentGuides = m_showIndentGuides->isChecked();
    s.wordWrap = m_wordWrap->isChecked();
    s.showWhitespace = m_showWhitespace->isChecked();
    s.caretWidth = m_caretWidth->value();
    s.currentLineHighlight = m_currentLineHighlight->isChecked();
    s.enableVirtualSpace = m_enableVirtualSpace->isChecked();
    s.copyLineWithoutSelection = m_copyLineWithoutSelection->isChecked();

    s.defaultEol = Eol(m_defaultEol->currentIndex());
    s.defaultEncoding = Encoding(m_defaultEncoding->currentIndex());
    s.autoDetectFileStatus = m_autoDetectFileStatus->isChecked();
    s.sessionFileExt = m_sessionFileExt->text();

    s.backupMode = BackupMode(m_backupMode->currentIndex());
    s.backupDir = m_backupDir->text();
    s.autosaveOnFocusLoss = m_autosaveOnFocusLoss->isChecked();
    s.enableSessionSnapshot = m_enableSessionSnapshot->isChecked();
    s.snapshotIntervalSec = m_snapshotIntervalSec->value();

    s.autoInsertPairs = m_autoInsertPairs->isChecked();
    s.wordAutoComplete = m_wordAutoComplete->isChecked();
    s.acThreshold = m_acThreshold->value();
    s.showCallTips = m_showCallTips->isChecked();

    s.largeFileMB = m_largeFileMB->value();
    s.disableAutoCompleteOverMB = m_disableAutoCompleteOverMB->value();

    s.searchEngineUrl = m_searchEngineUrl->text();
    s.keepFindDialogOpen = m_keepFindDialogOpen->isChecked();
    s.confirmReplaceAll = m_confirmReplaceAll->isChecked();
    s.findInSelectionThreshold = m_findInSelectionThreshold->value();

    s.smartHighlight = m_smartHighlight->isChecked();
    s.highlightMatchingTags = m_highlightMatchingTags->isChecked();
    s.edgeColumn = m_edgeColumn->value();
    s.multiEdgeEnabled = m_multiEdgeEnabled->isChecked();
    s.showWrapSymbol = m_showWrapSymbol->isChecked();
    s.showEol = m_showEol->isChecked();

    s.theme = ThemeMode(m_darkModeTheme->currentIndex());  // 與 General 頁同步，取任一皆可
    s.showToolbar = m_showToolbar->isChecked();
    s.showStatusBar = m_showStatusBar->isChecked();
    s.showTabBar = m_showTabBar->isChecked();
    s.caretBlinkRate = m_caretBlinkRate->value();

    return s;
}

}  // namespace macpad::ui
