#include "app/MainWindow.h"

#include "core/EditorWidget.h"
#include "platform/DesktopIntegration.h"
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


// Dual-View（FR-062）：Find All 結果的 docId 以 view*kViewDocIdBase + tabIndex 複合編碼，
// 供結果面板跳轉時解碼回（檢視, 分頁）。單一檢視分頁數遠小於此基數，不會溢位重疊。
static constexpr int kViewDocIdBase = 100000;


// 把 SVG 圖示渲染並整體 tint 成指定顏色（讓單色線圖能隨主題明暗自動變色）。
static QIcon tintedSvgIcon(const QString &path, const QColor &color)
{
    QSvgRenderer renderer(path);
    const int px = 40;   // 內部高解析，QIcon 會依需要縮放
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p);
    // SourceIn：保留 alpha、把顏色換成主題色
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    return QIcon(pm);
}


static QString lineCommentMarker(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == "py" || s == "pyw" || s == "sh" || s == "bash" || s == "zsh" ||
        s == "yml" || s == "yaml" || s == "rb" || s == "pl" || s == "cmake" || s == "r")
        return QStringLiteral("#");
    if (s == "sql" || s == "lua" || s == "hs")
        return QStringLiteral("--");
    if (s == "lisp" || s == "clj" || s == "el")
        return QStringLiteral(";");
    return QStringLiteral("//");  // C 家族預設
}


void MainWindow::buildToolbar()
{
    Q_INIT_RESOURCE(icons);   // 圖示 qrc 編在靜態庫,須顯式初始化
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setObjectName(QStringLiteral("MainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);  // 只顯示圖示,文字作為 tooltip

    // (圖示名, 顯示/提示文字, 觸發)。以分隔線分組,對齊 Notepad++ 工具列。
    auto add = [this](const char *icon, const QString &tip, auto slot) {
        QAction *a = m_toolbar->addAction(tip);   // 文字→tooltip
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, slot);
        m_tbIcons.append({a, QString::fromLatin1(icon)});
    };
    add("new", tr("New"), [this] { newFile(); });
    add("open", tr("Open…"), [this] { openFileDialog(); });
    add("save", tr("Save"), [this] { saveCurrent(); });
    add("saveall", tr("Save All"), [this] { saveAll(); });
    add("close", tr("Close Tab"), [this] {
        QTabWidget *w = currentTabWidget();
        if (w->count()) closeTab(w->currentIndex());
    });
    add("closeall", tr("Close All"), [this] { closeAllTabs(); });
    add("print", tr("Print…"), [this] {
        if (EditorWidget *e = currentEditor()) {
            QsciPrinter printer; QPrintDialog dlg(&printer, this);
            if (dlg.exec() == QDialog::Accepted) printer.printRange(e);
        }
    });
    m_toolbar->addSeparator();
    add("cut", tr("Cut"), [this] { if (auto *e = currentEditor()) e->cut(); });
    add("copy", tr("Copy"), [this] { if (auto *e = currentEditor()) e->copy(); });
    add("paste", tr("Paste"), [this] { if (auto *e = currentEditor()) e->paste(); });
    m_toolbar->addSeparator();
    add("undo", tr("Undo"), [this] { if (auto *e = currentEditor()) e->undo(); });
    add("redo", tr("Redo"), [this] { if (auto *e = currentEditor()) e->redo(); });
    m_toolbar->addSeparator();
    add("find", tr("Find…"), [this] { showFind(); });
    add("replace", tr("Replace…"), [this] { showReplace(); });
    m_toolbar->addSeparator();
    add("zoomin", tr("Zoom In"), [this] { if (auto *e = currentEditor()) e->zoomIn(); });
    add("zoomout", tr("Zoom Out"), [this] { if (auto *e = currentEditor()) e->zoomOut(); });
    m_toolbar->addSeparator();
    // 透過 View 選單同名的可勾選 QAction 切換，避免工具列與選單狀態不同步
    add("wordwrap", tr("Word Wrap"), [this] {
        if (m_wrapAct) m_wrapAct->toggle();
    });
    add("showall", tr("Show All Characters"), [this] {
        // 一鍵開/關空白＋EOL 顯示；setChecked 會觸發各自的 toggled 更新狀態與畫面
        const bool on = !(m_showWhitespace && m_showEol);
        if (m_wsAct) m_wsAct->setChecked(on);
        if (m_eolAct) m_eolAct->setChecked(on);
    });

    retintToolbar();   // 依目前主題上色
}


void MainWindow::retintToolbar()
{
    if (!m_toolbar)
        return;
    const QColor c = palette().color(QPalette::WindowText);
    for (const auto &pair : m_tbIcons)
        pair.first->setIcon(tintedSvgIcon(QStringLiteral(":/icons/%1.svg").arg(pair.second), c));
}


void MainWindow::createMenus()
{
    // 先依 Notepad++ 慣例順序建立所有頂層選單（再各自填內容）。
    // 順序很重要：macOS 選單列由左至右填,notch/視窗寬度不足時右側選單會被裁掉,
    // 故把常用的 File/Edit/Search/View 放最前,確保永遠可見（修正「找不到 View」）。
    QMenu *fileMenu     = menuBar()->addMenu(tr("&File"));
    QMenu *editMenu     = menuBar()->addMenu(tr("&Edit"));
    QMenu *searchMenu   = menuBar()->addMenu(tr("&Search"));
    QMenu *viewMenu     = menuBar()->addMenu(tr("&View"));
    QMenu *formatMenu   = menuBar()->addMenu(tr("&Encoding"));
    QMenu *langMenu     = menuBar()->addMenu(tr("&Language"));
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    QMenu *toolsMenu    = menuBar()->addMenu(tr("&Tools"));
    QMenu *macroMenu    = menuBar()->addMenu(tr("&Macro"));
    QMenu *runMenu      = menuBar()->addMenu(tr("&Run"));
    QMenu *pluginMenu   = menuBar()->addMenu(tr("&Plugins"));
    m_windowMenu        = menuBar()->addMenu(tr("&Window"));
    // 以 objectName 記錄「英文選單鍵」——供 addMenuAction 穩定比對(翻譯後 title 會變,不能用 title 比)
    fileMenu->setObjectName(QStringLiteral("File"));
    m_fileMenu = fileMenu;  // 供 rebuildRecentMenu 在 File 選單直接列出最近檔案（recentFilesInSubmenu=false）
    editMenu->setObjectName(QStringLiteral("Edit"));
    searchMenu->setObjectName(QStringLiteral("Search"));
    viewMenu->setObjectName(QStringLiteral("View"));
    formatMenu->setObjectName(QStringLiteral("Encoding"));
    langMenu->setObjectName(QStringLiteral("Language"));
    settingsMenu->setObjectName(QStringLiteral("Settings"));
    toolsMenu->setObjectName(QStringLiteral("Tools"));
    macroMenu->setObjectName(QStringLiteral("Macro"));
    runMenu->setObjectName(QStringLiteral("Run"));
    pluginMenu->setObjectName(QStringLiteral("Plugins"));
    m_windowMenu->setObjectName(QStringLiteral("Window"));

    createFileMenu(fileMenu);
    createEditMenu(editMenu);
    createSearchMenu(searchMenu);  // Notepad++ Search 選單（Find Next/Prev、Mark、書籤進階等）
    createEncodingMenu(formatMenu);
    createLanguageMenu(langMenu);
    createSettingsMenu(settingsMenu);
    createToolsMenu(toolsMenu);
    createMacroMenu(macroMenu);
    createRunMenu(runMenu);
    createViewMenu(viewMenu);
    createPluginsMenu(pluginMenu);

    // Window 選單（Notepad++ Window）——列出所有開啟文件並可切換（選單已於函式開頭建立）
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::buildWindowMenu);

    // Preferences（Cmd+,）
    // 重要：不可用 menuBar()->addAction（bare action）——macOS 原生選單列只接受子選單，
    // bare action 會迫使 Qt 退回「非原生視窗內選單列」，造成選單一半在螢幕頂端、一半在視窗上。
    // 正解：建立成一般 QAction，設 PreferencesRole，Qt 會自動移入 macOS 應用程式選單（macpad++ ▸ Preferences）。
    QAction *prefsAct = new QAction(tr("Preferences…"), this);
    prefsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    prefsAct->setMenuRole(QAction::PreferencesRole);
    connect(prefsAct, &QAction::triggered, this, [this] {
        const auto cur = macpad::persistence::SettingsStore::load();
        macpad::ui::PreferencesDialog dlg(cur, this);
        if (dlg.exec() == QDialog::Accepted) {
            const auto s = dlg.result();
            macpad::persistence::SettingsStore::save(s);
            // 直接可套用的編輯偏好：借道既有 View 選單同名可勾選動作，狀態與畫面自動同步。
            if (m_wrapAct) m_wrapAct->setChecked(s.wordWrap);
            if (m_wsAct) m_wsAct->setChecked(s.showWhitespace);
            m_showIndentGuide = s.showIndentGuides;
            // 逐分頁即時套用可即時生效的偏好（tabWidth/行號/游標寬/自動配對/自動完成/call tip/縮排參考線）
            forEachEditor([this, &s](EditorWidget *e) { applyEditorPrefs(e, s); });
            applySnapshotTimerSettings(s);  // 依偏好啟停/調整當機復原快照計時器
            applyWindowPrefs(s);            // 工具列/狀態列/分頁列可見性與圖示大小、最近檔案選單
            rebuildRunShortcuts();          // Run 指令快捷鍵（偏好可能影響，保守重建）
            applyTheme();
            statusBar()->showMessage(tr("偏好設定已儲存"), 4000);
        }
    });

    // About / Quit：設定 role，Qt 於 macOS 統一移入應用程式選單（讓 app menu 完整原生）
    QAction *aboutAct = new QAction(tr("About macpad++"), this);
    aboutAct->setMenuRole(QAction::AboutRole);
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About macpad++"),
                           tr("macpad++ — Notepad++ 對等的原生 macOS 編輯器\nQt6 + QScintilla"));
    });
    QAction *quitAct = new QAction(tr("Quit macpad++"), this);
    quitAct->setShortcut(QKeySequence::Quit);
    quitAct->setMenuRole(QAction::QuitRole);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    // 把三個 app-menu 動作掛進 File 選單（有實際容器）；macOS 會依 role 自動搬到應用程式選單，
    // 其他平台則留在 File。關鍵是它們都在 QMenu 內，不再是 bare action。
    fileMenu->addSeparator();
    fileMenu->addAction(prefsAct);
    fileMenu->addAction(aboutAct);
    fileMenu->addAction(quitAct);

    // 套用使用者在 Shortcut Mapper 儲存的快捷鍵覆寫
    QList<QAction *> allActs;
    for (QAction *a : menuBar()->findChildren<QAction *>())
        if (!a->menu() && !a->isSeparator() && !a->text().isEmpty())
            allActs.append(a);
    macpad::ui::ShortcutMapperDialog::applySavedOverrides(allActs);
}


void MainWindow::createFileMenu(QMenu *fileMenu)
{
    // File 選單（Mac 慣例快捷鍵——QKeySequence 標準鍵在 macOS 自動對映 Cmd，FR-024）
    fileMenu->addAction(tr("New"), QKeySequence::New, this, &MainWindow::newFile);
    fileMenu->addAction(tr("Open…"), QKeySequence::Open, this, &MainWindow::openFileDialog);
    m_recentMenu = fileMenu->addMenu(tr("Open Recent"));

    // 具名 Session 管理（FR-016 進階）
    QMenu *sessionMenu = fileMenu->addMenu(tr("Sessions"));
    sessionMenu->addAction(tr("Save Session As…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Session"),
                                                   tr("Session name:"), QLineEdit::Normal,
                                                   QString(), &ok);
        if (ok && !name.isEmpty()) {
            macpad::persistence::SessionStore::saveNamed(name, buildCurrentSession());
            statusBar()->showMessage(tr("Session「%1」已儲存").arg(name), 3000);
        }
    });
    sessionMenu->addAction(tr("Load Session…"), this, [this] {
        const QStringList names = macpad::persistence::SessionStore::listNames();
        if (names.isEmpty()) {
            QMessageBox::information(this, tr("Load Session"), tr("尚無已儲存的 session"));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getItem(this, tr("Load Session"), tr("Session:"),
                                                   names, 0, false, &ok);
        if (ok && !name.isEmpty())
            openSessionState(macpad::persistence::SessionStore::loadNamed(name));
    });

    fileMenu->addAction(tr("Open Folder as Workspace…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
        if (!dir.isEmpty() && m_workspace) {
            m_workspace->setRoot(dir);
            m_workspace->show();
            m_workspace->raise();
        }
    });
    fileMenu->addAction(tr("Add Folder to Workspace…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder to Workspace"));
        if (!dir.isEmpty() && m_workspace) {
            m_workspace->addRoot(dir);   // 多根支援：不清除既有根目錄
            m_workspace->show();
            m_workspace->raise();
        }
    });
    fileMenu->addAction(tr("Open Containing Folder"), this, [this] { revealInFinder(); });
    fileMenu->addAction(tr("Open in Default Application"), this, [this] { openInDefaultApp(); });
    fileMenu->addAction(tr("Reload from Disk"), this, [this] { reloadFromDisk(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Save"), QKeySequence::Save, this, [this] { saveCurrent(); });
    fileMenu->addAction(tr("Save As…"), QKeySequence::SaveAs, this, [this] { saveCurrentAs(); });
    fileMenu->addAction(tr("Save a Copy As…"), this, [this] { saveCopyAs(); });
    fileMenu->addAction(tr("Save All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
                        this, [this] { saveAll(); });
    fileMenu->addAction(tr("Rename…"), this, [this] { renameCurrentFile(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("New Window"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
                        this, [] {
        // 多視窗（FR-025/034）：另開一個獨立主視窗
        auto *w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Print…"), QKeySequence::Print, this, [this] {
        EditorWidget *e = currentEditor();
        if (!e) return;
        QsciPrinter printer;               // 保留語法高亮列印（FR-036）
        QPrintDialog dlg(&printer, this);
        if (dlg.exec() == QDialog::Accepted)
            printer.printRange(e);
    });
    fileMenu->addAction(tr("Export as HTML…"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e) return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export as HTML"),
                                                          QString(), tr("HTML (*.html)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(macpad::features::HtmlExporter::toHtml(e).toUtf8());
            statusBar()->showMessage(tr("已匯出 HTML"), 3000);
        }
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Close Tab"), QKeySequence::Close, this, [this] {
        QTabWidget *w = currentTabWidget();
        if (w->count() > 0)
            closeTab(w->currentIndex());
    });
    fileMenu->addAction(tr("Close All"), this, [this] { closeAllTabs(); });
    fileMenu->addAction(tr("Close All but This"), this, [this] { closeAllButCurrent(); });
    fileMenu->addAction(tr("Restore Recent Closed File"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), this,
                        [this] { restoreClosedTab(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Move to Recycle Bin"), this, [this] { moveCurrentToTrash(); });
}


void MainWindow::createEditMenu(QMenu *editMenu)
{
    // Edit 選單（QScintilla 內建 undo/redo/剪貼；多游標為 Cmd+Click，FR-005）
    editMenu->addAction(tr("Undo"), QKeySequence::Undo, this, [this] {
        if (auto *e = currentEditor()) e->undo();
    });
    editMenu->addAction(tr("Redo"), QKeySequence::Redo, this, [this] {
        if (auto *e = currentEditor()) e->redo();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Cut"), QKeySequence::Cut, this, [this] {
        auto *e = currentEditor();
        if (!e) return;
        // copyLineWithoutSelection：無選取時剪下整行（Notepad++ 慣例）
        if (e->selectedText().isEmpty()
            && macpad::persistence::SettingsStore::load().copyLineWithoutSelection)
            e->SendScintilla(QsciScintillaBase::SCI_LINECUT);
        else
            e->cut();
    });
    editMenu->addAction(tr("Copy"), QKeySequence::Copy, this, [this] {
        auto *e = currentEditor();
        if (!e) return;
        // copyLineWithoutSelection：無選取時複製整行（Notepad++ 慣例）
        if (e->selectedText().isEmpty()
            && macpad::persistence::SettingsStore::load().copyLineWithoutSelection)
            e->SendScintilla(QsciScintillaBase::SCI_LINECOPY);
        else
            e->copy();
    });
    editMenu->addAction(tr("Paste"), QKeySequence::Paste, this, [this] {
        if (auto *e = currentEditor()) e->paste();
    });
    editMenu->addAction(tr("Delete"), this, [this] {
        if (auto *e = currentEditor()) e->removeSelectedText();
    });
    editMenu->addAction(tr("Select All"), QKeySequence::SelectAll, this, [this] {
        if (auto *e = currentEditor()) e->selectAll();
    });
    createEditMenuOps(editMenu);  // 大小寫/行操作/空白/註解/插入（Notepad++ 對等）
    editMenu->addSeparator();
    editMenu->addAction(tr("Find…"), QKeySequence::Find, this, &MainWindow::showFind);
    editMenu->addAction(tr("Replace…"), QKeySequence::Replace, this, &MainWindow::showReplace);
    editMenu->addSeparator();
    editMenu->addAction(tr("Go to Line…"), QKeySequence(Qt::CTRL | Qt::Key_L), this,
                        [this] { gotoLineOrOffset(); });
    editMenu->addAction(tr("Go to Matching Brace"), QKeySequence(Qt::CTRL | Qt::Key_B), this, [this] {
        if (auto *e = currentEditor()) e->moveToMatchingBrace();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Find in Files…"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this, [this] {
        if (!m_findInFiles) {
            m_findInFiles = new macpad::features::FindInFilesDock(this);
            addDockWidget(Qt::BottomDockWidgetArea, m_findInFiles);
            connect(m_findInFiles, &macpad::features::FindInFilesDock::openLocation,
                    this, &MainWindow::openFileAtLine);
        }
        // 預設搜尋目錄：目前檔案所在資料夾
        if (EditorWidget *e = currentEditor(); e && !e->isUntitled())
            m_findInFiles->setSearchRoot(QFileInfo(e->filePath()).absolutePath());
        m_findInFiles->show();
        m_findInFiles->raise();
    });
    // Find in Projects（複刻 Notepad++「Find in Projects」：限定於 Project Panel 的檔案清單搜尋）
    // 無預設快捷鍵（Notepad++ 亦無；且 Ctrl+Alt+Shift+F 已被 Unfold All 佔用）
    editMenu->addAction(tr("Find in Projects…"), this,
                        [this] { showFindInProjects(); });

    // 書籤（FR-008）——置於 Edit 選單
    editMenu->addSeparator();
    editMenu->addAction(tr("Toggle Bookmark"), QKeySequence(Qt::CTRL | Qt::Key_F2), this, [this] {
        if (auto *ed = currentEditor()) ed->toggleBookmark();
    });
    editMenu->addAction(tr("Next Bookmark"), QKeySequence(Qt::Key_F2), this, [this] {
        if (auto *ed = currentEditor()) ed->nextBookmark();
    });
    editMenu->addAction(tr("Previous Bookmark"), QKeySequence(Qt::SHIFT | Qt::Key_F2), this, [this] {
        if (auto *ed = currentEditor()) ed->prevBookmark();
    });

    // 變更歷史（FR-057）
    editMenu->addSeparator();
    QMenu *changeHistoryMenu = editMenu->addMenu(tr("Change History"));
    QAction *chEnableAct = changeHistoryMenu->addAction(tr("Enable Change History"));
    chEnableAct->setCheckable(true);
    connect(chEnableAct, &QAction::toggled, this, [this](bool on) {
        if (auto *e = currentEditor()) e->setChangeHistoryEnabled(on);
    });
    changeHistoryMenu->addAction(tr("Next Change"), this, [this] {
        if (auto *e = currentEditor()) e->goToNextChange();
    });
    changeHistoryMenu->addAction(tr("Previous Change"), this, [this] {
        if (auto *e = currentEditor()) e->goToPrevChange();
    });

    // 虛擬空間（FR-060）
    QAction *virtualSpaceAct = editMenu->addAction(tr("Virtual Space"));
    virtualSpaceAct->setCheckable(true);
    connect(virtualSpaceAct, &QAction::toggled, this, [this](bool on) {
        if (auto *e = currentEditor()) e->setVirtualSpace(on);
    });

    // 多重選取（FR-060）
    QMenu *multiSelectMenu = editMenu->addMenu(tr("Multi-Select"));
    multiSelectMenu->addAction(tr("Select Next Occurrence"), this, [this] {
        if (auto *e = currentEditor()) e->selectNextOccurrence();
    });
    multiSelectMenu->addAction(tr("Skip and Select Next"), this, [this] {
        if (auto *e = currentEditor()) e->skipAndSelectNext();
    });
    multiSelectMenu->addAction(tr("Undo Last Selection"), this, [this] {
        if (auto *e = currentEditor()) e->undoLastMultiSelect();
    });
    multiSelectMenu->addSeparator();
    // Select All Occurrences 四種變體（大小寫 × 全字）
    QMenu *selAllMenu = multiSelectMenu->addMenu(tr("Select All Occurrences"));
    selAllMenu->addAction(tr("Match Case"), this, [this] {
        if (auto *e = currentEditor()) e->selectAllOccurrences(true, false);
    });
    selAllMenu->addAction(tr("Ignore Case"), this, [this] {
        if (auto *e = currentEditor()) e->selectAllOccurrences(false, false);
    });
    selAllMenu->addAction(tr("Match Case + Whole Word"), this, [this] {
        if (auto *e = currentEditor()) e->selectAllOccurrences(true, true);
    });
    selAllMenu->addAction(tr("Ignore Case + Whole Word"), this, [this] {
        if (auto *e = currentEditor()) e->selectAllOccurrences(false, true);
    });

    // On Selection（把選取內容當路徑/搜尋字串處理）
    QMenu *onSelectionMenu = editMenu->addMenu(tr("On Selection"));
    // Open Selected File / Search on Internet 與編輯區右鍵選單共用同一實作（見 MainWindow_View.cpp）
    onSelectionMenu->addAction(tr("Open Selected File"), this,
                               [this] { openSelectionAsPathOrUrl(currentEditor()); });
    onSelectionMenu->addAction(tr("Open Containing Folder"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || !e->hasSelectedText())
            return;
        const QString sel = e->selectedText().trimmed();
        if (sel.isEmpty())
            return;
        macpad::platform::revealInFileManager(sel);
    });
    onSelectionMenu->addAction(tr("Search on Internet"), this,
                               [this] { searchSelectionOnInternet(currentEditor()); });

    // Paste Special（貼上時去除格式，只留純文字）
    QMenu *pasteSpecialMenu = editMenu->addMenu(tr("Paste Special"));
    pasteSpecialMenu->addAction(tr("Paste as Plain Text"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || e->isReadOnly())
            return;
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty())
            e->insert(text);
    });
    // 貼上剪貼簿 HTML／RTF 內容並去除標記，插入為純文字（EditorWidget 內建）
    pasteSpecialMenu->addAction(tr("Paste HTML Content"), this, [this] {
        if (EditorWidget *e = currentEditor(); e && !e->isReadOnly()) e->pasteAsHtml();
    });
    pasteSpecialMenu->addAction(tr("Paste RTF Content"), this, [this] {
        if (EditorWidget *e = currentEditor(); e && !e->isReadOnly()) e->pasteAsRtf();
    });
}


void MainWindow::createEncodingMenu(QMenu *formatMenu)
{
    // Encoding 選單：編碼與 EOL 轉換（FR-019/020）
    using macpad::core::Encoding;
    using macpad::core::Eol;
    QMenu *encMenu = formatMenu->addMenu(tr("Encoding"));
    const struct { const char *name; Encoding enc; } encs[] = {
        {"UTF-8", Encoding::Utf8}, {"UTF-8 with BOM", Encoding::Utf8Bom},
        {"UTF-16 LE", Encoding::Utf16LE}, {"UTF-16 BE", Encoding::Utf16BE},
        {"ANSI (Latin-1)", Encoding::Latin1},
    };
    for (const auto &e : encs) {
        const Encoding enc = e.enc;
        // 以字面量 tr() 標示名稱，lupdate 才能靜態擷取翻譯（不可傳執行期 const char*）
        QString label;
        switch (enc) {
        case Encoding::Utf8:    label = tr("UTF-8"); break;
        case Encoding::Utf8Bom: label = tr("UTF-8 with BOM"); break;
        case Encoding::Utf16LE: label = tr("UTF-16 LE"); break;
        case Encoding::Utf16BE: label = tr("UTF-16 BE"); break;
        case Encoding::Latin1:  label = tr("ANSI (Latin-1)"); break;
        }
        encMenu->addAction(label, this, [this, enc] {
            if (auto *ed = currentEditor()) ed->setEncoding(enc);
        });
    }
    // Character sets（傳統/區域編碼，複刻 Notepad++ Encoding ▸ Character sets）
    // 以所選 codec 重新解讀目前檔案（開亂碼的 Big5/GBK/Shift-JIS… 檔可正確顯示）
    QMenu *charsetMenu = encMenu->addMenu(tr("Character sets"));
    for (const auto &grp : macpad::core::FileEncoding::characterSets()) {
        QMenu *regionMenu = charsetMenu->addMenu(grp.region);
        for (const auto &cs : grp.items) {
            const QString codec = cs.codec;
            regionMenu->addAction(cs.display, this, [this, codec] {
                EditorWidget *e = currentEditor();
                if (!e) return;
                QString err;
                if (!e->reinterpretWithCodec(codec, &err)) {
                    QMessageBox::warning(this, tr("Character sets"), err);
                    return;
                }
                updateStatusBar();
                statusBar()->showMessage(tr("已以 %1 重新解讀").arg(codec), 3000);
            });
        }
    }

    // Reinterpret（不重新編碼，僅以指定編碼重讀磁碟內容——FR-048）
    QMenu *reinterpretMenu = encMenu->addMenu(tr("Reinterpret as"));
    const struct { const char *label; Encoding enc; } reinterprets[] = {
        {"UTF-8", Encoding::Utf8}, {"UTF-16 LE", Encoding::Utf16LE}, {"UTF-16 BE", Encoding::Utf16BE},
    };
    for (const auto &r : reinterprets) {
        const Encoding enc = r.enc;
        QString label;
        switch (enc) {
        case Encoding::Utf8:    label = tr("Reinterpret as UTF-8"); break;
        case Encoding::Utf16LE: label = tr("Reinterpret as UTF-16 LE"); break;
        case Encoding::Utf16BE: label = tr("Reinterpret as UTF-16 BE"); break;
        default: break;
        }
        reinterpretMenu->addAction(label, this, [this, enc] {
            EditorWidget *e = currentEditor();
            if (!e) return;
            QString err;
            if (!e->reinterpretAsEncoding(enc, &err)) {
                QMessageBox::warning(this, tr("Reinterpret as"), err);
                return;
            }
            updateStatusBar();
            statusBar()->showMessage(tr("已重新解讀"), 3000);
        });
    }

    QMenu *eolMenu = formatMenu->addMenu(tr("End of Line"));
    const struct { const char *name; Eol eol; } eols[] = {
        {"Unix (LF)", Eol::Lf}, {"Windows (CRLF)", Eol::CrLf}, {"Classic Mac (CR)", Eol::Cr},
    };
    for (const auto &e : eols) {
        const Eol eol = e.eol;
        // 以字面量 tr() 標示名稱，lupdate 才能靜態擷取翻譯（不可傳執行期 const char*）
        QString label;
        switch (eol) {
        case Eol::Lf:   label = tr("Unix (LF)"); break;
        case Eol::CrLf: label = tr("Windows (CRLF)"); break;
        case Eol::Cr:   label = tr("Classic Mac (CR)"); break;
        }
        eolMenu->addAction(label, this, [this, eol] {
            if (auto *ed = currentEditor()) ed->convertEol(eol);
        });
    }
}


void MainWindow::createPluginsMenu(QMenu *pluginMenu)
{
    // Plugins 選單（複刻 Notepad++ Plugins；Plugins Admin 列出內建擴充）
    pluginMenu->addAction(tr("Plugins Admin…"), this, [this] {
        QString msg;
        if (m_extensions) {
            for (const auto &c : m_extensions->capabilitiesList())
                msg += QStringLiteral("• %1  (%2)  v%3\n").arg(c.name, c.id, c.version);
        }
        if (msg.isEmpty())
            msg = tr("（無已載入擴充）");
        // 誠實揭露：Notepad++ 的 .dll 外掛是 Windows 專屬二進位，macOS 無法載入。
        msg += tr("\n\nmacpad++ 以內建 extension protocol 取代外掛。\n"
                  "註：Notepad++ 的 .dll 外掛為 Windows 專屬，macOS 無法載入。");
        QMessageBox::information(this, tr("Plugins Admin"), msg);
    });
}


void MainWindow::createToolsMenu(QMenu *toolsMenu)
{
    // Tools 選單：雜湊
    const struct { const char *name; QCryptographicHash::Algorithm alg; } hashes[] = {
        {"MD5", QCryptographicHash::Md5},
        {"SHA-1", QCryptographicHash::Sha1},
        {"SHA-256", QCryptographicHash::Sha256},
        {"SHA-512", QCryptographicHash::Sha512},
    };
    for (const auto &h : hashes) {
        const auto alg = h.alg;
        const QString label = QString::fromLatin1(h.name);
        toolsMenu->addAction(tr("%1 of selection/document").arg(label), this, [this, alg, label] {
            EditorWidget *e = currentEditor();
            if (!e) return;
            const QString src = e->hasSelectedText() ? e->selectedText() : e->text();
            const QString digest = QString::fromLatin1(
                QCryptographicHash::hash(src.toUtf8(), alg).toHex());
            QMessageBox::information(this, label, digest);
        });
    }
}


void MainWindow::createSettingsMenu(QMenu *settingsMenu)
{
    // Settings 選單（複刻 Notepad++ Settings）：Style Configurator + Shortcut Mapper
    settingsMenu->addAction(tr("Style Configurator…"), this, [this] {
        macpad::ui::StyleConfiguratorDialog dlg(this);
        // 對話框內「Apply Theme」→ 立即套用具名主題到所有開啟編輯器（FR-056）
        connect(&dlg, &macpad::ui::StyleConfiguratorDialog::themeSelected, this,
                [this](const QString &name) {
            if (name.isEmpty())
                return;
            forEachEditor([&name](EditorWidget *e) {
                if (e)
                    macpad::platform::ThemeManager::applyNamedTheme(e, name);
            });
            statusBar()->showMessage(tr("主題已套用：%1").arg(name), 3000);
        });
        if (dlg.exec() == QDialog::Accepted) {
            applyTheme();  // 重建 lexer → 套主題 → 疊使用者覆寫
            statusBar()->showMessage(tr("樣式已更新"), 3000);
        }
    });
    // 具名主題選擇（FR-056）：套用到目前所有已開啟的分頁
    settingsMenu->addAction(tr("Select Theme…"), this, [this] {
        macpad::ui::ThemePickerDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const QString name = dlg.selectedTheme();
        if (name.isEmpty())
            return;
        forEachEditor([&name](EditorWidget *e) {
            if (e)
                macpad::platform::ThemeManager::applyNamedTheme(e, name);
        });
        statusBar()->showMessage(tr("主題已套用：%1").arg(name), 3000);
    });
    settingsMenu->addAction(tr("Shortcut Mapper…"), this, [this] {
        // 蒐集所有具文字的可指定快捷鍵命令（排除子選單標題與分隔線）
        QList<QAction *> acts;
        for (QAction *a : menuBar()->findChildren<QAction *>()) {
            if (a->menu() || a->isSeparator() || a->text().isEmpty())
                continue;
            acts.append(a);
        }
        macpad::ui::ShortcutMapperDialog dlg(acts, this);
        dlg.exec();
    });

    // 介面語言(i18n)——切換後重啟套用。語言名(endonym)固定不翻譯。
    settingsMenu->addSeparator();
    QMenu *uiLangMenu = settingsMenu->addMenu(tr("Interface Language"));
    const struct { const char *label; const char *code; bool endonym; } uiLangs[] = {
        {"System Default", "", false},
        {"繁體中文", "zh_TW", true},
        {"English", "en", true},
        {"日本語", "ja", true},
        {"简体中文", "zh_CN", true},
    };
    const QString curLang = macpad::persistence::SettingsStore::load().language;
    for (const auto &l : uiLangs) {
        const QString code = QString::fromLatin1(l.code);
        const QString label = l.endonym ? QString::fromUtf8(l.label) : tr("System Default");
        QAction *act = uiLangMenu->addAction(label, this, [this, code] {
            auto s = macpad::persistence::SettingsStore::load();
            s.language = code;
            macpad::persistence::SettingsStore::save(s);
            QMessageBox::information(this, tr("Interface Language"),
                tr("Language changed. Restart macpad++ to apply."));
        });
        act->setCheckable(true);
        act->setChecked(code == curLang);
    }
}


void MainWindow::createLanguageMenu(QMenu *langMenu)
{
    // Language 選單：手動選語法高亮（FR-003）+ UDL（FR-032）

    // 手動選擇語言：設定目前編輯器的 lexer（覆寫副檔名自動判斷）
    // 偏好 disabledLanguages 中的語言（以顯示名或內部鍵比對）不列入（Notepad++ Language 選單隱藏對等）
    QMenu *setLangMenu = langMenu->addMenu(tr("Set Language"));
    const QStringList disabledLangs = macpad::persistence::SettingsStore::load().disabledLanguages;
    for (const auto &lang : macpad::core::LexerFactory::languages()) {
        if (disabledLangs.contains(lang.key, Qt::CaseInsensitive)
            || disabledLangs.contains(lang.displayName, Qt::CaseInsensitive))
            continue;
        const QString key = lang.key;
        setLangMenu->addAction(lang.displayName, this, [this, key] {
            if (EditorWidget *e = currentEditor())
                e->setLanguageLexer(macpad::core::LexerFactory::createForLanguage(key, e));
        });
    }
    langMenu->addSeparator();

    // User-Defined Language ▸ Define your language（圖形化建立 UDL）
    QMenu *udlMenu = langMenu->addMenu(tr("User-Defined Language"));
    udlMenu->addAction(tr("Define Your Language…"), this, [this] {
        macpad::ui::UdlEditorDialog dlg(&m_udl, this);
        if (dlg.exec() == QDialog::Accepted) {
            statusBar()->showMessage(tr("UDL 已建立（開啟對應副檔名檔案即套用）"), 4000);
            if (EditorWidget *e = currentEditor(); e && !e->isUntitled()) {
                if (const auto *def = m_udl.findForExtension(QFileInfo(e->filePath()).suffix()))
                    e->setLexer(new macpad::features::UdlLexer(*def, e));
            }
        }
    });
    udlMenu->addAction(tr("Import UDL…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import UDL"),
                                                          QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        if (m_udl.importFromFile(path)) {
            statusBar()->showMessage(tr("UDL 已匯入（開啟對應副檔名檔案即套用）"), 4000);
            // 立即套用到目前檔案（若副檔名匹配）
            if (EditorWidget *e = currentEditor(); e && !e->isUntitled()) {
                if (const auto *def = m_udl.findForExtension(QFileInfo(e->filePath()).suffix()))
                    e->setLexer(new macpad::features::UdlLexer(*def, e));
            }
        } else {
            QMessageBox::warning(this, tr("Import UDL"), tr("UDL 檔案無效"));
        }
    });
    // 直接匯入 Notepad++ userDefineLang.xml（不開啟編輯器；UdlManager::importFromXml）
    udlMenu->addAction(tr("Import Notepad++ UDL (XML)…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Notepad++ UDL"),
                                                          QString(), tr("XML (*.xml)"));
        if (path.isEmpty())
            return;
        if (m_udl.importFromXml(path)) {
            statusBar()->showMessage(tr("已匯入 Notepad++ UDL（開啟對應副檔名檔案即套用）"), 4000);
            if (EditorWidget *e = currentEditor(); e && !e->isUntitled()) {
                if (const auto *def = m_udl.findForExtension(QFileInfo(e->filePath()).suffix()))
                    e->setLexer(new macpad::features::UdlLexer(*def, e));
            }
        } else {
            QMessageBox::warning(this, tr("Import Notepad++ UDL"),
                                 tr("XML 檔案無效或無法解析"));
        }
    });
}


void MainWindow::createRunMenu(QMenu *runMenu)
{
    // Run 選單（FR-031）
    runMenu->addAction(tr("Run…"), QKeySequence(Qt::Key_F5), this, [this] {
        if (!m_runDock) {
            m_runDock = new macpad::features::RunDock(this);
            addDockWidget(Qt::BottomDockWidgetArea, m_runDock);
        }
        // 填入目前檔案的變數上下文（FR-031）
        macpad::features::RunVars vars;
        if (EditorWidget *e = currentEditor(); e && !e->isUntitled()) {
            const QFileInfo fi(e->filePath());
            vars.fullCurrentPath = fi.absoluteFilePath();
            vars.currentDirectory = fi.absolutePath();
            vars.fileName = fi.fileName();
            vars.nameNoExt = fi.completeBaseName();
            if (e->hasSelectedText()) {
                vars.currentWord = e->selectedText();
            } else {
                // 無選取時取游標實際所在位置的單字（非固定 0,0）
                int line = 0, col = 0;
                e->getCursorPosition(&line, &col);
                vars.currentWord = e->wordAtLineIndex(line, col);
            }
        }
        m_runDock->setVars(vars);
        m_runDock->show();
        m_runDock->raise();
    });
    runMenu->addSeparator();
    runMenu->addAction(tr("Save Current Command…"), this, [this] {
        bool ok = false;
        const QString cmd = QInputDialog::getText(this, tr("Save Command"),
            tr("Command (可用 $(FULL_CURRENT_PATH) 等變數):"), QLineEdit::Normal,
            QString(), &ok);
        if (!ok || cmd.isEmpty())
            return;
        const QString name = QInputDialog::getText(this, tr("Save Command"),
            tr("Name:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty())
            return;
        // 選填快捷鍵（FR-031 延伸）：以 QKeySequenceEdit 讓使用者指定
        QDialog scDlg(this);
        scDlg.setWindowTitle(tr("Shortcut (選填)"));
        auto *scLayout = new QVBoxLayout(&scDlg);
        scLayout->addWidget(new QLabel(tr("為此指令指定快捷鍵（可留空）:"), &scDlg));
        auto *scEdit = new QKeySequenceEdit(&scDlg);
        scLayout->addWidget(scEdit);
        auto *scBtns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &scDlg);
        scLayout->addWidget(scBtns);
        connect(scBtns, &QDialogButtonBox::accepted, &scDlg, &QDialog::accept);
        connect(scBtns, &QDialogButtonBox::rejected, &scDlg, &QDialog::reject);
        QString shortcut;
        if (scDlg.exec() == QDialog::Accepted)
            shortcut = scEdit->keySequence().toString();

        QList<macpad::features::RunCommandEntry> entries =
            macpad::features::RunCommandStore::load();
        // 同名則覆寫
        bool replaced = false;
        for (auto &e : entries) {
            if (e.name == name) { e.command = cmd; e.shortcut = shortcut; replaced = true; break; }
        }
        if (!replaced)
            entries.append({name, cmd, shortcut});
        macpad::features::RunCommandStore::save(entries);
        rebuildRunShortcuts();  // 立即註冊/更新快捷鍵
        statusBar()->showMessage(tr("命令已儲存：%1").arg(name), 3000);
    });
    QMenu *savedRunMenu = runMenu->addMenu(tr("Saved Commands"));
    connect(savedRunMenu, &QMenu::aboutToShow, this, [this, savedRunMenu] {
        savedRunMenu->clear();
        const QList<macpad::features::RunCommandEntry> entries =
            macpad::features::RunCommandStore::load();
        if (entries.isEmpty()) {
            savedRunMenu->addAction(tr("(無已儲存命令)"))->setEnabled(false);
            return;
        }
        for (const auto &entry : entries) {
            const QString cmd = entry.command;
            QAction *a = savedRunMenu->addAction(entry.name, this,
                                                 [this, cmd] { runSavedCommand(cmd); });
            if (!entry.shortcut.isEmpty())
                a->setShortcut(QKeySequence(entry.shortcut));  // 選單顯示快捷鍵（實際綁定於 rebuildRunShortcuts）
        }
    });
}


void MainWindow::createMacroMenu(QMenu *macroMenu)
{
    // Macro 選單（FR-028）
    m_recordAction = macroMenu->addAction(tr("Start Recording"),
                                          this, &MainWindow::startMacroRecording);
    m_stopAction = macroMenu->addAction(tr("Stop Recording"),
                                        this, &MainWindow::stopMacroRecording);
    m_playAction = macroMenu->addAction(tr("Playback"),
                                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
                                        this, &MainWindow::playMacro);
    m_stopAction->setEnabled(false);
    m_playAction->setEnabled(false);
    macroMenu->addSeparator();
    // Macro Manager（複刻 Notepad++ Modify Shortcut / Delete Macro）
    macroMenu->addAction(tr("Macro Manager…"), this, [this] { openMacroManager(); });
    macroMenu->addAction(tr("Run a Macro Multiple Times…"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || m_savedMacro.isEmpty()) {
            statusBar()->showMessage(tr("尚無已錄製的巨集"), 2000);
            return;
        }
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Run a Macro Multiple Times"),
                                           tr("Times:"), 1, 1, 100000, 1, &ok);
        if (!ok)
            return;
        e->beginUndoAction();
        for (int i = 0; i < n; ++i) {
            QsciMacro macro(e);
            macro.load(m_savedMacro);
            macro.play();
        }
        e->endUndoAction();
    });
    macroMenu->addAction(tr("Save Current Recorded Macro…"), this, [this] {
        if (m_savedMacro.isEmpty()) {
            statusBar()->showMessage(tr("尚無已錄製的巨集"), 2000);
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Macro"), tr("Name:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty())
            return;
        const QString path = macpad::persistence::AppPaths::filePath(
            QStringLiteral("macros.json"));
        QJsonObject o = macpad::persistence::JsonFile::load(path);
        o.insert(name, m_savedMacro);
        macpad::persistence::JsonFile::save(path, o);
        statusBar()->showMessage(tr("巨集已儲存：%1").arg(name), 3000);
    });
    QMenu *savedMacroMenu = macroMenu->addMenu(tr("Saved Macros"));
    connect(savedMacroMenu, &QMenu::aboutToShow, this, [this, savedMacroMenu] {
        savedMacroMenu->clear();
        const QString path = macpad::persistence::AppPaths::filePath(
            QStringLiteral("macros.json"));
        const QJsonObject o = macpad::persistence::JsonFile::load(path);
        if (o.isEmpty()) {
            savedMacroMenu->addAction(tr("(無已儲存巨集)"))->setEnabled(false);
            return;
        }
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString macroStr = it.value().toString();
            savedMacroMenu->addAction(it.key(), this, [this, macroStr] {
                EditorWidget *e = currentEditor();
                if (!e) return;
                QsciMacro macro(e);
                macro.load(macroStr);
                macro.play();
            });
        }
    });
}


void MainWindow::createViewMenu(QMenu *viewMenu)
{
    // View 選單：Zoom（FR-023）+ 全螢幕（FR-023）
    viewMenu->addAction(tr("Zoom In"), QKeySequence::ZoomIn, this, [this] {
        if (auto *ed = currentEditor()) ed->zoomIn();
    });
    viewMenu->addAction(tr("Zoom Out"), QKeySequence::ZoomOut, this, [this] {
        if (auto *ed = currentEditor()) ed->zoomOut();
    });
    viewMenu->addAction(tr("Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0), this, [this] {
        if (auto *ed = currentEditor()) ed->zoomTo(0);
    });
    if (m_toolbar) {
        QAction *tbAct = m_toolbar->toggleViewAction();
        tbAct->setText(tr("Show Toolbar"));
        viewMenu->addAction(tbAct);
    }
    viewMenu->addSeparator();
    QAction *wrapAct = viewMenu->addAction(tr("Word Wrap"));
    m_wrapAct = wrapAct;  // 供工具列同名按鈕切換
    wrapAct->setCheckable(true);
    connect(wrapAct, &QAction::toggled, this, [this](bool on) {
        m_wordWrap = on;
        forEachEditor([this](EditorWidget *e) { applyViewPrefs(e); });
    });
    QAction *wsAct = viewMenu->addAction(tr("Show Whitespace"));
    m_wsAct = wsAct;  // 供工具列 Show All Characters 按鈕切換
    wsAct->setCheckable(true);
    connect(wsAct, &QAction::toggled, this, [this](bool on) {
        m_showWhitespace = on;
        forEachEditor([this](EditorWidget *e) { applyViewPrefs(e); });
    });
    QAction *eolAct = viewMenu->addAction(tr("Show End of Line"));
    m_eolAct = eolAct;  // 供工具列 Show All Characters 按鈕切換
    eolAct->setCheckable(true);
    connect(eolAct, &QAction::toggled, this, [this](bool on) {
        m_showEol = on;
        forEachEditor([this](EditorWidget *e) { applyViewPrefs(e); });
    });
    QAction *igAct = viewMenu->addAction(tr("Show Indent Guide"));
    igAct->setCheckable(true);
    igAct->setChecked(true);
    connect(igAct, &QAction::toggled, this, [this](bool on) {
        m_showIndentGuide = on;
        forEachEditor([this](EditorWidget *e) { applyViewPrefs(e); });
    });
    QAction *wrapSymAct = viewMenu->addAction(tr("Show Wrap Symbol"));
    wrapSymAct->setCheckable(true);
    connect(wrapSymAct, &QAction::toggled, this, [this](bool on) {
        m_showWrapSymbol = on;
        forEachEditor([this](EditorWidget *e) { applyViewPrefs(e); });
    });
    viewMenu->addAction(tr("Show All Characters"), this, [wsAct, eolAct] {
        // 一鍵開啟空白＋EOL 顯示
        wsAct->setChecked(true);
        eolAct->setChecked(true);
    });

    // Smart Highlighting（游標所在字詞自動標記全部出現處）——套用到所有開啟編輯器；
    // 新分頁於 addEditorTab 依此動作的勾選狀態同步。
    QAction *shAct = viewMenu->addAction(tr("Smart Highlighting"));
    shAct->setCheckable(true);
    m_smartHighlightAct = shAct;
    connect(shAct, &QAction::toggled, this, [this](bool on) {
        forEachEditor([on](EditorWidget *e) { if (e) e->setSmartHighlight(on); });
    });

    // Fold（折疊）子選單
    QMenu *foldMenu = viewMenu->addMenu(tr("Fold"));
    foldMenu->addAction(tr("Fold All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F),
                        this, [this] { if (auto *e = currentEditor()) e->foldAllBlocks(true); });
    foldMenu->addAction(tr("Unfold All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F),
                        this, [this] { if (auto *e = currentEditor()) e->foldAllBlocks(false); });
    foldMenu->addAction(tr("Collapse Current"), this,
                        [this] { if (auto *e = currentEditor()) e->foldCurrent(true); });
    foldMenu->addAction(tr("Expand Current"), this,
                        [this] { if (auto *e = currentEditor()) e->foldCurrent(false); });
    foldMenu->addSeparator();
    for (int lvl = 1; lvl <= 8; ++lvl)
        foldMenu->addAction(tr("Fold Level %1").arg(lvl), this,
                            [this, lvl] { if (auto *e = currentEditor()) e->foldToLevel(lvl); });

    // Tab（分頁）導覽子選單
    QMenu *tabMenu = viewMenu->addMenu(tr("Tab"));
    tabMenu->addAction(tr("Next Tab"), QKeySequence(Qt::CTRL | Qt::Key_BraceRight),
                       this, [this] { activateTabRelative(1); });
    tabMenu->addAction(tr("Previous Tab"), QKeySequence(Qt::CTRL | Qt::Key_BraceLeft),
                       this, [this] { activateTabRelative(-1); });
    tabMenu->addAction(tr("First Tab"), this, [this] {
        QTabWidget *w = currentTabWidget();
        if (w->count()) w->setCurrentIndex(0);
    });
    tabMenu->addAction(tr("Last Tab"), this, [this] {
        QTabWidget *w = currentTabWidget();
        if (w->count()) w->setCurrentIndex(w->count() - 1);
    });
    tabMenu->addSeparator();
    tabMenu->addAction(tr("Move Tab Forward"), this, [this] { moveCurrentTab(1); });
    tabMenu->addAction(tr("Move Tab Backward"), this, [this] { moveCurrentTab(-1); });

    viewMenu->addSeparator();
    QAction *aotAct = viewMenu->addAction(tr("Always on Top"));
    aotAct->setCheckable(true);
    connect(aotAct, &QAction::toggled, this, &MainWindow::toggleAlwaysOnTop);
    viewMenu->addAction(tr("Monitoring (tail -f)"), this, [this] { toggleMonitoring(); });
    if (m_charPanel) {
        QAction *cpAct = m_charPanel->toggleViewAction();
        cpAct->setText(tr("Character Panel"));
        viewMenu->addAction(cpAct);
    }
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Document Summary…"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e) return;
        const QString text = e->text();
        const int chars = int(text.size());
        static const QRegularExpression ws(QStringLiteral("\\s+"));
        const int words = text.split(ws, Qt::SkipEmptyParts).size();
        const int lines = e->lines();
        const int selChars = e->hasSelectedText() ? int(e->selectedText().size()) : 0;
        QMessageBox::information(this, tr("Document Summary"),
            tr("字元數：%1\n單字數：%2\n行數：%3\n選取字元：%4")
                .arg(chars).arg(words).arg(lines).arg(selChars));
    });
    viewMenu->addSeparator();
    // Dual-View：把目前分頁移到 / 複製到另一個檢視容器（複刻 Notepad++ Move/Clone to Other View）
    QMenu *moveCloneMenu = viewMenu->addMenu(tr("Move/Clone Current Document"));
    moveCloneMenu->addAction(tr("Move to Other View"), this, &MainWindow::moveToOtherView);
    moveCloneMenu->addAction(tr("Clone to Other View"), this, &MainWindow::cloneToOtherView);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Toggle Split"), QKeySequence(Qt::CTRL | Qt::Key_Backslash),
                        this, &MainWindow::toggleSplit);
    // 旋轉分割方向（水平 ↔ 垂直）——複刻 Notepad++ Rotate to left/right
    viewMenu->addAction(tr("Rotate Split Orientation"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R), this, [this] {
        if (m_viewSplit)
            m_viewSplit->setOrientation(
                m_viewSplit->orientation() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
    });
    QAction *syncVAct = viewMenu->addAction(tr("Synchronize Vertical Scrolling"));
    syncVAct->setCheckable(true);
    connect(syncVAct, &QAction::toggled, this, [this](bool on) {
        if (EditorPane *p = currentPane()) p->setSyncVerticalScroll(on);
    });
    QAction *syncHAct = viewMenu->addAction(tr("Synchronize Horizontal Scrolling"));
    syncHAct->setCheckable(true);
    connect(syncHAct, &QAction::toggled, this, [this](bool on) {
        if (EditorPane *p = currentPane()) p->setSyncHorizontalScroll(on);
    });
    if (m_docList) {
        QAction *docAct = m_docList->toggleViewAction();
        docAct->setText(tr("Document List"));
        viewMenu->addAction(docAct);
    }
    if (m_projectPanel) {
        QAction *ppAct = m_projectPanel->toggleViewAction();
        ppAct->setText(tr("Project Panel"));
        viewMenu->addAction(ppAct);
    }
    // 停靠面板切換（FR-029）；顯示時即時刷新
    for (QDockWidget *dock : {static_cast<QDockWidget *>(m_funcList),
                              static_cast<QDockWidget *>(m_clipHistory),
                              static_cast<QDockWidget *>(m_docMap)}) {
        if (!dock)
            continue;
        viewMenu->addAction(dock->toggleViewAction());
        connect(dock, &QDockWidget::visibilityChanged, this, [this](bool vis) {
            if (vis) refreshPanels();
        });
    }
    viewMenu->addAction(tr("Toggle Full Screen"),
                        QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F), this, [this] {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });
    QAction *dfAct = viewMenu->addAction(tr("Distraction Free Mode"),
                                         QKeySequence(Qt::Key_F11));
    dfAct->setCheckable(true);
    connect(dfAct, &QAction::toggled, this, &MainWindow::setDistractionFree);
    QAction *postItAct = viewMenu->addAction(tr("Post-It Mode"), QKeySequence(Qt::Key_F12));
    postItAct->setCheckable(true);
    connect(postItAct, &QAction::toggled, this, &MainWindow::setPostIt);

    // View Current File In（在瀏覽器開啟目前檔案）
    QMenu *browserMenu = viewMenu->addMenu(tr("View Current File In"));
    browserMenu->addAction(tr("Default Browser"), this,
                           [this] { viewCurrentFileInBrowser(QString()); });
    browserMenu->addAction(tr("Safari"), this,
                           [this] { viewCurrentFileInBrowser(QStringLiteral("Safari")); });
    browserMenu->addAction(tr("Google Chrome"), this,
                           [this] { viewCurrentFileInBrowser(QStringLiteral("Google Chrome")); });
    browserMenu->addAction(tr("Firefox"), this,
                           [this] { viewCurrentFileInBrowser(QStringLiteral("Firefox")); });
}


void MainWindow::createEditMenuOps(QMenu *editMenu)
{
    using macpad::features::TextOps;

    // 大小寫轉換
    QMenu *caseMenu = editMenu->addMenu(tr("Convert Case"));
    caseMenu->addAction(tr("UPPERCASE"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U),
                        this, [this] { applyTextOp(&TextOps::toUpper); });
    caseMenu->addAction(tr("lowercase"), QKeySequence(Qt::CTRL | Qt::Key_U),
                        this, [this] { applyTextOp(&TextOps::toLower); });
    caseMenu->addAction(tr("Title Case"), this, [this] { applyTextOp(&TextOps::toTitleCase); });
    caseMenu->addAction(tr("Sentence case"), this, [this] { applyTextOp(&TextOps::toSentenceCase); });
    // Blend 變體：僅將首字母轉大寫，其餘字母大小寫保留原狀（Notepad++ Proper/Sentence case blend 對等）
    caseMenu->addAction(tr("Proper Case (blend)"), this,
                        [this] { applyTextOp(&TextOps::properCaseBlend); });
    caseMenu->addAction(tr("Sentence case (blend)"), this,
                        [this] { applyTextOp(&TextOps::sentenceCaseBlend); });
    caseMenu->addAction(tr("iNVERT cASE"), this, [this] { applyTextOp(&TextOps::invertCase); });
    caseMenu->addAction(tr("rAnDoM CaSe"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::toRandomCase(s); }); });

    // 行操作
    QMenu *lineMenu = editMenu->addMenu(tr("Line Operations"));
    lineMenu->addAction(tr("Sort Ascending"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesAscending(s); }); });
    lineMenu->addAction(tr("Sort Descending"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesDescending(s); }); });
    lineMenu->addAction(tr("Sort Numeric"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesNumeric(s); }); });
    lineMenu->addAction(tr("Sort Lines by Length (Ascending)"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesByLength(s, true); }); });
    lineMenu->addAction(tr("Sort Lines by Length (Descending)"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesByLength(s, false); }); });
    lineMenu->addAction(tr("Sort Lines as Decimals"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesAsDecimals(s, true, false); }); });
    lineMenu->addAction(tr("Sort Lines (Locale)"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesLocale(s, true, true); }); });
    lineMenu->addAction(tr("Shuffle Lines"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::shuffleLines(s); }); });
    lineMenu->addAction(tr("Remove Duplicate Lines"), this,
                        [this] { applyTextOp(&TextOps::removeDuplicateLines); });
    lineMenu->addAction(tr("Remove Consecutive Duplicate Lines"), this,
                        [this] { applyTextOp(&TextOps::removeConsecutiveDuplicateLines); });
    lineMenu->addAction(tr("Remove Empty Lines"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::removeEmptyLines(s, /*blankMeansWhitespace=*/false); }); });
    lineMenu->addAction(tr("Remove Empty Lines (Containing Blank Chars)"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::removeEmptyLines(s, /*blankMeansWhitespace=*/true); }); });
    lineMenu->addAction(tr("Reverse Line Order"), this,
                        [this] { applyTextOp(&TextOps::reverseLines); });
    lineMenu->addAction(tr("Duplicate Lines"), this,
                        [this] { applyTextOp(&TextOps::duplicateLines); });
    lineMenu->addAction(tr("Join Lines"), this,
                        [this] { if (auto *e = currentEditor()) e->joinSelectedLines(); });
    lineMenu->addAction(tr("Split Lines"), this,
                        [this] { if (auto *e = currentEditor()) e->splitSelectedLines(); });
    lineMenu->addSeparator();
    lineMenu->addAction(tr("Duplicate Current Line"), QKeySequence(Qt::CTRL | Qt::Key_D),
                        this, [this] {
        if (auto *e = currentEditor()) e->SendScintilla(QsciScintilla::SCI_LINEDUPLICATE);
    });
    lineMenu->addAction(tr("Delete Current Line"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
                        this, [this] {
        if (auto *e = currentEditor(); e && !e->isReadOnly())
            e->SendScintilla(QsciScintilla::SCI_LINEDELETE);
    });
    lineMenu->addSeparator();
    lineMenu->addAction(tr("Move Lines Up"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up),
                        this, [this] { moveCurrentLines(true); });
    lineMenu->addAction(tr("Move Lines Down"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down),
                        this, [this] { moveCurrentLines(false); });

    // 空白操作
    QMenu *blankMenu = editMenu->addMenu(tr("Blank Operations"));
    blankMenu->addAction(tr("Trim Trailing Whitespace"), this,
                         [this] { applyTextOp(&TextOps::trimTrailing); });
    blankMenu->addAction(tr("Trim Leading Whitespace"), this,
                         [this] { applyTextOp(&TextOps::trimLeading); });
    blankMenu->addAction(tr("TAB to Space"), this,
                         [this] { applyTextOp([](const QString &s){ return TextOps::tabsToSpaces(s, 4); }); });
    blankMenu->addAction(tr("Space to TAB"), this,
                         [this] { applyTextOp([](const QString &s){ return TextOps::spacesToTabs(s, 4); }); });
    blankMenu->addAction(tr("Trim Leading and Trailing Space"), this,
                         [this] { applyTextOp(&TextOps::trimBoth); });
    blankMenu->addAction(tr("EOL to Space"), this,
                         [this] { applyTextOp(&TextOps::eolToSpace); });
    blankMenu->addAction(tr("Trim Both and EOL to Space"), this,
                         [this] { applyTextOp(&TextOps::trimBothAndEolToSpace); });
    blankMenu->addAction(tr("Leading Spaces to TAB"), this, [this] {
        const int tabWidth = macpad::persistence::SettingsStore::load().tabWidth;
        applyTextOp([tabWidth](const QString &s){ return TextOps::spacesToTabsLeading(s, tabWidth); });
    });

    // 註解切換
    editMenu->addAction(tr("Toggle Line Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash),
                        this, [this] {
        EditorWidget *e = currentEditor();
        const QString marker = e && !e->isUntitled()
            ? lineCommentMarker(QFileInfo(e->filePath()).suffix())
            : QStringLiteral("//");
        applyTextOp([marker](const QString &s){ return TextOps::toggleLineComment(s, marker); });
    });

    // 欄位編輯器（插入遞增數列）
    editMenu->addAction(tr("Column Editor…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C),
                        this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || e->isReadOnly())
            return;
        int lineFrom = 0, idxFrom = 0, lineTo = 0, idxTo = 0;
        if (e->hasSelectedText())
            e->getSelection(&lineFrom, &idxFrom, &lineTo, &idxTo);
        else {
            e->getCursorPosition(&lineFrom, &idxFrom);
            lineTo = e->lines() - 1;  // 無選取 → 由游標行到檔尾
            idxTo = idxFrom;
        }
        macpad::ui::ColumnEditorDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const int col = qMin(idxFrom, idxTo);
        QString out;
        if (dlg.isTextMode()) {
            // Text 模式：於各行同欄插入固定文字
            out = macpad::features::ColumnEditor::insertTextColumn(
                e->text(), lineFrom, lineTo, col, dlg.textToInsert());
        } else {
            // Number 模式：遞增數列；repeat 表示每幾行才遞增一次
            macpad::features::NumberSeqSpec spec = dlg.spec();
            spec.repeat = dlg.repeatCount();
            out = macpad::features::ColumnEditor::insertNumberColumn(
                e->text(), lineFrom, lineTo, col, spec);
        }
        e->beginUndoAction();
        const QByteArray b = out.toUtf8();
        e->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, 0UL);
        e->SendScintilla(QsciScintilla::SCI_SETTARGETEND, static_cast<unsigned long>(e->length()));
        e->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                         static_cast<quintptr>(b.size()), b.constData());
        e->endUndoAction();
    });

    // 插入日期／時間（EditorWidget 內建三種格式；預設格式取自偏好 dateFormat/customDateFormat）
    QMenu *insertDateMenu = editMenu->addMenu(tr("Insert Date/Time"));
    insertDateMenu->addAction(tr("Date Time (Short Format)"), this, [this] {
        if (EditorWidget *e = currentEditor(); e && !e->isReadOnly()) e->insertDateShort();
    });
    insertDateMenu->addAction(tr("Date Time (Long Format)"), this, [this] {
        if (EditorWidget *e = currentEditor(); e && !e->isReadOnly()) e->insertDateLong();
    });
    insertDateMenu->addAction(tr("Date Time (Preference Format)"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || e->isReadOnly())
            return;
        const auto s = macpad::persistence::SettingsStore::load();
        // dateFormat == "custom" 時採用 customDateFormat，否則以 dateFormat 為格式字串
        const QString fmt = (s.dateFormat == QLatin1String("custom") && !s.customDateFormat.isEmpty())
                                ? s.customDateFormat : s.dateFormat;
        e->insertDateCustom(fmt.isEmpty() ? QStringLiteral("yyyy-MM-dd HH:mm:ss") : fmt);
    });
    insertDateMenu->addAction(tr("Date Time (Custom…)"), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e || e->isReadOnly())
            return;
        bool ok = false;
        const QString fmt = QInputDialog::getText(this, tr("Insert Date/Time"),
            tr("格式字串（如 yyyy-MM-dd HH:mm:ss）:"), QLineEdit::Normal,
            macpad::persistence::SettingsStore::load().dateFormat, &ok);
        if (ok && !fmt.isEmpty())
            e->insertDateCustom(fmt);
    });

    editMenu->addSeparator();

    // Indent（縮排）子選單
    QMenu *indentMenu = editMenu->addMenu(tr("Indent"));
    indentMenu->addAction(tr("Increase Line Indent"), this,
                          [this] { if (auto *e = currentEditor()) e->indentSelection(); });
    indentMenu->addAction(tr("Decrease Line Indent"), this,
                          [this] { if (auto *e = currentEditor()) e->unindentSelection(); });

    // Copy to Clipboard（複製檔案資訊）子選單
    QMenu *copyMenu = editMenu->addMenu(tr("Copy to Clipboard"));
    copyMenu->addAction(tr("Current Full File Path"), this, [this] {
        if (auto *e = currentEditor(); e && !e->isUntitled())
            QApplication::clipboard()->setText(e->filePath());
    });
    copyMenu->addAction(tr("Current Filename"), this, [this] {
        if (auto *e = currentEditor(); e && !e->isUntitled())
            QApplication::clipboard()->setText(QFileInfo(e->filePath()).fileName());
    });
    copyMenu->addAction(tr("Current Directory Path"), this, [this] {
        if (auto *e = currentEditor(); e && !e->isUntitled())
            QApplication::clipboard()->setText(QFileInfo(e->filePath()).absolutePath());
    });

    // 區塊註解（依語言 /* */、<!-- --> 等）
    editMenu->addAction(tr("Block Comment / Uncomment"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Slash), this,
                        [this] { if (auto *e = currentEditor()) e->toggleBlockComment(); });

    // 自動完成（強制觸發詞彙完成，忽略字數門檻）——Ctrl+Space，另綁 Ctrl+Return
    QAction *wordCompAct = editMenu->addAction(tr("Word Completion"),
                                               QKeySequence(Qt::CTRL | Qt::Key_Space), this,
                        [this] { if (auto *e = currentEditor()) e->triggerWordCompletion(); });
    wordCompAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Space),
                               QKeySequence(Qt::CTRL | Qt::Key_Return)});
    // 函式參數提示（Call Tip）——不依賴輸入 '('，由使用者明確觸發（Ctrl+Shift+Space）
    editMenu->addAction(tr("Show Call Tip"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space),
                        this, [this] { if (auto *e = currentEditor()) e->triggerCallTip(); });

    editMenu->addSeparator();

    // 唯讀切換
    QAction *roAct = editMenu->addAction(tr("Read-Only"));
    roAct->setCheckable(true);
    connect(roAct, &QAction::toggled, this, [this](bool on) {
        if (auto *e = currentEditor()) e->setReadOnly(on);
    });
    // 切換分頁時同步唯讀勾選狀態（兩個檢視都要監聽）
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        connect(w, &QTabWidget::currentChanged, this, [this, roAct] {
            if (auto *e = currentEditor()) {
                QSignalBlocker b(roAct);
                roAct->setChecked(e->isReadOnly());
            }
        });
    }

    // Character Panel（開啟/聚焦 ASCII 插入面板）
    editMenu->addAction(tr("Character Panel"), this, [this] {
        if (m_charPanel) { m_charPanel->show(); m_charPanel->raise(); }
    });

    editMenu->addSeparator();

    // 兩段式選取 / 遮蔽（Begin/End Select、欄位版本、Redact Selection）
    QMenu *selMenu = editMenu->addMenu(tr("Selection"));
    selMenu->addAction(tr("Begin Select"), this,
                       [this] { if (auto *e = currentEditor()) e->beginSelect(); });
    selMenu->addAction(tr("End Select"), this,
                       [this] { if (auto *e = currentEditor()) e->endSelect(); });
    selMenu->addAction(tr("Begin Column Select"), this,
                       [this] { if (auto *e = currentEditor()) e->beginColumnSelect(); });
    selMenu->addAction(tr("End Column Select"), this,
                       [this] { if (auto *e = currentEditor()) e->endColumnSelect(); });
    selMenu->addSeparator();
    selMenu->addAction(tr("Redact Selection"), this,
                       [this] { if (auto *e = currentEditor()) e->redactSelection(); });

    // 詞彙上色（5 色 Style Token；持續標記直到清除）
    QMenu *tokenMenu = editMenu->addMenu(tr("Style Token"));
    for (int c = 0; c < 5; ++c)
        tokenMenu->addAction(tr("Style Token Color %1").arg(c + 1), this,
                             [this, c] { if (auto *e = currentEditor()) e->styleTokenOccurrences(c); });
    tokenMenu->addSeparator();
    tokenMenu->addAction(tr("Clear Styled Tokens"), this,
                         [this] { if (auto *e = currentEditor()) e->clearStyledTokens(); });

    // MIME 工具（Base64 / URL 編解碼）——作用於選取（無選取則整份文件），沿用 applyTextOp
    using macpad::features::MimeTools;
    QMenu *mimeMenu = editMenu->addMenu(tr("MIME Tools"));
    mimeMenu->addAction(tr("Base64 Encode"), this, [this] { applyTextOp(&MimeTools::base64Encode); });
    mimeMenu->addAction(tr("Base64 Decode"), this, [this] { applyTextOp(&MimeTools::base64Decode); });
    mimeMenu->addAction(tr("URL Encode"), this, [this] { applyTextOp(&MimeTools::urlEncode); });
    mimeMenu->addAction(tr("URL Decode"), this, [this] { applyTextOp(&MimeTools::urlDecode); });
}


// === Notepad++ Search 選單 ===
void MainWindow::createSearchMenu(QMenu *searchMenu)
{
    searchMenu->addAction(tr("Find…"), QKeySequence::Find, this, &MainWindow::showFind);
    searchMenu->addAction(tr("Find Next"), QKeySequence(Qt::Key_F3), this,
                          [this] { findNextDir(true); });
    searchMenu->addAction(tr("Find Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), this,
                          [this] { findNextDir(false); });
    searchMenu->addAction(tr("Select and Find Next"), QKeySequence(Qt::CTRL | Qt::Key_F3), this,
                          [this] { selectAndFind(true); });
    searchMenu->addAction(tr("Select and Find Previous"),
                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F3), this,
                          [this] { selectAndFind(false); });
    // Volatile Find（不改變對話框中儲存的查詢字串，直接以選取內容前後搜尋）
    searchMenu->addAction(tr("Find (Volatile) Next"),
                          QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F3), this, [this] {
        if (!m_findDialog)
            m_findDialog = new macpad::features::FindReplaceDialog(this);
        m_findDialog->setEditor(currentEditor());
        m_findDialog->findNextVolatile();
    });
    searchMenu->addAction(tr("Find (Volatile) Previous"),
                          QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F3), this, [this] {
        if (!m_findDialog)
            m_findDialog = new macpad::features::FindReplaceDialog(this);
        m_findDialog->setEditor(currentEditor());
        m_findDialog->findPreviousVolatile();
    });
    searchMenu->addAction(tr("Replace…"), QKeySequence::Replace, this, &MainWindow::showReplace);
    searchMenu->addAction(tr("Incremental Search"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I),
                          this, [this] { showIncrementalSearch(); });
    searchMenu->addSeparator();
    searchMenu->addAction(tr("Go to…"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
                          [this] { gotoLineOrOffset(); });
    searchMenu->addAction(tr("Go to Matching Brace"), QKeySequence(Qt::CTRL | Qt::Key_B), this,
                          [this] { if (auto *e = currentEditor()) e->moveToMatchingBrace(); });
    searchMenu->addAction(tr("Select All Between Matching Braces"),
                          QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B), this,
                          [this] { if (auto *e = currentEditor()) e->selectBetweenBraces(); });
    searchMenu->addSeparator();
    searchMenu->addAction(tr("Mark All Occurrences of Selection"),
                          QKeySequence(Qt::CTRL | Qt::Key_M), this,
                          [this] { markSelectionOccurrences(); });
    searchMenu->addAction(tr("Clear All Marks"), this, [this] { clearAllMarks(); });
    searchMenu->addSeparator();

    // Bookmark 子選單
    QMenu *bm = searchMenu->addMenu(tr("Bookmark"));
    bm->addAction(tr("Toggle Bookmark"), QKeySequence(Qt::CTRL | Qt::Key_F2), this,
                  [this] { if (auto *e = currentEditor()) e->toggleBookmark(); });
    bm->addAction(tr("Next Bookmark"), QKeySequence(Qt::Key_F2), this,
                  [this] { if (auto *e = currentEditor()) e->nextBookmark(); });
    bm->addAction(tr("Previous Bookmark"), QKeySequence(Qt::SHIFT | Qt::Key_F2), this,
                  [this] { if (auto *e = currentEditor()) e->prevBookmark(); });
    bm->addSeparator();
    bm->addAction(tr("Clear All Bookmarks"), this,
                  [this] { if (auto *e = currentEditor()) e->clearAllBookmarks(); });
    bm->addAction(tr("Copy Bookmarked Lines"), this, [this] {
        if (auto *e = currentEditor())
            QApplication::clipboard()->setText(e->bookmarkedText());
    });
    bm->addAction(tr("Remove Bookmarked Lines"), this,
                  [this] { if (auto *e = currentEditor()) e->removeBookmarkedLines(); });
    bm->addAction(tr("Remove Non-Bookmarked Lines"), this,
                  [this] { if (auto *e = currentEditor()) e->removeNonBookmarkedLines(); });
    bm->addAction(tr("Inverse Bookmark"), this,
                  [this] { if (auto *e = currentEditor()) e->inverseBookmarks(); });
    bm->addAction(tr("Cut Bookmarked Lines"), this,
                  [this] { if (auto *e = currentEditor()) e->cutBookmarkedLines(); });
    bm->addAction(tr("Paste to (Replace) Bookmarked Lines"), this,
                  [this] { if (auto *e = currentEditor()) e->pasteReplaceBookmarkedLines(); });

    searchMenu->addSeparator();
    searchMenu->addAction(tr("Replace All in All Opened Documents…"), this, [this] {
        bool ok = false;
        const QString findText = QInputDialog::getText(this, tr("Replace All in All Opened Documents"),
                                                        tr("Find what:"), QLineEdit::Normal,
                                                        QString(), &ok);
        if (!ok || findText.isEmpty())
            return;
        const QString replText = QInputDialog::getText(this, tr("Replace All in All Opened Documents"),
                                                        tr("Replace with:"), QLineEdit::Normal,
                                                        QString(), &ok);
        if (!ok)
            return;
        int total = 0;
        // Dual-View：涵蓋兩個檢視的所有文件；略過 clone（與來源共享同一文件，
        // 對來源取代即已生效，重複呼叫會造成雙重取代）。
        for (QTabWidget *w : {m_tabs, m_tabs2}) {
            if (!w)
                continue;
            for (int i = 0; i < w->count(); ++i) {
                macpad::ui::EditorPane *p = paneIn(w, i);
                if (!p || p->isClone())
                    continue;
                EditorWidget *e = p->primary();
                if (!e || e->isReadOnly())
                    continue;
                total += e->replaceAll(findText, replText, /*regex=*/false, /*cs=*/false, /*wholeWord=*/false);
            }
        }
        statusBar()->showMessage(tr("已在所有開啟文件中取代 %1 處").arg(total), 3000);
        if (macpad::persistence::SettingsStore::load().enableSound)
            QApplication::beep();  // 動作提示音（偏好 enableSound）
    });

    // Find All in Opened Documents（FR-058）：逐分頁搜尋並彙整到結果面板
    searchMenu->addAction(tr("Find All in Opened Documents…"), this, [this] {
        bool ok = false;
        const QString pattern = QInputDialog::getText(this, tr("Find All in Opened Documents"),
                                                       tr("Find what:"), QLineEdit::Normal,
                                                       m_lastFindText, &ok);
        if (!ok || pattern.isEmpty())
            return;
        m_lastFindText = pattern;

        // FR-062：跨兩個檢視彙整搜尋。docId 以複合鍵編碼 (view, tabIndex)：view*kViewDocIdBase + i。
        QVector<macpad::features::FindAllMatch> all;
        int viewIdx = 0;
        for (QTabWidget *sv : {m_tabs, m_tabs2}) {
            if (sv) {
                for (int i = 0; i < sv->count(); ++i) {
                    EditorWidget *e = editorIn(sv, i);
                    if (!e)
                        continue;
                    const QString title = (viewIdx == 1 ? QStringLiteral("② ") : QString()) + sv->tabText(i);
                    all += macpad::features::FindAllEngine::searchInText(
                        viewIdx * kViewDocIdBase + i, title, e->text(), pattern,
                        /*regex=*/false, /*cs=*/false, /*wholeWord=*/false);
                }
            }
            ++viewIdx;
        }

        if (!m_findAllDock) {
            m_findAllDock = new macpad::features::FindAllDock(this);
            addDockWidget(Qt::BottomDockWidgetArea, m_findAllDock);
            connect(m_findAllDock, &macpad::features::FindAllDock::openLocation, this,
                    [this](int docId, int line, int column) {
                QTabWidget *w = (docId >= kViewDocIdBase) ? m_tabs2 : m_tabs;  // 解碼檢視
                const int idx = docId % kViewDocIdBase;
                if (!w || idx < 0 || idx >= w->count())
                    return;
                setActiveTabWidget(w);
                w->setCurrentIndex(idx);
                if (EditorWidget *e = currentEditor()) {
                    e->setCursorPosition(qMax(0, line - 1), qMax(0, column - 1));
                    e->ensureLineVisible(qMax(0, line - 1));
                    e->setFocus();
                }
            });
        }
        m_findAllDock->setResults(all);
        m_findAllDock->show();
        m_findAllDock->raise();
        statusBar()->showMessage(tr("找到 %1 處符合").arg(all.size()), 3000);
        if (macpad::persistence::SettingsStore::load().enableSound)
            QApplication::beep();  // 動作提示音（偏好 enableSound）
    });
}


void MainWindow::buildWindowMenu()
{
    if (!m_windowMenu)
        return;
    m_windowMenu->clear();
    m_windowMenu->addAction(tr("Next Document"), QKeySequence(Qt::CTRL | Qt::Key_Tab),
                            this, [this] { activateTabRelative(1); });
    m_windowMenu->addAction(tr("Previous Document"),
                            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                            this, [this] { activateTabRelative(-1); });
    m_windowMenu->addSeparator();
    // 開啟中的文件清單（打勾標示目前分頁）——涵蓋兩個檢視（Dual-View）
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            QAction *act = m_windowMenu->addAction(w->tabText(i));
            act->setCheckable(true);
            act->setChecked(w == currentTabWidget() && i == w->currentIndex());
            connect(act, &QAction::triggered, this, [this, w, i] {
                w->setCurrentIndex(i);
                setActiveTabWidget(w);
            });
        }
    }
}
