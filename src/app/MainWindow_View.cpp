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


EditorPane *MainWindow::paneAt(int index) const
{
    return paneIn(currentTabWidget(), index);
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


EditorWidget *MainWindow::editorAt(int index) const
{
    EditorPane *pane = paneAt(index);
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
        const int idx = w->tabBar()->tabAt(pos);
        if (idx < 0) return;
        setActiveTabWidget(w);      // 右鍵操作前先讓該檢視成為作用中
        w->setCurrentIndex(idx);
        EditorWidget *e = editorIn(w, idx);
        const bool hasFile = e && !e->isUntitled();
        QMenu menu;

        // --- 檔案操作（複刻 Notepad++ 分頁右鍵）---
        menu.addAction(tr("Close"), this, [this, w, idx] { closeTabIn(w, idx); });
        menu.addAction(tr("Close All but This"), this, [this] { closeAllButCurrent(); });
        menu.addAction(tr("Close All to the Left"), this,
                       [this, w, idx] { closeTabsToOneSide(w, idx, /*toLeft=*/true); });
        menu.addAction(tr("Close All to the Right"), this,
                       [this, w, idx] { closeTabsToOneSide(w, idx, /*toLeft=*/false); });
        menu.addSeparator();
        menu.addAction(tr("Save"), this, [this] { saveCurrent(); });
        menu.addAction(tr("Save As…"), this, [this] { saveCurrentAs(); });
        menu.addAction(tr("Rename…"), this, [this] { renameCurrentFile(); });
        menu.addAction(tr("Reload from Disk"), this, [this] { reloadFromDisk(); })
            ->setEnabled(hasFile);
        menu.addAction(tr("Move to Recycle Bin"), this, [this] { moveCurrentToTrash(); })
            ->setEnabled(hasFile);
        menu.addSeparator();
        menu.addAction(tr("Open Containing Folder"), this, [this] { revealInFinder(); })
            ->setEnabled(hasFile);
        menu.addAction(tr("Open in Default Application"), this, [this] { openInDefaultApp(); })
            ->setEnabled(hasFile);
        menu.addAction(tr("Copy Full File Path"), this, [e] {
            if (e) QApplication::clipboard()->setText(e->filePath());
        })->setEnabled(hasFile);
        menu.addAction(tr("Copy File Name"), this, [e] {
            if (e) QApplication::clipboard()->setText(QFileInfo(e->filePath()).fileName());
        })->setEnabled(hasFile);
        menu.addAction(tr("Copy Directory Path"), this, [e] {
            if (e) QApplication::clipboard()->setText(QFileInfo(e->filePath()).absolutePath());
        })->setEnabled(hasFile);
        menu.addSeparator();

        // --- 外觀/鎖定 ---
        menu.addAction(tr("Set Tab Color…"), this, [this, w, idx] {
            const QColor c = QColorDialog::getColor(Qt::white, this, tr("Tab Color"));
            if (c.isValid())
                w->tabBar()->setTabTextColor(idx, c);
        });
        menu.addAction(tr("Clear Tab Color"), this, [w, idx] {
            w->tabBar()->setTabTextColor(idx, QColor());
        });
        if (e) {
            QAction *lock = menu.addAction(tr("Read-Only"));
            lock->setCheckable(true);
            lock->setChecked(e->isReadOnly());
            connect(lock, &QAction::toggled, this, [this, w, idx](bool ro) {
                if (EditorWidget *ed = editorIn(w, idx)) {
                    ed->setReadOnly(ro);
                    w->setTabText(idx, (ro ? QStringLiteral("🔒 ") : QString())
                                            + ed->displayName());
                }
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Move to Other View"), this, &MainWindow::moveToOtherView);
        menu.addAction(tr("Clone to Other View"), this, &MainWindow::cloneToOtherView);
        menu.exec(w->tabBar()->mapToGlobal(pos));
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
            if (m_findDialog)
                m_findDialog->setEditor(currentEditor());
        }
    });
}


// 編輯器 → 狀態列/標題/自動完成的訊號連線（新分頁與 clone 檢視共用）
void MainWindow::wireEditorSignals(EditorWidget *editor)
{
    connect(editor, &EditorWidget::dirtyChanged, this, &MainWindow::updateTabTitle);
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
    connect(editor, &QsciScintilla::cursorPositionChanged, this,
            [this](int, int) { updateStatusBar(); updateDocMapRange(); });
    // 捲動時更新 Document Map 的可視範圍色帶（FR-029）
    if (QScrollBar *vsb = editor->verticalScrollBar())
        connect(vsb, &QScrollBar::valueChanged, this, [this](int) { updateDocMapRange(); });
    // 長度/行數需隨編輯即時更新；選取需即時反映 Sel 欄與 INS/OVR
    connect(editor, &QsciScintilla::textChanged, this, &MainWindow::updateStatusBar);
    connect(editor, &QsciScintilla::selectionChanged, this, &MainWindow::updateStatusBar);
    // Call tip（函式參數提示）：鍵入 '(' 時查文件內同名函式定義行當簽名
    connect(editor, &EditorWidget::callTipRequested, this,
            [editor](const QString &fn) {
        const QString suffix = editor->isUntitled()
                                   ? QString() : QFileInfo(editor->filePath()).suffix();
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
        // 反覆關閉最左（index 0）pivot 次；每關一次樞紐左移一格
        for (int n = 0; n < pivot; ++n) {
            const int before = w->count();
            closeTabIn(w, 0);
            if (w->count() == before)   // 使用者於存檔提示取消
                return;
        }
    } else {
        // 反覆關閉樞紐右側第一個（pivot+1）直到沒有右側分頁
        while (w->count() > pivot + 1) {
            const int before = w->count();
            closeTabIn(w, pivot + 1);
            if (w->count() == before)
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


// 建構編輯區右鍵選單本體（複刻 Notepad++ contextMenu.xml）。各項作用於 ed 本身，
// enable 狀態依 ed 當下狀態決定，故可於單元測試中直接斷言項目與啟用狀態。
QMenu *MainWindow::buildEditorContextMenu(EditorWidget *ed, QWidget *parent)
{
    const bool hasSel = ed->hasSelectedText();
    const bool hasFile = !ed->isUntitled();
    const bool ro = ed->isReadOnly();

    auto *menu = new QMenu(parent);

    // --- 復原 / 重做 ---
    menu->addAction(tr("Undo"), this, [ed] { ed->undo(); })->setEnabled(ed->isUndoAvailable());
    menu->addAction(tr("Redo"), this, [ed] { ed->redo(); })->setEnabled(ed->isRedoAvailable());
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
    copyMenu->addAction(tr("Copy Full File Path"), this,
                        [ed] { QApplication::clipboard()->setText(ed->filePath()); });
    copyMenu->addAction(tr("Copy File Name"), this, [ed] {
        QApplication::clipboard()->setText(QFileInfo(ed->filePath()).fileName());
    });
    copyMenu->addAction(tr("Copy Directory Path"), this, [ed] {
        QApplication::clipboard()->setText(QFileInfo(ed->filePath()).absolutePath());
    });

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
    onSelMenu->addAction(tr("Open Selected File"), this, [this, ed] {
        const QString sel = ed->selectedText().trimmed();
        if (sel.isEmpty())
            return;
        QString path = sel;
        if (QFileInfo(path).isRelative() && !ed->isUntitled())
            path = QFileInfo(QDir(QFileInfo(ed->filePath()).absolutePath()), path).absoluteFilePath();
        if (QFileInfo::exists(path))
            openFile(path);
        else
            QDesktopServices::openUrl(QUrl::fromUserInput(sel));
    });
    onSelMenu->addAction(tr("Search on Internet"), this, [ed] {
        const QString sel = ed->selectedText().trimmed();
        if (sel.isEmpty())
            return;
        QString tmpl = macpad::persistence::SettingsStore::load().searchEngineUrl;
        if (tmpl.isEmpty())
            tmpl = QStringLiteral("https://www.google.com/search?q=%1");
        const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(sel));
        const QString url = tmpl.contains(QStringLiteral("%1")) ? tmpl.arg(encoded) : tmpl + encoded;
        QDesktopServices::openUrl(QUrl(url));
    });
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


EditorWidget *MainWindow::addEditorTab()
{
    // 新分頁一律加入作用中檢視（單一檢視時即 m_tabs，行為與改動前一致）
    QTabWidget *w = currentTabWidget();
    auto *pane = new EditorPane(w);
    EditorWidget *editor = pane->primary();

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
        return;
    }
    const QString path = e->filePath();
    if (m_monitored.contains(path)) {
        m_monitored.remove(path);
        e->setReadOnly(false);
        statusBar()->showMessage(tr("已停止監控：%1").arg(QFileInfo(path).fileName()), 3000);
    } else {
        m_monitored.insert(path);
        watchPath(path);
        e->setReadOnly(true);         // 監控時唯讀（如 tail -f）
        e->setCursorPosition(e->lines() - 1, 0);
        e->ensureLineVisible(e->lines() - 1);
        statusBar()->showMessage(tr("監控中（tail -f）：%1").arg(QFileInfo(path).fileName()), 3000);
    }
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
            w->setTabText(i, p->tabTitle());              // clone 追隨來源檔名
            w->setTabToolTip(i, p->primary()->filePath());
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
