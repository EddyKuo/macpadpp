#include "app/MainWindow.h"

#include "core/EditorWidget.h"
#include "core/LexerFactory.h"
#include "features/search/FindReplaceDialog.h"
#include "persistence/RecentFiles.h"
#include "persistence/SessionStore.h"
#include "persistence/SettingsStore.h"
#include "platform/ThemeManager.h"
#include "ui/EditorPane.h"
#include "ui/MultiRowTabBar.h"
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

#include "ui/ColorPicker.h"
#include "ui/WindowsListDialog.h"
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


// 文件類型描述（複刻 Notepad++ 第一格）：有 lexer → 「<語言> file」；無 → 「Normal text file」
static QString docTypeName(EditorWidget *e)
{
    if (QsciLexer *lex = e->lexer()) {
        const QString lang = QString::fromLatin1(lex->language());
        if (!lang.isEmpty())
            return QObject::tr("%1 file").arg(lang);
    }
    return QObject::tr("Normal text file");
}


// EOL 顯示（Notepad++ 風格全名）
static QString eolDisplay(macpad::core::Eol eol)
{
    switch (eol) {
    case macpad::core::Eol::CrLf: return QStringLiteral("Windows (CR LF)");
    case macpad::core::Eol::Cr:   return QStringLiteral("Macintosh (CR)");
    case macpad::core::Eol::Lf:   default: return QStringLiteral("Unix (LF)");
    }
}


QTabWidget *MainWindow::currentTabWidget() const
{
    return m_activeTabs ? m_activeTabs : m_tabs;
}


QTabWidget *MainWindow::otherTabWidget() const
{
    return currentTabWidget() == m_tabs ? m_tabs2 : m_tabs;
}


EditorPane *MainWindow::currentPane() const
{
    return qobject_cast<EditorPane *>(currentTabWidget()->currentWidget());
}


EditorPane *MainWindow::paneIn(QTabWidget *w, int index) const
{
    return w ? qobject_cast<EditorPane *>(w->widget(index)) : nullptr;
}


EditorWidget *MainWindow::currentEditor() const
{
    EditorPane *pane = currentPane();
    return pane ? pane->primary() : nullptr;
}


EditorWidget *MainWindow::editorIn(QTabWidget *w, int index) const
{
    EditorPane *pane = paneIn(w, index);
    return pane ? pane->primary() : nullptr;
}


void MainWindow::forEachEditor(const std::function<void(EditorWidget *)> &fn) const
{
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            if (EditorWidget *e = editorIn(w, i))
                fn(e);
        }
    }
}


void MainWindow::setActiveTabWidget(QTabWidget *w)
{
    if (!w || w == m_activeTabs)
        return;
    m_activeTabs = w;
    // 作用中檢視變更 → 狀態列/面板/文件清單/Find 對話框全部切換到新檢視的當前文件
    updateStatusBar();
    refreshDocList();
    refreshPanels();
    if (m_findDialog)
        m_findDialog->setEditor(currentEditor());
}


// 分頁列座標 → 分頁索引。多列模式下必須用 MultiRowTabBar 的自算矩形，
// QTabBar::tabAt 非虛擬函式，仍會回傳單列版面的結果。
int MainWindow::tabIndexAtPos(QTabWidget *w, const QPoint &pos) const
{
    if (!w)
        return -1;
    if (auto *bar = qobject_cast<macpad::ui::MultiRowTabBar *>(w->tabBar()))
        return bar->tabIndexAt(pos);
    return w->tabBar()->tabAt(pos);
}


// 為某個檢視容器接上「關閉/右鍵選單/切換分頁」訊號（兩個檢視共用同一套行為）
void MainWindow::wireTabWidget(QTabWidget *w)
{
    w->setTabsClosable(true);
    w->setMovable(true);            // 分頁拖曳排序（FR-001）
    w->setDocumentMode(true);

    connect(w, &QTabWidget::tabCloseRequested, this,
            [this, w](int idx) { closeTabIn(w, idx); });

    // 分頁右鍵選單：標色 / 唯讀鎖定（FR-001）＋ Dual-View 的移動/複製
    w->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w->tabBar(), &QTabBar::customContextMenuRequested, this,
            [this, w](const QPoint &pos) {
        // 多列模式下 QTabBar::tabAt 用的是它自己的單列座標，會選到錯的分頁；
        // MultiRowTabBar::tabIndexAt 在單列時等同 tabAt。
        const int idx = tabIndexAtPos(w, pos);
        if (idx < 0) return;
        setActiveTabWidget(w);      // 右鍵操作前先讓該檢視成為作用中
        w->setCurrentIndex(idx);
        QMenu *menu = buildTabContextMenu(w, idx, this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->exec(w->tabBar()->mapToGlobal(pos));
    });

    // 雙擊分頁關閉（偏好 tabBarDoubleClickCloses；於觸發時讀取即時設定）
    connect(w->tabBar(), &QTabBar::tabBarDoubleClicked, this, [this, w](int idx) {
        if (idx >= 0
            && macpad::persistence::SettingsStore::load().tabBarDoubleClickCloses)
            closeTabIn(w, idx);
    });

    connect(w, &QTabWidget::currentChanged, this, [this, w](int) {
        setActiveTabWidget(w);
        // 若切換發生在已作用中的檢視，setActiveTabWidget 會提前 return，這裡補刷新一次
        if (w == currentTabWidget()) {
            updateStatusBar();
            refreshDocList();
            refreshPanels();
            updateToolbarState();   // Monitoring 是逐檔狀態，切分頁必須跟著更新勾選
            if (m_findDialog)
                m_findDialog->setEditor(currentEditor());
        }
    });
}


// 編輯器 → 狀態列/標題/自動完成的訊號連線（新分頁與 clone 檢視共用）
void MainWindow::wireEditorSignals(EditorWidget *editor)
{
    connect(editor, &EditorWidget::dirtyChanged, this, &MainWindow::updateTabTitle);
    // 跨檢視同步縮放（Notepad++ v8.9.5）：Ctrl+滾輪或選單縮放皆會發出 zoomChanged
    connect(editor, &EditorWidget::zoomChanged, this,
            [this, editor](int z) { syncZoomToOtherViews(editor, z); });
    connect(editor, &EditorWidget::metaChanged, this, &MainWindow::updateStatusBar);
    connect(editor, &EditorWidget::lexerChanged, this, [this, editor] {
        themeEditor(editor);
        // API 自動完成（FR-055）：依 lexer 語言套用對應語言的自動完成字典（設定允許時）
        if (macpad::persistence::SettingsStore::load().wordAutoComplete) {
            const QString langKey = languageKeyForLexer(editor->lexer());
            if (!langKey.isEmpty())
                editor->applyApiCompletions(macpad::features::ApiDatabase::entriesFor(langKey));
        }
    });
    // 編輯區右鍵選單（複刻 Notepad++）：EditorWidget 已停用 Scintilla 內建 popup 並轉發此訊號
    connect(editor, &EditorWidget::contextMenuRequested,
            this, &MainWindow::showEditorContextMenu);
    // 拖放開檔（複刻 Notepad++）：拖到編輯區的檔案由 EditorWidget 攔截後轉發至此開成分頁
    connect(editor, &EditorWidget::filesDropped, this, &MainWindow::openDroppedFiles);
    connect(editor, &QsciScintilla::cursorPositionChanged, this,
            [this](int, int) { updateStatusBar(); updateDocMapRange(); });
    // 捲動時更新 Document Map 的可視範圍色帶（FR-029）
    if (QScrollBar *vsb = editor->verticalScrollBar())
        connect(vsb, &QScrollBar::valueChanged, this, [this](int) { updateDocMapRange(); });
    // 長度/行數需隨編輯即時更新；選取需即時反映 Sel 欄與 INS/OVR
    connect(editor, &QsciScintilla::textChanged, this, &MainWindow::updateStatusBar);
    connect(editor, &QsciScintilla::selectionChanged, this, &MainWindow::updateStatusBar);
    // Function List 隨打字更新（複刻 Notepad++）：原本只在切換分頁/開檔時刷新，
    // 編輯中新增的函式不會出現，清單會一直是舊的。以單一 debounce timer 節流，
    // 避免每次按鍵都重新解析整份文件（大檔會卡）。
    connect(editor, &QsciScintilla::textChanged, this, [this] {
        if (!m_funcList || !m_funcList->isVisible())
            return;   // 面板沒開就不必付出解析成本
        m_panelRefreshTimer->start();   // 單發計時器；重複 start() 會重新計時
    });
    // Call tip（函式參數提示）。查找順序：
    //   1) ApiDatabase：標準庫/內建函式的「真實簽名」（printf、malloc…）——精確度最高。
    //   2) 文件內掃描：使用者自訂函式，以其定義行當簽名——ApiDatabase 不可能收錄。
    // 先前只有 (2)，導致 ApiDatabase::callTipFor 成為死碼、且標準函式提示只能靠
    // 碰巧在同檔內有定義才出得來。
    connect(editor, &EditorWidget::callTipRequested, this,
            [editor](const QString &fn) {   // languageKeyForLexer 是靜態成員，無需捕獲 this
        const QString suffix = editor->isUntitled()
                                   ? QString() : QFileInfo(editor->filePath()).suffix();
        // ApiDatabase 用的是 LexerFactory 語言鍵，與 FunctionListParser 的語言鍵不同源，
        // 故兩者各自取用自己的鍵，不可混用。
        if (const QString langKey = languageKeyForLexer(editor->lexer()); !langKey.isEmpty()) {
            // 外部 API 檔（Notepad++ 相容）可帶多個多載；有多載時以 ▲ n of m ▼ 呈現，
            // 讓使用者逐一切換，複刻上游行為。
            const auto tips = macpad::features::ApiDatabase::callTipsFor(fn, langKey);
            if (!tips.isEmpty()) {
                editor->showCallTips(tips);
                return;
            }
        }
        const QString lang = macpad::features::FunctionListParser::languageForSuffix(suffix);
        for (const auto &s : macpad::features::FunctionListParser::parse(editor->text(), lang)) {
            if (s.name == fn && s.line >= 1) {
                editor->showCallTip(editor->text(s.line - 1).trimmed());
                return;
            }
        }
    });
}


// 關閉樞紐分頁某一側的所有分頁（分頁右鍵 Close All to the Left/Right）。
// 逐一關閉並偵測使用者取消存檔（count 不變即中止），避免索引錯位。
void MainWindow::closeTabsToOneSide(QTabWidget *w, int pivot, bool toLeft)
{
    if (!w)
        return;
    if (toLeft) {
        // 反覆關閉最左的「非釘選」分頁，直到樞紐左側只剩釘選分頁。
        // （複刻 Notepad++：釘選分頁不被批次關閉指令波及）
        int target = 0;
        while (target < pivot) {
            if (isTabPinned(w, target)) {
                ++target;               // 釘選：跳過，樞紐位置不變
                continue;
            }
            const int before = w->count();
            closeTabIn(w, target);
            if (w->count() == before)   // 使用者於存檔提示取消
                return;
            --pivot;                     // 關掉一個 → 樞紐左移一格
        }
    } else {
        // 反覆關閉樞紐右側第一個「非釘選」分頁，直到沒有可關的右側分頁
        int target = pivot + 1;
        while (target < w->count()) {
            if (isTabPinned(w, target)) {
                ++target;
                continue;
            }
            const int before = w->count();
            closeTabIn(w, target);
            if (w->count() == before)
                return;
        }
    }
}


// === 釘選分頁（複刻 Notepad++ v8.7.2 Pin Tab / v8.7.3 Close All BUT Pinned）===

bool MainWindow::isTabPinned(QTabWidget *w, int index) const
{
    const auto *pane = paneIn(w, index);
    return pane && pane->isPinned();
}

int MainWindow::pinnedCount(QTabWidget *w) const
{
    if (!w)
        return 0;
    int n = 0;
    for (int i = 0; i < w->count(); ++i)
        if (isTabPinned(w, i))
            ++n;
    return n;
}

void MainWindow::setTabPinned(QTabWidget *w, int index, bool pinned)
{
    if (!w || index < 0 || index >= w->count())
        return;
    auto *pane = paneIn(w, index);
    if (!pane || pane->isPinned() == pinned)
        return;

    pane->setPinned(pinned);

    // 釘選分頁一律靠左集中：釘選時搬到釘選區末端，取消釘選時搬到釘選區之後的第一格。
    // 先算出目標位置再搬動，避免搬動後索引失效。
    const int pinned_ = pinnedCount(w);
    const int target = pinned ? pinned_ - 1 : pinned_;
    if (target != index && target >= 0 && target < w->count()) {
        w->tabBar()->moveTab(index, target);
        index = target;
    }

    // 釘選分頁不提供關閉鈕（避免誤觸），與 Notepad++ 一致
    if (w->tabsClosable()) {
        for (auto pos : {QTabBar::RightSide, QTabBar::LeftSide}) {
            if (QWidget *btn = w->tabBar()->tabButton(index, pos))
                btn->setVisible(!pinned);
        }
    }
    updateTabTitle();
}


void MainWindow::closeAllButPinned()
{
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = w->count() - 1; i >= 0; --i) {
            if (isTabPinned(w, i))
                continue;
            const int before = w->count();
            closeTabIn(w, i);
            if (w->count() == before)   // 使用者於存檔提示取消
                return;
        }
    }
}


// 編輯區右鍵選單（複刻 Notepad++ 編輯區 contextMenu.xml）。
// EditorWidget 已停用 Scintilla 內建 popup 並轉發 contextMenuRequested；sender() 即被右鍵的編輯器。
// 先把它設為作用中分頁，使以 currentEditor()/currentTabWidget() 運作的既有 slot（開檔/改名/關閉…）
// 作用於正確文件；純編輯器操作則直接綁定該編輯器指標，Dual-View/clone 情境皆正確。
void MainWindow::showEditorContextMenu(const QPoint &globalPos)
{
    EditorWidget *ed = qobject_cast<EditorWidget *>(sender());
    if (!ed)
        ed = currentEditor();
    if (!ed)
        return;

    // 讓被右鍵的編輯器成為作用中分頁
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            if (editorIn(w, i) == ed) {
                setActiveTabWidget(w);
                w->setCurrentIndex(i);
            }
        }
    }

    QMenu *menu = buildEditorContextMenu(ed, this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->exec(globalPos);
}


// On Selection：把選取內容當路徑（存在則開檔，否則以 URL 開啟）——Edit 選單與右鍵選單共用。
void MainWindow::openSelectionAsPathOrUrl(EditorWidget *ed)
{
    if (!ed || !ed->hasSelectedText())
        return;
    const QString sel = ed->selectedText().trimmed();
    if (sel.isEmpty())
        return;
    QString path = sel;
    // 相對路徑 → 以目前檔案所在目錄為基準（若目前分頁已存檔）
    if (QFileInfo(path).isRelative() && !ed->isUntitled())
        path = QFileInfo(QDir(QFileInfo(ed->filePath()).absolutePath()), path).absoluteFilePath();
    if (QFileInfo::exists(path))
        openFile(path);
    else
        QDesktopServices::openUrl(QUrl::fromUserInput(sel));
}

// On Selection：以選取內容依偏好的搜尋引擎樣板開網頁搜尋——Edit 選單與右鍵選單共用。
void MainWindow::searchSelectionOnInternet(EditorWidget *ed)
{
    if (!ed || !ed->hasSelectedText())
        return;
    const QString sel = ed->selectedText().trimmed();
    if (sel.isEmpty())
        return;
    QString tmpl = macpad::persistence::SettingsStore::load().searchEngineUrl;
    if (tmpl.isEmpty())
        tmpl = QStringLiteral("https://www.google.com/search?q=%1");
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(sel));
    const QString url = tmpl.contains(QStringLiteral("%1")) ? tmpl.arg(encoded) : tmpl + encoded;
    QDesktopServices::openUrl(QUrl(url));
}


// 建構編輯區右鍵選單本體（複刻 Notepad++ contextMenu.xml）。各項作用於 ed 本身，
// enable 狀態依 ed 當下狀態決定，故可於單元測試中直接斷言項目與啟用狀態。
QMenu *MainWindow::buildEditorContextMenu(EditorWidget *ed, QWidget *parent)
{
    const bool hasSel = ed->hasSelectedText();
    const bool hasFile = !ed->isUntitled();
    const bool ro = ed->isReadOnly();

    auto *menu = new QMenu(parent);

    // --- 復原 / 重做 ---
    menu->addAction(tr("Undo"), this, [ed] { ed->undoWithHistory(); })->setEnabled(ed->isUndoAvailable());
    menu->addAction(tr("Redo"), this, [ed] { ed->redoWithHistory(); })->setEnabled(ed->isRedoAvailable());
    menu->addSeparator();

    // --- 剪貼簿基本操作 ---
    menu->addAction(tr("Cut"), this, [ed] { ed->cut(); })->setEnabled(hasSel && !ro);
    menu->addAction(tr("Copy"), this, [ed] { ed->copy(); })->setEnabled(hasSel);
    menu->addAction(tr("Paste"), this, [ed] { ed->paste(); })->setEnabled(!ro);
    menu->addAction(tr("Delete"), this, [ed] { ed->removeSelectedText(); })
        ->setEnabled(hasSel && !ro);
    menu->addAction(tr("Select All"), this, [ed] { ed->selectAll(); });
    menu->addSeparator();

    // --- 兩段式選取 ---
    QMenu *selMenu = menu->addMenu(tr("Selection"));
    selMenu->addAction(tr("Begin Select"), this, [ed] { ed->beginSelect(); });
    selMenu->addAction(tr("End Select"), this, [ed] { ed->endSelect(); });
    selMenu->addAction(tr("Begin Column Select"), this, [ed] { ed->beginColumnSelect(); });
    selMenu->addAction(tr("End Column Select"), this, [ed] { ed->endColumnSelect(); });

    // --- 複製路徑到剪貼簿 ---
    QMenu *copyMenu = menu->addMenu(tr("Copy to Clipboard"));
    copyMenu->setEnabled(hasFile);
    addCopyPathActions(copyMenu, ed, hasFile);

    // --- 貼上特殊（去格式）---
    QMenu *pasteMenu = menu->addMenu(tr("Paste Special"));
    pasteMenu->setEnabled(!ro);
    pasteMenu->addAction(tr("Paste as Plain Text"), this, [ed] {
        if (ed->isReadOnly())
            return;
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty())
            ed->insert(text);
    });
    pasteMenu->addAction(tr("Paste HTML Content"), this,
                         [ed] { if (!ed->isReadOnly()) ed->pasteAsHtml(); });
    pasteMenu->addAction(tr("Paste RTF Content"), this,
                         [ed] { if (!ed->isReadOnly()) ed->pasteAsRtf(); });
    menu->addSeparator();

    // --- 詞彙上色（5 色 Style Token）---
    QMenu *tokenMenu = menu->addMenu(tr("Style Token"));
    for (int c = 0; c < 5; ++c)
        tokenMenu->addAction(tr("Style Token Color %1").arg(c + 1), this,
                             [ed, c] { ed->styleTokenOccurrences(c); });
    tokenMenu->addSeparator();
    tokenMenu->addAction(tr("Clear Styled Tokens"), this, [ed] { ed->clearStyledTokens(); });

    // --- 書籤 ---
    menu->addAction(tr("Toggle Bookmark"), this, [ed] { ed->toggleBookmark(); });
    menu->addSeparator();

    // --- 針對選取內容 ---
    QMenu *onSelMenu = menu->addMenu(tr("On Selection"));
    onSelMenu->setEnabled(hasSel);
    onSelMenu->addAction(tr("Open Selected File"), this,
                         [this, ed] { openSelectionAsPathOrUrl(ed); });
    onSelMenu->addAction(tr("Search on Internet"), this,
                         [this, ed] { searchSelectionOnInternet(ed); });
    menu->addSeparator();

    // --- 檔案 / 分頁操作（作用於作用中分頁，即上面設定的被右鍵編輯器）---
    menu->addAction(tr("Open in Default Application"), this, [this] { openInDefaultApp(); })
        ->setEnabled(hasFile);
    menu->addAction(tr("Open Containing Folder"), this, [this] { revealInFinder(); })
        ->setEnabled(hasFile);
    menu->addSeparator();
    menu->addAction(tr("Reload from Disk"), this, [this] { reloadFromDisk(); })
        ->setEnabled(hasFile);
    menu->addAction(tr("Rename…"), this, [this] { renameCurrentFile(); });
    menu->addAction(tr("Move to Recycle Bin"), this, [this] { moveCurrentToTrash(); })
        ->setEnabled(hasFile);
    menu->addSeparator();

    // --- 唯讀切換 ---
    QAction *roAct = menu->addAction(tr("Read-Only"));
    roAct->setCheckable(true);
    roAct->setChecked(ro);
    connect(roAct, &QAction::toggled, this, [this, ed](bool on) {
        ed->setReadOnly(on);
        statusBar()->showMessage(on ? tr("唯讀已開啟") : tr("唯讀已關閉"), 2000);
    });
    menu->addSeparator();
    menu->addAction(tr("Close"), this, [this] {
        QTabWidget *w = currentTabWidget();
        if (w->count() > 0)
            closeTab(w->currentIndex());
    });

    return menu;
}


// 加入「複製完整路徑 / 檔名 / 目錄」三動作（分頁右鍵與編輯區右鍵共用）；hasFile 決定 enable
void MainWindow::addCopyPathActions(QMenu *menu, EditorWidget *ed, bool hasFile)
{
    menu->addAction(tr("Copy Full File Path"), this, [ed] {
        if (ed) QApplication::clipboard()->setText(ed->filePath());
    })->setEnabled(hasFile);
    menu->addAction(tr("Copy File Name"), this, [ed] {
        if (ed) QApplication::clipboard()->setText(QFileInfo(ed->filePath()).fileName());
    })->setEnabled(hasFile);
    menu->addAction(tr("Copy Directory Path"), this, [ed] {
        if (ed) QApplication::clipboard()->setText(QFileInfo(ed->filePath()).absolutePath());
    })->setEnabled(hasFile);
}


// 建構分頁右鍵選單（複刻 Notepad++ 分頁右鍵；僅建構+回傳，不 exec，便於單元測試斷言 enable 狀態）
QMenu *MainWindow::buildTabContextMenu(QTabWidget *w, int index, QWidget *parent)
{
    EditorWidget *e = editorIn(w, index);
    const bool hasFile = e && !e->isUntitled();
    auto *menu = new QMenu(parent);

    // --- 檔案操作 ---
    // --- 釘選（複刻 Notepad++ v8.7.2/v8.7.3）---
    const bool pinned = isTabPinned(w, index);
    menu->addAction(pinned ? tr("Unpin Tab") : tr("Pin Tab"), this,
                    [this, w, index, pinned] { setTabPinned(w, index, !pinned); });
    menu->addSeparator();

    menu->addAction(tr("Close"), this, [this, w, index] { closeTabIn(w, index); });
    menu->addAction(tr("Close All but This"), this, [this] { closeAllButCurrent(); });
    menu->addAction(tr("Close All BUT Pinned"), this, [this] { closeAllButPinned(); });
    menu->addAction(tr("Close All to the Left"), this,
                    [this, w, index] { closeTabsToOneSide(w, index, /*toLeft=*/true); });
    menu->addAction(tr("Close All to the Right"), this,
                    [this, w, index] { closeTabsToOneSide(w, index, /*toLeft=*/false); });
    menu->addSeparator();
    menu->addAction(tr("Save"), this, [this] { saveCurrent(); });
    menu->addAction(tr("Save As…"), this, [this] { saveCurrentAs(); });
    menu->addAction(tr("Rename…"), this, [this] { renameCurrentFile(); });
    menu->addAction(tr("Reload from Disk"), this, [this] { reloadFromDisk(); })
        ->setEnabled(hasFile);
    menu->addAction(tr("Move to Recycle Bin"), this, [this] { moveCurrentToTrash(); })
        ->setEnabled(hasFile);
    menu->addSeparator();
    menu->addAction(tr("Open Containing Folder"), this, [this] { revealInFinder(); })
        ->setEnabled(hasFile);
    menu->addAction(tr("Open in Default Application"), this, [this] { openInDefaultApp(); })
        ->setEnabled(hasFile);
    addCopyPathActions(menu, e, hasFile);
    menu->addSeparator();

    // --- 外觀/鎖定 ---
    menu->addAction(tr("Set Tab Color…"), this, [this, w, index] {
        const QColor c = macpad::ui::ColorPicker::getColor(Qt::white, this, tr("Tab Color"));
        if (c.isValid())
            w->tabBar()->setTabTextColor(index, c);
    });
    menu->addAction(tr("Clear Tab Color"), this, [w, index] {
        w->tabBar()->setTabTextColor(index, QColor());
    });
    if (e) {
        QAction *lock = menu->addAction(tr("Read-Only"));
        lock->setCheckable(true);
        lock->setChecked(e->isReadOnly());
        connect(lock, &QAction::toggled, this, [this, w, index](bool ro) {
            if (EditorWidget *ed = editorIn(w, index)) {
                ed->setReadOnly(ro);
                w->setTabText(index, (ro ? QStringLiteral("🔒 ") : QString())
                                         + ed->displayName());
            }
        });
    }
    menu->addSeparator();
    menu->addAction(tr("Move to Other View"), this, &MainWindow::moveToOtherView);
    menu->addAction(tr("Clone to Other View"), this, &MainWindow::cloneToOtherView);
    return menu;
}


// 未命名分頁的最小可用序號：掃描兩檢視所有 untitled 分頁已用號，回傳最小未用正整數（複刻 Notepad++ new N，會回收已關閉的號）
int MainWindow::nextUntitledNumber() const
{
    QSet<int> used;
    forEachEditor([&used](EditorWidget *e) {
        if (e && e->isUntitled() && e->untitledNumber() > 0)
            used.insert(e->untitledNumber());
    });
    int n = 1;
    while (used.contains(n))
        ++n;
    return n;
}


EditorWidget *MainWindow::addEditorTab()
{
    // 新分頁一律加入作用中檢視（單一檢視時即 m_tabs，行為與改動前一致）
    QTabWidget *w = currentTabWidget();
    auto *pane = new EditorPane(w);
    EditorWidget *editor = pane->primary();
    // 指派未命名序號（untitled(N)）——於加入分頁前計算，故不會把自己算進去
    editor->setUntitledNumber(nextUntitledNumber());

    wireEditorSignals(editor);

    themeEditor(editor);
    applyEditorPrefs(editor, macpad::persistence::SettingsStore::load());  // 新分頁套用目前偏好
    // Smart Highlighting 為全域檢視狀態：新分頁跟隨 View 選單目前勾選狀態
    if (m_smartHighlightAct && m_smartHighlightAct->isChecked())
        editor->setSmartHighlight(true);

    const int idx = w->addTab(pane, editor->displayName());
    w->setCurrentIndex(idx);
    return editor;
}


void MainWindow::toggleSplit()
{
    if (EditorPane *pane = currentPane())
        pane->toggleSplit();  // FR-002
}


// --- Dual-View（雙檢視，複刻 Notepad++ Move/Clone to Other View）---------

// 第二檢視空了就隱藏、有內容就顯示；作用中檢視若被隱藏則退回主檢視。
void MainWindow::updateSecondViewVisibility()
{
    if (!m_tabs2)
        return;
    const bool hasTabs = m_tabs2->count() > 0;
    m_tabs2->setVisible(hasTabs);
    if (!hasTabs && m_activeTabs == m_tabs2)
        setActiveTabWidget(m_tabs);
    // 同步捲動只在雙檢視下有意義；這裡是第二檢視顯示狀態的唯一出入口，
    // 把工具列按鈕的啟用狀態掛在此處，就不會有漏掉的路徑。
    updateToolbarState();
}


// 把目前分頁從作用中檢視「搬移」到另一個檢視（原 pane 物件直接轉移，狀態不變）。
void MainWindow::moveToOtherView()
{
    QTabWidget *src = currentTabWidget();
    QTabWidget *dst = otherTabWidget();
    EditorPane *pane = qobject_cast<EditorPane *>(src->currentWidget());
    if (!pane || !dst || src == dst)
        return;

    const int idx = src->indexOf(pane);
    const QString title = src->tabText(idx);
    const QString tip = src->tabToolTip(idx);
    const QColor col = src->tabBar()->tabTextColor(idx);

    src->removeTab(idx);                         // pane 仍存活（QTabWidget 不刪除被移除的頁面）
    const int nidx = dst->addTab(pane, title);   // addTab 自動 reparent 到 dst
    dst->setTabToolTip(nidx, tip);
    dst->tabBar()->setTabTextColor(nidx, col);
    dst->setCurrentIndex(nidx);

    updateSecondViewVisibility();                // dst 若原本隱藏（第二檢視）現在要顯示
    setActiveTabWidget(dst);
    pane->primary()->setFocus();

    // 若來源檢視被搬空：第二檢視自動隱藏；主檢視若空了且無其他文件才補一張空白頁
    if (src->count() == 0) {
        if (src == m_tabs2)
            updateSecondViewVisibility();
        else if (!m_tabs2 || m_tabs2->count() == 0)
            newFile();
    }
    updateTabTitle();
}


// 把目前文件「複製」到另一個檢視：新分頁與來源共享同一 QsciDocument（游標/捲動獨立）。
void MainWindow::cloneToOtherView()
{
    EditorPane *srcPane = currentPane();
    QTabWidget *dst = otherTabWidget();
    if (!srcPane || !dst)
        return;
    EditorWidget *src = srcPane->primary();
    if (!src)
        return;

    EditorPane *clone = EditorPane::makeClone(src, dst);
    wireEditorSignals(clone->primary());          // clone 也要即時更新狀態列/標題
    // 為 clone 建立獨立 lexer 實例（與來源相同語言，但各自擁有；避免共用指標懸空）
    if (const QString langKey = languageKeyForLexer(src->lexer()); !langKey.isEmpty()) {
        if (QsciLexer *lex = macpad::core::LexerFactory::createForLanguage(langKey, clone->primary()))
            clone->primary()->setLanguageLexer(lex);
    }
    themeEditor(clone->primary());
    applyEditorPrefs(clone->primary(), macpad::persistence::SettingsStore::load());

    const int nidx = dst->addTab(clone, srcPane->tabTitle());
    dst->setCurrentIndex(nidx);

    updateSecondViewVisibility();
    setActiveTabWidget(dst);
    clone->primary()->setFocus();
    updateTabTitle();
}


// ===== Notepad++ 對等：Distraction Free / Incremental Search / View in Browser =====

void MainWindow::setDistractionFree(bool on)
{
    m_distractionFree = on;
    if (on) {
        m_dfHidden.clear();
        for (QDockWidget *d : findChildren<QDockWidget *>()) {
            if (d->isVisible()) {
                m_dfHidden.append(d);
                d->hide();
            }
        }
        if (m_toolbar) m_toolbar->hide();
        statusBar()->hide();
        m_tabs->tabBar()->hide();
        if (m_tabs2) m_tabs2->tabBar()->hide();
        showFullScreen();
    } else {
        for (QDockWidget *d : m_dfHidden)
            d->show();
        m_dfHidden.clear();
        statusBar()->show();
        m_tabs->tabBar()->show();
        if (m_tabs2) m_tabs2->tabBar()->show();
        showNormal();
    }
}


void MainWindow::setPostIt(bool on)
{
    // Post-It（複刻 Notepad++）：無邊框 + 永遠置頂 + 隱藏所有周邊，只留純文字，像便利貼
    m_postIt = on;
    if (on) {
        m_postItHidden.clear();
        for (QDockWidget *d : findChildren<QDockWidget *>()) {
            if (d->isVisible()) { m_postItHidden.append(d); d->hide(); }
        }
        if (m_toolbar) m_toolbar->hide();
        statusBar()->hide();
        m_tabs->tabBar()->hide();
        if (m_tabs2) m_tabs2->tabBar()->hide();
        setWindowFlag(Qt::FramelessWindowHint, true);
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        showNormal();
        show();
    } else {
        for (QDockWidget *d : m_postItHidden)
            d->show();
        m_postItHidden.clear();
        statusBar()->show();
        m_tabs->tabBar()->show();
        if (m_tabs2) m_tabs2->tabBar()->show();
        setWindowFlag(Qt::FramelessWindowHint, false);
        setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
        show();
    }
}


void MainWindow::activateTabRelative(int delta)
{
    QTabWidget *w = currentTabWidget();
    const int n = w->count();
    if (n <= 0)
        return;
    w->setCurrentIndex((w->currentIndex() + delta + n) % n);
}


void MainWindow::moveCurrentTab(int delta)
{
    QTabWidget *w = currentTabWidget();
    const int idx = w->currentIndex();
    const int dst = idx + delta;
    if (idx < 0 || dst < 0 || dst >= w->count())
        return;
    w->tabBar()->moveTab(idx, dst);
}


void MainWindow::toggleMonitoring()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled()) {
        QMessageBox::information(this, tr("Monitoring (tail -f)"), tr("此分頁尚未存成檔案。"));
        // 動作是可勾選的，Qt 已在 triggered 之前改過勾選狀態；這裡沒真的開始監控，
        // 必須把它撥回去，否則按鈕會顯示「監控中」而實際什麼都沒發生。
        updateToolbarState();
        return;
    }
    const QString path = e->filePath();
    if (m_monitored.contains(path)) {
        m_monitored.remove(path);
        e->setPolicyReadOnly(false);   // 解除政策鎖定
        statusBar()->showMessage(tr("已停止監控：%1").arg(QFileInfo(path).fileName()), 3000);
    } else {
        m_monitored.insert(path);
        watchPath(path);
        e->setPolicyReadOnly(true);   // 監控時唯讀（如 tail -f）；屬政策鎖定
        e->setCursorPosition(e->lines() - 1, 0);
        e->ensureLineVisible(e->lines() - 1);
        statusBar()->showMessage(tr("監控中（tail -f）：%1").arg(QFileInfo(path).fileName()), 3000);
    }
    updateToolbarState();
}


// Window ▸ Windows…（複刻 Notepad++ 的文件管理對話框）。
// 以「攤平的 (檢視, 分頁索引)」清單為索引依據，與對話框回報的列號一一對應。
void MainWindow::showWindowsListDialog()
{
    QVector<QPair<QTabWidget *, int>> map;
    QVector<macpad::ui::WindowsListEntry> entries;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorWidget *e = editorIn(w, i);
            if (!e)
                continue;
            macpad::ui::WindowsListEntry entry;
            entry.name = w->tabText(i);
            entry.path = e->isUntitled() ? QString() : e->filePath();
            entry.type = docTypeName(e);
            entry.size = e->text().toUtf8().size();
            if (!entry.path.isEmpty())
                entry.modified = QFileInfo(entry.path).lastModified();
            entries.push_back(entry);
            map.push_back({w, i});
        }
    }
    if (entries.isEmpty())
        return;

    macpad::ui::WindowsListDialog dlg(entries, this);

    connect(&dlg, &macpad::ui::WindowsListDialog::activateRequested, this, [this, map](int row) {
        if (row < 0 || row >= map.size())
            return;
        setActiveTabWidget(map[row].first);
        map[row].first->setCurrentIndex(map[row].second);
    });
    connect(&dlg, &macpad::ui::WindowsListDialog::saveRequested, this,
            [this, map](const QVector<int> &rows) {
        QTabWidget *prevW = currentTabWidget();
        const int prevIdx = prevW ? prevW->currentIndex() : -1;
        for (int row : rows) {
            if (row < 0 || row >= map.size())
                continue;
            setActiveTabWidget(map[row].first);
            map[row].first->setCurrentIndex(map[row].second);
            saveCurrent();
        }
        if (prevW && prevIdx >= 0 && prevIdx < prevW->count()) {
            setActiveTabWidget(prevW);
            prevW->setCurrentIndex(prevIdx);
        }
    });
    connect(&dlg, &macpad::ui::WindowsListDialog::closeRequested, this,
            [this, map](const QVector<int> &rows) {
        // 由後往前關，避免關閉前面的分頁使後面的索引位移
        QVector<int> ordered = rows;
        std::sort(ordered.begin(), ordered.end(), std::greater<int>());
        for (int row : ordered) {
            if (row < 0 || row >= map.size())
                continue;
            closeTabIn(map[row].first, map[row].second);
        }
    });
    connect(&dlg, &macpad::ui::WindowsListDialog::sortTabsRequested, this, [this] {
        // 依分頁標題排序目前檢視的分頁（Notepad++ 的 Sort Tabs）；釘選分頁維持在前段不動。
        QTabWidget *w = currentTabWidget();
        if (!w)
            return;
        const int firstMovable = pinnedCount(w);
        for (int i = firstMovable; i < w->count(); ++i) {
            int best = i;
            for (int j = i + 1; j < w->count(); ++j)
                if (w->tabText(j).compare(w->tabText(best), Qt::CaseInsensitive) < 0)
                    best = j;
            if (best != i)
                w->tabBar()->moveTab(best, i);
        }
    });

    dlg.exec();
}


void MainWindow::syncZoomToOtherViews(EditorWidget *source, int zoom)
{
    if (!macpad::persistence::SettingsStore::load().syncZoomBetweenViews)
        return;
    forEachEditor([&](EditorWidget *e) {
        if (e == source)
            return;
        if (static_cast<int>(e->SendScintilla(QsciScintillaBase::SCI_GETZOOM)) != zoom)
            e->zoomTo(zoom);
    });
}


QString MainWindow::tabLabelFor(EditorPane *pane) const
{
    if (!pane)
        return QString();
    const auto s = macpad::persistence::SettingsStore::load();
    EditorWidget *e = pane->primary();
    QString title = pane->tabTitle();          // clone 追隨來源檔名

    // 未命名分頁以內容首行命名（Notepad++ v8.8.2）；首行為空白時維持 untitled(N)
    if (s.tabBarUntitledNameFromFirstLine && e && e->isUntitled()) {
        const QString firstLine = e->text(0).trimmed();
        if (!firstLine.isEmpty())
            title = firstLine;
    }

    // 標籤長度上限（Notepad++ v8.8.8）——避免單一長檔名把分頁列撐爆
    if (s.tabBarLabelMaxLength > 0 && title.size() > s.tabBarLabelMaxLength)
        title = title.left(qMax(1, s.tabBarLabelMaxLength - 1)) + QStringLiteral("…");

    // 前綴標記：釘選（📌）與唯讀（🔒）
    QString prefix;
    if (pane->isPinned())
        prefix += QStringLiteral("📌 ");
    if (e && e->isReadOnly())
        prefix += QStringLiteral("🔒 ");
    return prefix + title;
}


QString MainWindow::tabTooltipFor(EditorPane *pane) const
{
    if (!pane || !pane->primary())
        return QString();
    EditorWidget *e = pane->primary();
    if (!e->isUntitled())
        return e->filePath();
    // 未命名分頁沒有路徑可顯示 → 改顯示建立時間（複刻 Notepad++ v8.7.1）
    return tr("Created: %1").arg(pane->createdAt().toString(Qt::TextDate));
}


void MainWindow::updateTabTitle()
{
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorPane *p = paneIn(w, i);
            if (!p)
                continue;
            w->setTabText(i, tabLabelFor(p));             // clone 追隨來源檔名
            w->setTabToolTip(i, tabTooltipFor(p));
        }
    }
    refreshDocList();
}


void MainWindow::refreshDocList()
{
    if (!m_docList)
        return;
    // 文件清單合併兩個檢視（FR-062）；m_docListMap 記錄每列對應的 (檢視, 分頁索引) 供跳轉。
    // 前幾行預覽（docPeekerEnabled）：取即時文字（含未存/未命名），限行數與每行長度避免過大 tooltip。
    auto previewOf = [](const QString &content) -> QString {
        const QStringList lines = content.split(QLatin1Char('\n'));
        const int n = qMin(15, int(lines.size()));
        QStringList head;
        head.reserve(n);
        for (int i = 0; i < n; ++i) {
            QString ln = lines.at(i);
            if (ln.size() > 200)
                ln = ln.left(200) + QStringLiteral("…");
            head << ln;
        }
        QString out = head.join(QLatin1Char('\n'));
        if (lines.size() > 15)
            out += QStringLiteral("\n…");
        return out;
    };

    QStringList names;
    QStringList paths;
    QStringList previews;
    m_docListMap.clear();
    int current = -1;
    int viewIdx = 0;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (w && (w == m_tabs || w->count() > 0)) {  // 第二檢視空則不列
            for (int i = 0; i < w->count(); ++i) {
                EditorPane *p = paneIn(w, i);
                EditorWidget *ed = p ? p->primary() : nullptr;
                const QString prefix = (viewIdx == 1) ? QStringLiteral("② ") : QString();
                names << prefix + (p ? p->tabTitle() : tr("Untitled"));
                paths << (ed ? ed->filePath() : QString());
                previews << (ed ? previewOf(ed->text()) : QString());
                if (w == currentTabWidget() && i == w->currentIndex())
                    current = int(m_docListMap.size());
                m_docListMap.append({w, i});
            }
        }
        ++viewIdx;
    }
    m_docList->setPeekEnabled(macpad::persistence::SettingsStore::load().docPeekerEnabled);
    m_docList->setPreviews(previews);
    m_docList->refresh(names, paths, current);
}


void MainWindow::refreshPanels()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    const QString suffix = e->isUntitled() ? QString() : QFileInfo(e->filePath()).suffix();
    if (m_funcList && m_funcList->isVisible())
        m_funcList->update(e->text(), suffix);
    if (m_docMap && m_docMap->isVisible()) {
        m_docMap->attach(e);
        updateDocMapRange();
    }
}


void MainWindow::updateDocMapRange()
{
    if (!m_docMap || !m_docMap->isVisible())
        return;
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    // 第一可視行與螢幕上可容納行數（best-effort：反映作用中編輯器的視埠）
    const int first = int(e->SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE));
    const int onScreen = int(e->SendScintilla(QsciScintilla::SCI_LINESONSCREEN));
    m_docMap->setVisibleRange(first, onScreen);
}


// 建立 Notepad++ 風格的分格狀態列：文件類型（左、可伸展）＋ 5 個右側固定格。
// 每格為下凹面板（QFrame::Panel|Sunken），視覺上就是 Notepad++ 的分隔格。
void MainWindow::createStatusCells()
{
    auto makeCell = [this](Qt::Alignment align, int minW) {
        auto *lbl = new QLabel(this);
        lbl->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        lbl->setAlignment(align | Qt::AlignVCenter);
        lbl->setContentsMargins(6, 0, 6, 0);
        lbl->setMinimumWidth(minW);
        return lbl;
    };

    // 第 1 格：文件類型（靠左、佔滿剩餘寬度）——用 addWidget 放左側並給 stretch
    m_stDoc = makeCell(Qt::AlignLeft, 120);
    statusBar()->addWidget(m_stDoc, 1);

    // 右側 5 格（順序即 Notepad++：長度·行數 / 游標·選取 / EOL / 編碼 / INS·OVR）
    m_stLenLines = makeCell(Qt::AlignLeft, 190);
    m_stCaret    = makeCell(Qt::AlignLeft, 230);
    m_stEol      = makeCell(Qt::AlignHCenter, 140);
    m_stEnc      = makeCell(Qt::AlignHCenter, 130);
    m_stMode     = makeCell(Qt::AlignHCenter, 52);
    statusBar()->addPermanentWidget(m_stLenLines);
    statusBar()->addPermanentWidget(m_stCaret);
    statusBar()->addPermanentWidget(m_stEol);
    statusBar()->addPermanentWidget(m_stEnc);
    statusBar()->addPermanentWidget(m_stMode);

    statusBar()->setSizeGripEnabled(true);
}


void MainWindow::updateStatusBar()
{
    EditorWidget *editor = currentEditor();
    if (!editor) {
        m_stDoc->clear();
        m_stLenLines->clear();
        m_stCaret->clear();
        m_stEol->clear();
        m_stEnc->clear();
        m_stMode->clear();
        return;
    }

    const EditorWidget::DocStats s = editor->stats();
    const QLocale loc;

    m_stDoc->setText(docTypeName(editor));

    m_stLenLines->setText(tr("length : %1    lines : %2")
                              .arg(loc.toString(qlonglong(s.length)))
                              .arg(loc.toString(qlonglong(s.lines))));

    QString caret = tr("Ln : %1    Col : %2    Pos : %3")
                        .arg(loc.toString(s.line))
                        .arg(loc.toString(s.col))
                        .arg(loc.toString(qlonglong(s.pos)));
    if (s.selChars > 0)
        caret += tr("    Sel : %1 | %2")
                     .arg(loc.toString(qlonglong(s.selChars)))
                     .arg(loc.toString(s.selLines));
    m_stCaret->setText(caret);

    m_stEol->setText(eolDisplay(editor->eol()));
    m_stEnc->setText(editor->encodingLabel());
    // Character Panel 標頭同步顯示作用中文件的編碼；並依編碼切換 128..255 區的字元對映：
    // Latin1（或有指定 legacy save codec）→ ANSI/Latin-1 全 8-bit 顯示；其餘 Unicode 家族 → codepage 映射。
    if (m_charPanel) {
        m_charPanel->setEncodingLabel(editor->encodingLabel());
        const bool ansi = (editor->encoding() == macpad::core::Encoding::Latin1)
                          || !editor->saveCodec().isEmpty();
        m_charPanel->setUnicodeMode(!ansi);
    }
    m_stMode->setText(s.overtype ? QStringLiteral("OVR") : QStringLiteral("INS"));
}


void MainWindow::themeEditor(EditorWidget *editor)
{
    using macpad::persistence::ThemeMode;
    const auto settings = macpad::persistence::SettingsStore::load();
    bool dark;
    switch (settings.theme) {
    case ThemeMode::Light: dark = false; break;
    case ThemeMode::Dark:  dark = true;  break;
    default:               dark = macpad::platform::ThemeManager::systemIsDark(); break;
    }
    macpad::platform::ThemeManager::applyToEditor(editor, dark);
}


void MainWindow::applyTheme()
{
    // 重建每個編輯器的 lexer（回到預設色）→ lexerChanged 觸發 themeEditor 重新降飽和，
    // 避免對已降飽和的顏色再次降飽和（疊加變灰）。
    forEachEditor([](EditorWidget *e) {
        if (e)
            e->reapplyLexer();
    });
    retintToolbar();  // 圖示跟隨主題明暗重新上色
}
