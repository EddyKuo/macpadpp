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
using macpad::persistence::DefaultDirPolicy;
using macpad::persistence::EdgeMode;
using macpad::persistence::FileStatusAutoDetectMode;
using macpad::persistence::FoldMarginStyle;
using macpad::persistence::MultiInstanceMode;
using macpad::persistence::Settings;
using macpad::persistence::ThemeMode;
using macpad::persistence::ToolbarIconSize;

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
    m_tabs->addTab(buildToolbarPage(), tr("Toolbar"));
    m_tabs->addTab(buildTabBarPage(), tr("Tab Bar"));
    m_tabs->addTab(buildMarginsPage(), tr("Margins/Border/Edge"));
    m_tabs->addTab(buildDefaultDirectoryPage(), tr("Default Directory"));
    m_tabs->addTab(buildRecentFilesPage(), tr("Recent Files History"));
    m_tabs->addTab(buildLanguagePage(), tr("Language"));
    m_tabs->addTab(buildMultiInstanceDatePage(), tr("Multi-Instance & Date"));
    m_tabs->addTab(buildDelimiterPage(), tr("Delimiter"));
    m_tabs->addTab(buildMiscPage(), tr("MISC"));

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

    m_columnSelectionToMultiEdit = new QCheckBox(tr("欄選結束時轉為多游標編輯"), page);
    m_columnSelectionToMultiEdit->setChecked(current.columnSelectionToMultiEdit);

    auto *form = new QFormLayout(page);
    form->addRow(m_showLineNumbers);
    form->addRow(m_showIndentGuides);
    form->addRow(m_wordWrap);
    form->addRow(m_showWhitespace);
    form->addRow(tr("插入點寬度"), m_caretWidth);
    form->addRow(m_currentLineHighlight);
    form->addRow(m_enableVirtualSpace);
    form->addRow(m_copyLineWithoutSelection);
    form->addRow(m_columnSelectionToMultiEdit);
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

QWidget *PreferencesDialog::buildToolbarPage()
{
    // 注意：工具列「顯示/隱藏」開關（showToolbar）已在 Dark Mode/Appearance 頁提供
    // （m_showToolbar 為單一 widget，避免同一欄位由兩個 widget 各自持有不同步的值）。
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_toolbarIconSize = new QComboBox(page);
    m_toolbarIconSize->addItems({tr("小"), tr("標準"), tr("大")});
    m_toolbarIconSize->setCurrentIndex(int(current.toolbarIconSize));

    auto *form = new QFormLayout(page);
    form->addRow(tr("圖示大小"), m_toolbarIconSize);
    return page;
}

QWidget *PreferencesDialog::buildTabBarPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_tabBarMultiLine = new QCheckBox(tr("多行顯示分頁（放不下時換行）"), page);
    m_tabBarMultiLine->setChecked(current.tabBarMultiLine);

    m_tabBarVertical = new QCheckBox(tr("分頁列垂直排列"), page);
    m_tabBarVertical->setChecked(current.tabBarVertical);

    m_tabBarShowCloseButton = new QCheckBox(tr("分頁上顯示關閉按鈕"), page);
    m_tabBarShowCloseButton->setChecked(current.tabBarShowCloseButton);

    m_tabBarDoubleClickCloses = new QCheckBox(tr("雙擊分頁關閉"), page);
    m_tabBarDoubleClickCloses->setChecked(current.tabBarDoubleClickCloses);

    auto *form = new QFormLayout(page);
    form->addRow(m_tabBarMultiLine);
    form->addRow(m_tabBarVertical);
    form->addRow(m_tabBarShowCloseButton);
    form->addRow(m_tabBarDoubleClickCloses);
    return page;
}

QWidget *PreferencesDialog::buildMarginsPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_edgeMode = new QComboBox(page);
    m_edgeMode->addItems({tr("不顯示"), tr("垂直線"), tr("背景變色")});
    m_edgeMode->setCurrentIndex(int(current.edgeMode));

    m_foldMarginStyle = new QComboBox(page);
    m_foldMarginStyle->addItems({tr("不顯示"), tr("簡易"), tr("箭頭"), tr("圓形"), tr("方框")});
    m_foldMarginStyle->setCurrentIndex(int(current.foldMarginStyle));

    m_lineNumberMargin = new QCheckBox(tr("顯示行號邊界"), page);
    m_lineNumberMargin->setChecked(current.lineNumberMargin);

    auto *form = new QFormLayout(page);
    form->addRow(tr("邊界線模式"), m_edgeMode);
    form->addRow(tr("摺疊邊界樣式"), m_foldMarginStyle);
    form->addRow(m_lineNumberMargin);
    return page;
}

QWidget *PreferencesDialog::buildDefaultDirectoryPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_defaultDirPolicy = new QComboBox(page);
    m_defaultDirPolicy->addItems({tr("跟隨目前文件所在目錄"), tr("記住上次使用的目錄"), tr("固定目錄")});
    m_defaultDirPolicy->setCurrentIndex(int(current.defaultDirPolicy));

    m_defaultDirFixedPath = new QLineEdit(page);
    m_defaultDirFixedPath->setText(current.defaultDirFixedPath);
    m_defaultDirFixedPath->setPlaceholderText(tr("固定目錄路徑（policy 為「固定目錄」時使用）"));

    auto *form = new QFormLayout(page);
    form->addRow(tr("開啟/儲存預設目錄"), m_defaultDirPolicy);
    form->addRow(tr("固定目錄路徑"), m_defaultDirFixedPath);
    return page;
}

QWidget *PreferencesDialog::buildRecentFilesPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_recentFilesMaxEntries = new QSpinBox(page);
    m_recentFilesMaxEntries->setRange(0, 100);
    m_recentFilesMaxEntries->setSpecialValueText(tr("關閉"));
    m_recentFilesMaxEntries->setValue(current.recentFilesMaxEntries);

    m_recentFilesShowFullPath = new QCheckBox(tr("顯示完整路徑"), page);
    m_recentFilesShowFullPath->setChecked(current.recentFilesShowFullPath);

    m_recentFilesInSubmenu = new QCheckBox(tr("收於子選單（而非直接列於 File 選單）"), page);
    m_recentFilesInSubmenu->setChecked(current.recentFilesInSubmenu);

    auto *form = new QFormLayout(page);
    form->addRow(tr("最多記錄筆數"), m_recentFilesMaxEntries);
    form->addRow(m_recentFilesShowFullPath);
    form->addRow(m_recentFilesInSubmenu);
    return page;
}

QWidget *PreferencesDialog::buildLanguagePage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_disabledLanguages = new QLineEdit(page);
    m_disabledLanguages->setText(current.disabledLanguages.join(QStringLiteral(", ")));
    m_disabledLanguages->setPlaceholderText(tr("逗號分隔，例如：Python, JSON"));

    m_perLangTabWidth = new QLineEdit(page);
    {
        QStringList pairs;
        for (auto it = current.perLangTabWidth.constBegin(); it != current.perLangTabWidth.constEnd(); ++it)
            pairs << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
        m_perLangTabWidth->setText(pairs.join(QStringLiteral(", ")));
    }
    m_perLangTabWidth->setPlaceholderText(tr("語言=寬度，逗號分隔，例如：Python=2, C++=4"));

    auto *form = new QFormLayout(page);
    form->addRow(tr("停用的語言"), m_disabledLanguages);
    form->addRow(tr("各語言 Tab 寬度覆寫"), m_perLangTabWidth);
    return page;
}

QWidget *PreferencesDialog::buildMultiInstanceDatePage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_multiInstanceMode = new QComboBox(page);
    m_multiInstanceMode->addItems(
        {tr("單一執行個體"), tr("Session 期間內共用"), tr("永遠開啟新執行個體")});
    m_multiInstanceMode->setCurrentIndex(int(current.multiInstanceMode));

    m_dateFormat = new QLineEdit(page);
    m_dateFormat->setText(current.dateFormat);
    m_dateFormat->setPlaceholderText(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    m_customDateFormat = new QLineEdit(page);
    m_customDateFormat->setText(current.customDateFormat);
    m_customDateFormat->setPlaceholderText(tr("自訂格式（選填）"));

    auto *form = new QFormLayout(page);
    form->addRow(tr("多重執行個體模式"), m_multiInstanceMode);
    form->addRow(tr("日期/時間插入格式"), m_dateFormat);
    form->addRow(tr("自訂格式"), m_customDateFormat);
    return page;
}

QWidget *PreferencesDialog::buildDelimiterPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_delimiterChars = new QLineEdit(page);
    m_delimiterChars->setText(current.delimiterChars);

    m_ctrlDoubleClickWholeWord = new QCheckBox(tr("Ctrl+雙擊選取整個字"), page);
    m_ctrlDoubleClickWholeWord->setChecked(current.ctrlDoubleClickWholeWord);

    auto *form = new QFormLayout(page);
    form->addRow(tr("雙擊選字邊界字元"), m_delimiterChars);
    form->addRow(m_ctrlDoubleClickWholeWord);
    return page;
}

QWidget *PreferencesDialog::buildMiscPage()
{
    const Settings &current = m_original;
    auto *page = new QWidget(this);

    m_docSwitcherEnabled = new QCheckBox(tr("啟用文件切換器（Ctrl+Tab 清單）"), page);
    m_docSwitcherEnabled->setChecked(current.docSwitcherEnabled);

    m_docPeekerEnabled = new QCheckBox(tr("切換時預覽文件內容"), page);
    m_docPeekerEnabled->setChecked(current.docPeekerEnabled);

    m_fileStatusAutoDetect = new QComboBox(page);
    m_fileStatusAutoDetect->addItems({tr("停用"), tr("啟用（提示）"), tr("啟用（靜默更新）")});
    m_fileStatusAutoDetect->setCurrentIndex(int(current.fileStatusAutoDetect));

    m_autoUpdater = new QCheckBox(tr("啟動時檢查更新"), page);
    m_autoUpdater->setChecked(current.autoUpdater);

    m_enableSound = new QCheckBox(tr("動作提示音"), page);
    m_enableSound->setChecked(current.enableSound);

    auto *form = new QFormLayout(page);
    form->addRow(m_docSwitcherEnabled);
    form->addRow(m_docPeekerEnabled);
    form->addRow(tr("檔案外部變更偵測"), m_fileStatusAutoDetect);
    form->addRow(m_autoUpdater);
    form->addRow(m_enableSound);
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
    s.columnSelectionToMultiEdit = m_columnSelectionToMultiEdit->isChecked();

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

    s.toolbarIconSize = ToolbarIconSize(m_toolbarIconSize->currentIndex());

    s.tabBarMultiLine = m_tabBarMultiLine->isChecked();
    s.tabBarVertical = m_tabBarVertical->isChecked();
    s.tabBarShowCloseButton = m_tabBarShowCloseButton->isChecked();
    s.tabBarDoubleClickCloses = m_tabBarDoubleClickCloses->isChecked();

    s.edgeMode = EdgeMode(m_edgeMode->currentIndex());
    s.foldMarginStyle = FoldMarginStyle(m_foldMarginStyle->currentIndex());
    s.lineNumberMargin = m_lineNumberMargin->isChecked();

    s.defaultDirPolicy = DefaultDirPolicy(m_defaultDirPolicy->currentIndex());
    s.defaultDirFixedPath = m_defaultDirFixedPath->text();

    s.recentFilesMaxEntries = m_recentFilesMaxEntries->value();
    s.recentFilesShowFullPath = m_recentFilesShowFullPath->isChecked();
    s.recentFilesInSubmenu = m_recentFilesInSubmenu->isChecked();

    {
        QStringList langs;
        for (const QString &part : m_disabledLanguages->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            langs << part.trimmed();
        s.disabledLanguages = langs;
    }
    {
        QMap<QString, int> map;
        const QStringList pairs =
            m_perLangTabWidth->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &pair : pairs) {
            const int eq = pair.indexOf(QLatin1Char('='));
            if (eq <= 0) continue;
            const QString lang = pair.left(eq).trimmed();
            bool ok = false;
            const int width = pair.mid(eq + 1).trimmed().toInt(&ok);
            if (!lang.isEmpty() && ok && width > 0) map.insert(lang, width);
        }
        s.perLangTabWidth = map;
    }

    s.multiInstanceMode = MultiInstanceMode(m_multiInstanceMode->currentIndex());
    s.dateFormat = m_dateFormat->text();
    s.customDateFormat = m_customDateFormat->text();

    s.delimiterChars = m_delimiterChars->text();
    s.ctrlDoubleClickWholeWord = m_ctrlDoubleClickWholeWord->isChecked();

    s.docSwitcherEnabled = m_docSwitcherEnabled->isChecked();
    s.docPeekerEnabled = m_docPeekerEnabled->isChecked();
    s.fileStatusAutoDetect = FileStatusAutoDetectMode(m_fileStatusAutoDetect->currentIndex());
    s.autoUpdater = m_autoUpdater->isChecked();
    s.enableSound = m_enableSound->isChecked();

    return s;
}

}  // namespace macpad::ui
