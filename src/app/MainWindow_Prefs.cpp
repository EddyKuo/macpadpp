#include "app/MainWindow.h"

#include "core/EditorWidget.h"
#include "core/LexerFactory.h"
#include "features/search/FindReplaceDialog.h"
#include "persistence/RecentFiles.h"
#include "persistence/SessionStore.h"
#include "persistence/SettingsStore.h"
#include "platform/ThemeManager.h"
#include "ui/EditorPane.h"
#include "extension/ExtensionRegistry.h"
#include "extension/builtin/WordCountExtension.h"
#include "extension/builtin/MarkdownPreviewExtension.h"
#include "features/findinfiles/FindInFilesDock.h"
#include "features/run/RunDock.h"
#include "features/run/RunCommand.h"
#include "features/udl/UdlLexer.h"
#include "features/export/HtmlExporter.h"
#include "features/textops/TextOps.h"
#include "features/mime/MimeTools.h"
#include "features/autocomplete/ApiDatabase.h"
#include "persistence/ThemeStore.h"
#include "ui/ThemePickerDialog.h"
#include "ui/SnapshotRecoveryDialog.h"
#include "features/findall/FindAllEngine.h"
#include "features/findall/FindAllDock.h"
#include "features/backup/BackupService.h"

#include <QDateTime>

#include <Qsci/qsciprinter.h>
#include <QPrintDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QTabBar>
#include "ui/DocumentListDock.h"
#include "ui/Panels.h"
#include "ui/CharacterPanel.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QClipboard>
#include <QScrollBar>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTabBar>
#include <QUrl>
#include "ui/WorkspaceDock.h"
#include "ui/PreferencesDialog.h"
#include "ui/ColumnEditorDialog.h"
#include "ui/StyleConfiguratorDialog.h"
#include "ui/ShortcutMapperDialog.h"
#include "ui/UdlEditorDialog.h"
#include "ui/ProjectPanelDock.h"
#include "ui/MacroManagerDialog.h"
#include "features/functionlist/FunctionListParser.h"
#include "features/columneditor/ColumnEditor.h"

#include <QCryptographicHash>
#include <QHash>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolBar>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

#include <Qsci/qscimacro.h>

#include <QApplication>
#include <QFileSystemWatcher>
#include <QStyleHints>
#include <QTimer>

using macpad::ui::EditorPane;

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QFrame>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QShortcut>
#include <QRadioButton>
#include <QSpinBox>
#include <QKeySequenceEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent>

using macpad::core::EditorWidget;


void MainWindow::applyViewPrefs(EditorWidget *editor)
{
    if (!editor)
        return;
    editor->setWrapMode(m_wordWrap ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
    editor->setWhitespaceVisibility(m_showWhitespace ? QsciScintilla::WsVisible
                                                     : QsciScintilla::WsInvisible);
    editor->setEolVisibility(m_showEol);
    editor->setIndentationGuides(m_showIndentGuide);
    editor->setWrapVisualFlags(m_showWrapSymbol ? QsciScintilla::WrapFlagByText
                                                : QsciScintilla::WrapFlagNone);
}


void MainWindow::applyEditorPrefs(EditorWidget *editor, const macpad::persistence::Settings &s)
{
    if (!editor)
        return;
    editor->setTabWidth(s.tabWidth);
    // 行號邊界：Editing 頁 showLineNumbers 與 Margins 頁 lineNumberMargin 皆需為真才顯示（兩者調和）
    editor->setShowLineNumbers(s.showLineNumbers && s.lineNumberMargin);
    editor->setCaretWidth(s.caretWidth);
    // 插入點閃爍週期（毫秒；0=不閃爍）
    editor->SendScintilla(QsciScintillaBase::SCI_SETCARETPERIOD,
                          static_cast<unsigned long>(qMax(0, s.caretBlinkRate)));
    // 邊界線（Margins/Border/Edge 頁）：edgeMode + edgeColumn
    {
        int mode = QsciScintillaBase::EDGE_NONE;
        switch (s.edgeMode) {
        case macpad::persistence::EdgeMode::None:       mode = QsciScintillaBase::EDGE_NONE; break;
        case macpad::persistence::EdgeMode::Line:       mode = QsciScintillaBase::EDGE_LINE; break;
        case macpad::persistence::EdgeMode::Background:  mode = QsciScintillaBase::EDGE_BACKGROUND; break;
        }
        editor->SendScintilla(QsciScintillaBase::SCI_SETEDGEMODE,
                              static_cast<unsigned long>(mode));
        if (s.edgeColumn > 0)
            editor->SendScintilla(QsciScintillaBase::SCI_SETEDGECOLUMN,
                                  static_cast<unsigned long>(s.edgeColumn));
        // 多重邊界線（multiEdgeEnabled）：於單一 edge 之外，在數個常用欄位加畫垂直參考線。
        editor->SendScintilla(QsciScintillaBase::SCI_MULTIEDGECLEARALL);
        if (s.multiEdgeEnabled) {
            const long colour = editor->SendScintilla(QsciScintillaBase::SCI_GETEDGECOLOUR);
            QList<int> cols;
            if (s.edgeColumn > 0)
                cols << s.edgeColumn;
            for (int c : {72, 80, 120}) {  // 合理預設欄位（去重）
                if (!cols.contains(c))
                    cols << c;
            }
            for (int c : cols)
                editor->SendScintilla(QsciScintillaBase::SCI_MULTIEDGEADDLINE,
                                      static_cast<unsigned long>(c), colour);
        }
    }
    // 摺疊邊界樣式（foldMarginStyle）：對應 Notepad++ Fold Margin Style
    editor->setFoldMarginStyle(static_cast<int>(s.foldMarginStyle));
    // Ctrl/⌘+雙擊選整個字（Delimiter 頁）與標示相符標籤（Highlighting 頁）
    editor->setCtrlDoubleClickWholeWord(s.ctrlDoubleClickWholeWord);
    editor->setHighlightMatchingTags(s.highlightMatchingTags);
    editor->setAutoClose(s.autoInsertPairs);          // 括號/引號自動配對
    editor->setWordCompletionEnabled(s.wordAutoComplete);
    editor->setAutoCompletionThreshold(s.acThreshold);
    editor->setCallTipsEnabled(s.showCallTips);
    editor->setCaretLineVisible(s.currentLineHighlight);  // 高亮目前所在行
    editor->setVirtualSpace(s.enableVirtualSpace);        // 允許插入點移至行尾之後
    editor->setColumnSelectionToMultiEdit(s.columnSelectionToMultiEdit);  // 欄選轉多游標
    applyDelimiters(editor, s);          // delimiterChars → 雙擊選字邊界
    applyPerLangTabWidth(editor, s);     // 依語言覆寫 Tab 寬度（否則沿用上面的全域 tabWidth）
    applyViewPrefs(editor);                            // wrap/whitespace/eol/縮排參考線
}


void MainWindow::applyDelimiters(EditorWidget *editor, const macpad::persistence::Settings &s)
{
    if (!editor)
        return;
    // 以 delimiterChars 為「邊界字元」：word-char 集合 = 可見 ASCII 去除空白與這些邊界字元。
    // 效果：雙擊選字會在這些分隔字元處斷開（如 '.' 為分隔字元則 foo.bar 拆成 foo / bar）。
    const QString delims = s.delimiterChars;
    QByteArray wordChars;
    for (char c = 0x21; c < 0x7F; ++c) {
        if (!delims.contains(QLatin1Char(c)))
            wordChars.append(c);
    }
    editor->SendScintilla(QsciScintillaBase::SCI_SETWORDCHARS, 0UL, wordChars.constData());
}


void MainWindow::applyPerLangTabWidth(EditorWidget *editor, const macpad::persistence::Settings &s)
{
    if (!editor)
        return;
    // 依目前 lexer 對應的語言鍵查覆寫；未列出者維持全域 tabWidth（applyEditorPrefs 已先設過）。
    if (s.perLangTabWidth.isEmpty())
        return;
    const QString langKey = languageKeyForLexer(editor->lexer());
    if (!langKey.isEmpty() && s.perLangTabWidth.contains(langKey))
        editor->setTabWidth(s.perLangTabWidth.value(langKey));
}


void MainWindow::applyWindowPrefs(const macpad::persistence::Settings &s)
{
    // 工具列可見性 + 圖示大小（FR-053 Toolbar 頁）
    if (m_toolbar) {
        m_toolbar->setVisible(s.showToolbar);
        int px = 18;
        switch (s.toolbarIconSize) {
        case macpad::persistence::ToolbarIconSize::Small:    px = 16; break;
        case macpad::persistence::ToolbarIconSize::Standard: px = 24; break;
        case macpad::persistence::ToolbarIconSize::Large:    px = 32; break;
        }
        m_toolbar->setIconSize(QSize(px, px));
    }
    // 狀態列可見性
    statusBar()->setVisible(s.showStatusBar);
    // 分頁列：可見性 / 垂直排列 / 關閉鈕 / 多行（多行以停用捲動鈕近似，QTabBar 無原生多行）
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        w->tabBar()->setVisible(s.showTabBar);
        w->setTabPosition(s.tabBarVertical ? QTabWidget::West : QTabWidget::North);
        w->setTabsClosable(s.tabBarShowCloseButton);
        w->tabBar()->setUsesScrollButtons(!s.tabBarMultiLine);
    }
    // 最近檔案選單依偏好（max/full-path/submenu）重建
    rebuildRecentMenu();
}


QString MainWindow::startDirForDialog() const
{
    const auto s = macpad::persistence::SettingsStore::load();
    switch (s.defaultDirPolicy) {
    case macpad::persistence::DefaultDirPolicy::FixedPath:
        if (!s.defaultDirFixedPath.isEmpty())
            return s.defaultDirFixedPath;
        break;
    case macpad::persistence::DefaultDirPolicy::RememberLast:
        if (!m_lastDir.isEmpty())
            return m_lastDir;
        break;
    case macpad::persistence::DefaultDirPolicy::FollowCurrentDoc:
        break;
    }
    // FollowCurrentDoc（或上述無值時的退回）：目前文件所在資料夾
    if (EditorWidget *e = const_cast<MainWindow *>(this)->currentEditor(); e && !e->isUntitled())
        return QFileInfo(e->filePath()).absolutePath();
    return QString();
}


void MainWindow::applyCliWindowOptions(bool alwaysOnTop, const QString &titleAdd)
{
    // -alwaysOnTop：沿用既有置頂路徑（含 setWindowFlag + 重新 show）
    if (alwaysOnTop)
        toggleAlwaysOnTop(true);
    // -title:/-titleAdd:：附加於視窗標題
    if (!titleAdd.isEmpty()) {
        const QString base = windowTitle().isEmpty() ? QStringLiteral("macpad++") : windowTitle();
        setWindowTitle(base + QStringLiteral(" - ") + titleAdd);
    }
}


// -notabbar：隱藏/顯示分頁列（兩檢視同步）
void MainWindow::setTabBarVisible(bool visible)
{
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (w)
            w->tabBar()->setVisible(visible);
    }
}


// -fullReadOnly：全域唯讀——所有已開啟編輯器設唯讀（較 -ro 嚴格，涵蓋全部分頁）
void MainWindow::setFullReadOnly(bool on)
{
    forEachEditor([on](EditorWidget *e) {
        if (e)
            e->setReadOnly(on);
    });
}


// -udl=<name>：對作用中編輯器套用指定名稱的使用者定義語言（UDL）
void MainWindow::applyUdlByName(const QString &name)
{
    if (name.isEmpty())
        return;
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    for (const auto &def : m_udl.definitions()) {
        if (def.name == name) {
            e->setLexer(new macpad::features::UdlLexer(def, e));
            return;
        }
    }
}


// -monitor：對所有已開啟且已存檔的分頁啟用外部異動監控（tail -f 式）
void MainWindow::enableMonitoringForOpenFiles()
{
    forEachEditor([this](EditorWidget *e) {
        if (e && !e->isUntitled()) {
            const QString path = e->filePath();
            if (!m_monitored.contains(path)) {
                m_monitored.insert(path);
                watchPath(path);
                e->setReadOnly(true);
            }
        }
    });
}


// -openFoldersAsWorkspace <folder>：將資料夾加入工作區（多根，不清除既有）
void MainWindow::addWorkspaceFolder(const QString &path)
{
    if (path.isEmpty() || !m_workspace)
        return;
    m_workspace->addRoot(path);
    m_workspace->show();
    m_workspace->raise();
}
