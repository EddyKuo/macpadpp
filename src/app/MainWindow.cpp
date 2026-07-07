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
#include "features/udl/UdlLexer.h"
#include "features/export/HtmlExporter.h"
#include "features/textops/TextOps.h"

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
#include "features/functionlist/FunctionListParser.h"
#include "features/columneditor/ColumnEditor.h"

#include <QCryptographicHash>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolBar>

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
#include <QStatusBar>
#include <QTabWidget>

using macpad::core::EditorWidget;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);           // 分頁拖曳排序（FR-001）
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

    // 分頁右鍵選單：標色 / 唯讀鎖定（FR-001）
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const int idx = m_tabs->tabBar()->tabAt(pos);
        if (idx < 0) return;
        QMenu menu;
        menu.addAction(tr("Set Tab Color…"), this, [this, idx] {
            const QColor c = QColorDialog::getColor(Qt::white, this, tr("Tab Color"));
            if (c.isValid())
                m_tabs->tabBar()->setTabTextColor(idx, c);
        });
        menu.addAction(tr("Clear Tab Color"), this, [this, idx] {
            m_tabs->tabBar()->setTabTextColor(idx, QColor());
        });
        if (EditorWidget *e = editorAt(idx)) {
            QAction *lock = menu.addAction(tr("Read-Only"));
            lock->setCheckable(true);
            lock->setChecked(e->isReadOnly());
            connect(lock, &QAction::toggled, this, [this, idx](bool ro) {
                if (EditorWidget *ed = editorAt(idx)) {
                    ed->setReadOnly(ro);
                    m_tabs->setTabText(idx, (ro ? QStringLiteral("🔒 ") : QString())
                                            + ed->displayName());
                }
            });
        }
        menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        updateStatusBar();
        refreshDocList();
        refreshPanels();
        if (m_findDialog)
            m_findDialog->setEditor(currentEditor());
    });

    // 文件切換清單（FR-026）
    m_docList = new macpad::ui::DocumentListDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_docList);
    m_docList->hide();  // 預設隱藏，由 View 選單開啟
    connect(m_docList, &macpad::ui::DocumentListDock::activated, this, [this](int idx) {
        if (idx >= 0 && idx < m_tabs->count())
            m_tabs->setCurrentIndex(idx);
    });

    // 停靠面板（FR-029）：Function List / Clipboard History / Document Map
    m_funcList = new macpad::ui::FunctionListDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_funcList);
    m_funcList->hide();
    connect(m_funcList, &macpad::ui::FunctionListDock::symbolActivated, this, [this](int line) {
        if (EditorWidget *e = currentEditor()) {
            e->setCursorPosition(qMax(0, line - 1), 0);
            e->ensureLineVisible(qMax(0, line - 1));
            e->setFocus();
        }
    });

    m_clipHistory = new macpad::ui::ClipboardHistoryDock(this);
    addDockWidget(Qt::RightDockWidgetArea, m_clipHistory);
    m_clipHistory->hide();
    connect(m_clipHistory, &macpad::ui::ClipboardHistoryDock::pasteRequested, this,
            [this](const QString &text) {
        if (EditorWidget *e = currentEditor())
            e->insert(text);
    });

    m_docMap = new macpad::ui::DocumentMapDock(this);
    addDockWidget(Qt::RightDockWidgetArea, m_docMap);
    m_docMap->hide();
    connect(m_docMap, &macpad::ui::DocumentMapDock::lineClicked, this, [this](int line) {
        if (EditorWidget *e = currentEditor())
            e->ensureLineVisible(line);
    });

    // Folder as Workspace（FR-030）
    m_workspace = new macpad::ui::WorkspaceDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_workspace);
    m_workspace->hide();
    connect(m_workspace, &macpad::ui::WorkspaceDock::fileActivated,
            this, &MainWindow::openFile);

    // Character Panel（Edit ▸ Character Panel，Notepad++ 對等）
    m_charPanel = new macpad::ui::CharacterPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_charPanel);
    m_charPanel->hide();
    connect(m_charPanel, &macpad::ui::CharacterPanel::charChosen, this,
            [this](const QString &text) {
        if (EditorWidget *e = currentEditor()) {
            e->insert(text);
            int line = 0, col = 0;
            e->getCursorPosition(&line, &col);
            e->setCursorPosition(line, col + int(text.size()));
            e->setFocus();
        }
    });

    // 工具列（常用動作）——於 createMenus 前建立，讓 View 選單可加入其開關
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setMovable(false);
    m_toolbar->addAction(tr("New"), this, &MainWindow::newFile);
    m_toolbar->addAction(tr("Open"), this, &MainWindow::openFileDialog);
    m_toolbar->addAction(tr("Save"), this, [this] { saveCurrent(); });
    m_toolbar->addSeparator();
    m_toolbar->addAction(tr("Find"), this, &MainWindow::showFind);
    m_toolbar->addAction(tr("Replace"), this, &MainWindow::showReplace);
    // Mac 慣例：預設隱藏工具列（這些動作已在頂端選單列 File/Edit 中）；
    // 需要的人可從 View ▸ Show Toolbar 開啟。
    m_toolbar->hide();

    createMenus();

    // 狀態列（FR-022）——複刻 Notepad++ 的分格式狀態列（6 格，各格下凹分隔）
    createStatusCells();

    // 外部檔案異動監控（FR-018, IR-001）
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onFileChangedOnDisk);

    // 深色模式跟隨系統（FR-021, IR-002）——訂閱系統外觀變更
    if (QStyleHints *h = QApplication::styleHints()) {
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyTheme(); });
    }

    resize(1000, 720);

    m_udl.loadAll();  // 載入使用者自訂語言（FR-032）

    // 還原上次 session（FR-016）；若無則開空白分頁
    const auto settings = macpad::persistence::SettingsStore::load();
    if (settings.restoreOnLaunch)
        restoreSession();
    if (m_tabs->count() == 0)
        newFile();
    rebuildRecentMenu();
    applyTheme();  // 還原後統一套用主題（含 lexer paper）

    // 擴充協定 dogfood（FR-035）：內建 WordCount 擴充透過協定掛載，驗證可擴充性
    m_extensions = std::make_unique<macpad::extension::ExtensionRegistry>(this);
    m_extensions->load(std::make_unique<macpad::extension::WordCountExtension>());
    m_extensions->load(std::make_unique<macpad::extension::MarkdownPreviewExtension>());

    // 自動儲存（FR-015）：預設關閉；啟用時定期存已命名之未存分頁
    if (settings.autosaveEnabled) {
        auto *timer = new QTimer(this);
        timer->setInterval(settings.autosaveIntervalSec * 1000);
        connect(timer, &QTimer::timeout, this, [this] {
            for (int i = 0; i < m_tabs->count(); ++i) {
                EditorWidget *e = editorAt(i);
                if (e && !e->isUntitled() && e->isDirty())
                    e->saveFile(e->filePath());
            }
            updateTabTitle();
        });
        timer->start();
    }
}

MainWindow::~MainWindow() = default;

// --- IHostServices（extension protocol，FR-035）--------------------------

EditorWidget *MainWindow::activeEditor()
{
    return currentEditor();
}

void MainWindow::addMenuAction(const QString &menuTitle, const QString &text,
                               std::function<void()> callback)
{
    // 依標題找既有選單（去除 & 助記符）；找不到則新建
    QMenu *target = nullptr;
    const auto menus = menuBar()->findChildren<QMenu *>();
    for (QMenu *m : menus) {
        if (QString(m->title()).remove(QLatin1Char('&')) == menuTitle) {
            target = m;
            break;
        }
    }
    if (!target)
        target = menuBar()->addMenu(menuTitle);
    target->addAction(text, this, [cb = std::move(callback)] { cb(); });
}

void MainWindow::showStatusMessage(const QString &message, int timeoutMs)
{
    statusBar()->showMessage(message, timeoutMs);
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
        if (m_tabs->count() > 0)
            closeTab(m_tabs->currentIndex());
    });
    fileMenu->addAction(tr("Close All"), this, [this] { closeAllTabs(); });
    fileMenu->addAction(tr("Close All but This"), this, [this] { closeAllButCurrent(); });
    fileMenu->addAction(tr("Restore Recent Closed File"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), this,
                        [this] { restoreClosedTab(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Move to Recycle Bin"), this, [this] { moveCurrentToTrash(); });

    // Edit 選單（QScintilla 內建 undo/redo/剪貼；多游標為 Cmd+Click，FR-005）
    editMenu->addAction(tr("Undo"), QKeySequence::Undo, this, [this] {
        if (auto *e = currentEditor()) e->undo();
    });
    editMenu->addAction(tr("Redo"), QKeySequence::Redo, this, [this] {
        if (auto *e = currentEditor()) e->redo();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Cut"), QKeySequence::Cut, this, [this] {
        if (auto *e = currentEditor()) e->cut();
    });
    editMenu->addAction(tr("Copy"), QKeySequence::Copy, this, [this] {
        if (auto *e = currentEditor()) e->copy();
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
    editMenu->addAction(tr("Go to Line…"), QKeySequence(Qt::CTRL | Qt::Key_L), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e) return;
        bool ok = false;
        const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line:"),
                                              1, 1, e->lines(), 1, &ok);
        if (ok) {
            e->setCursorPosition(line - 1, 0);
            e->ensureLineVisible(line - 1);
            e->setFocus();
        }
    });
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

    createSearchMenu(searchMenu);  // Notepad++ Search 選單（Find Next/Prev、Mark、書籤進階等）

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
        encMenu->addAction(tr(e.name), this, [this, enc] {
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

    QMenu *eolMenu = formatMenu->addMenu(tr("End of Line"));
    const struct { const char *name; Eol eol; } eols[] = {
        {"Unix (LF)", Eol::Lf}, {"Windows (CRLF)", Eol::CrLf}, {"Classic Mac (CR)", Eol::Cr},
    };
    for (const auto &e : eols) {
        const Eol eol = e.eol;
        eolMenu->addAction(tr(e.name), this, [this, eol] {
            if (auto *ed = currentEditor()) ed->convertEol(eol);
        });
    }

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

    // Settings 選單（複刻 Notepad++ Settings）：Style Configurator + Shortcut Mapper
    settingsMenu->addAction(tr("Style Configurator…"), this, [this] {
        macpad::ui::StyleConfiguratorDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            applyTheme();  // 重建 lexer → 套主題 → 疊使用者覆寫
            statusBar()->showMessage(tr("樣式已更新"), 3000);
        }
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
            for (int i = 0; i < m_tabs->count(); ++i)
                if (EditorWidget *e = editorAt(i)) e->setTabWidth(s.tabWidth);
            applyTheme();
            statusBar()->showMessage(tr("偏好設定已儲存（部分設定重啟後生效）"), 4000);
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

    // Language 選單：手動選語法高亮（FR-003）+ UDL（FR-032）

    // 手動選擇語言：設定目前編輯器的 lexer（覆寫副檔名自動判斷）
    QMenu *setLangMenu = langMenu->addMenu(tr("Set Language"));
    for (const auto &lang : macpad::core::LexerFactory::languages()) {
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
            vars.currentWord = e->hasSelectedText() ? e->selectedText() : e->wordAtLineIndex(0, 0);
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
        const QString path = macpad::persistence::AppPaths::filePath(
            QStringLiteral("run_commands.json"));
        QJsonObject o = macpad::persistence::JsonFile::load(path);
        o.insert(name, cmd);
        macpad::persistence::JsonFile::save(path, o);
        statusBar()->showMessage(tr("命令已儲存：%1").arg(name), 3000);
    });
    QMenu *savedRunMenu = runMenu->addMenu(tr("Saved Commands"));
    connect(savedRunMenu, &QMenu::aboutToShow, this, [this, savedRunMenu] {
        savedRunMenu->clear();
        const QString path = macpad::persistence::AppPaths::filePath(
            QStringLiteral("run_commands.json"));
        const QJsonObject o = macpad::persistence::JsonFile::load(path);
        if (o.isEmpty()) {
            savedRunMenu->addAction(tr("(無已儲存命令)"))->setEnabled(false);
            return;
        }
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString name = it.key();
            const QString cmd = it.value().toString();
            savedRunMenu->addAction(name, this, [this, cmd] {
                if (!m_runDock) {
                    m_runDock = new macpad::features::RunDock(this);
                    addDockWidget(Qt::BottomDockWidgetArea, m_runDock);
                }
                macpad::features::RunVars vars;
                if (EditorWidget *e = currentEditor(); e && !e->isUntitled()) {
                    const QFileInfo fi(e->filePath());
                    vars.fullCurrentPath = fi.absoluteFilePath();
                    vars.currentDirectory = fi.absolutePath();
                    vars.fileName = fi.fileName();
                    vars.nameNoExt = fi.completeBaseName();
                    vars.currentWord = e->hasSelectedText() ? e->selectedText() : QString();
                }
                m_runDock->setVars(vars);
                m_runDock->show();
                m_runDock->raise();
                m_runDock->runCommand(cmd);
            });
        }
    });

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
    wrapAct->setCheckable(true);
    connect(wrapAct, &QAction::toggled, this, [this](bool on) {
        m_wordWrap = on;
        for (int i = 0; i < m_tabs->count(); ++i) applyViewPrefs(editorAt(i));
    });
    QAction *wsAct = viewMenu->addAction(tr("Show Whitespace"));
    wsAct->setCheckable(true);
    connect(wsAct, &QAction::toggled, this, [this](bool on) {
        m_showWhitespace = on;
        for (int i = 0; i < m_tabs->count(); ++i) applyViewPrefs(editorAt(i));
    });
    QAction *eolAct = viewMenu->addAction(tr("Show End of Line"));
    eolAct->setCheckable(true);
    connect(eolAct, &QAction::toggled, this, [this](bool on) {
        m_showEol = on;
        for (int i = 0; i < m_tabs->count(); ++i) applyViewPrefs(editorAt(i));
    });
    QAction *igAct = viewMenu->addAction(tr("Show Indent Guide"));
    igAct->setCheckable(true);
    igAct->setChecked(true);
    connect(igAct, &QAction::toggled, this, [this](bool on) {
        m_showIndentGuide = on;
        for (int i = 0; i < m_tabs->count(); ++i) applyViewPrefs(editorAt(i));
    });
    QAction *wrapSymAct = viewMenu->addAction(tr("Show Wrap Symbol"));
    wrapSymAct->setCheckable(true);
    connect(wrapSymAct, &QAction::toggled, this, [this](bool on) {
        m_showWrapSymbol = on;
        for (int i = 0; i < m_tabs->count(); ++i) applyViewPrefs(editorAt(i));
    });
    viewMenu->addAction(tr("Show All Characters"), this, [wsAct, eolAct] {
        // 一鍵開啟空白＋EOL 顯示
        wsAct->setChecked(true);
        eolAct->setChecked(true);
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
    tabMenu->addAction(tr("First Tab"), this, [this] { if (m_tabs->count()) m_tabs->setCurrentIndex(0); });
    tabMenu->addAction(tr("Last Tab"), this, [this] { if (m_tabs->count()) m_tabs->setCurrentIndex(m_tabs->count() - 1); });
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
    viewMenu->addAction(tr("Toggle Split"), QKeySequence(Qt::CTRL | Qt::Key_Backslash),
                        this, &MainWindow::toggleSplit);
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

    // Window 選單（Notepad++ Window）——列出所有開啟文件並可切換（選單已於函式開頭建立）
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::buildWindowMenu);

    // 套用使用者在 Shortcut Mapper 儲存的快捷鍵覆寫
    QList<QAction *> allActs;
    for (QAction *a : menuBar()->findChildren<QAction *>())
        if (!a->menu() && !a->isSeparator() && !a->text().isEmpty())
            allActs.append(a);
    macpad::ui::ShortcutMapperDialog::applySavedOverrides(allActs);
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
        showFullScreen();
    } else {
        for (QDockWidget *d : m_dfHidden)
            d->show();
        m_dfHidden.clear();
        statusBar()->show();
        m_tabs->tabBar()->show();
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
        setWindowFlag(Qt::FramelessWindowHint, false);
        setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
        show();
    }
}

void MainWindow::showIncrementalSearch()
{
    if (!m_incBar) {
        m_incBar = new QToolBar(tr("Incremental Search"), this);
        m_incBar->setMovable(false);
        m_incSearch = new QLineEdit(m_incBar);
        m_incSearch->setPlaceholderText(tr("Incremental search…"));
        m_incSearch->setMaximumWidth(320);
        m_incBar->addWidget(new QLabel(tr("  Find: "), m_incBar));
        m_incBar->addWidget(m_incSearch);
        auto *closeBtn = m_incBar->addAction(tr("✕"));
        addToolBar(Qt::BottomToolBarArea, m_incBar);

        // 邊打邊找：從目前游標位置往後搜尋
        connect(m_incSearch, &QLineEdit::textChanged, this, [this](const QString &q) {
            EditorWidget *e = currentEditor();
            if (!e || q.isEmpty())
                return;
            int line = 0, idx = 0;
            e->getCursorPosition(&line, &idx);
            e->findFirst(q, false, false, false, true, true, line, idx);
        });
        // Enter 找下一個
        connect(m_incSearch, &QLineEdit::returnPressed, this, [this] {
            if (EditorWidget *e = currentEditor()) e->findNext();
        });
        connect(closeBtn, &QAction::triggered, this, [this] {
            m_incBar->hide();
            if (EditorWidget *e = currentEditor()) e->setFocus();
        });
    }
    m_incBar->show();
    m_incSearch->setFocus();
    m_incSearch->selectAll();
}

void MainWindow::viewCurrentFileInBrowser(const QString &appName)
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled()) {
        statusBar()->showMessage(tr("請先存檔再於瀏覽器開啟"), 3000);
        return;
    }
    const QString path = e->filePath();
    if (appName.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else {
        // macOS：open -a "<App>" <file>
        QProcess::startDetached(QStringLiteral("open"),
                                {QStringLiteral("-a"), appName, path});
    }
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
    // 開啟中的文件清單（打勾標示目前分頁）
    for (int i = 0; i < m_tabs->count(); ++i) {
        QAction *act = m_windowMenu->addAction(m_tabs->tabText(i));
        act->setCheckable(true);
        act->setChecked(i == m_tabs->currentIndex());
        connect(act, &QAction::triggered, this, [this, i] { m_tabs->setCurrentIndex(i); });
    }
}

void MainWindow::toggleSplit()
{
    if (EditorPane *pane = currentPane())
        pane->toggleSplit();  // FR-002
}

// --- 巨集錄製/播放（FR-028）---------------------------------------------

void MainWindow::startMacroRecording()
{
    EditorWidget *editor = currentEditor();
    if (!editor || m_recordingMacro)
        return;
    m_recordingMacro = new QsciMacro(editor);
    m_recordingMacro->startRecording();
    m_recordAction->setEnabled(false);
    m_stopAction->setEnabled(true);
    statusBar()->showMessage(tr("巨集錄製中…"), 0);
}

void MainWindow::stopMacroRecording()
{
    if (!m_recordingMacro)
        return;
    m_recordingMacro->endRecording();
    m_savedMacro = m_recordingMacro->save();
    delete m_recordingMacro;
    m_recordingMacro = nullptr;
    m_recordAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_playAction->setEnabled(!m_savedMacro.isEmpty());
    statusBar()->showMessage(tr("巨集已錄製"), 3000);
}

void MainWindow::playMacro()
{
    EditorWidget *editor = currentEditor();
    if (!editor || m_savedMacro.isEmpty())
        return;
    QsciMacro macro(editor);
    macro.load(m_savedMacro);
    macro.play();
}

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
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (EditorWidget *e = editorAt(i))
            e->reapplyLexer();
    }
}

void MainWindow::watchPath(const QString &path)
{
    if (path.isEmpty() || m_watcher->files().contains(path))
        return;
    m_watcher->addPath(path);
}

void MainWindow::onFileChangedOnDisk(const QString &path)
{
    // FR-018：外部異動時提示（僅在視窗作用中避免打擾）
    const int idx = indexOfPath(path);
    if (idx < 0)
        return;
    EditorWidget *editor = editorAt(idx);
    if (!editor)
        return;

    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("檔案已在磁碟上被刪除：%1").arg(path), 5000);
        return;
    }

    // Monitoring（tail -f）：靜默自動重載並捲到檔尾，不打擾
    if (m_monitored.contains(path)) {
        QString err;
        const bool ro = editor->isReadOnly();
        editor->setReadOnly(false);
        editor->loadFile(path, &err);
        editor->setReadOnly(ro);
        editor->setCursorPosition(editor->lines() - 1, 0);
        editor->ensureLineVisible(editor->lines() - 1);
        updateStatusBar();
        watchPath(path);
        return;
    }

    // 檔案被外部修改：詢問是否重新載入
    const auto ret = QMessageBox::question(
        this, tr("檔案已變更"),
        tr("「%1」已被外部程式修改，是否重新載入？\n（未儲存的變更將遺失）")
            .arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        QString err;
        editor->loadFile(path, &err);
        updateTabTitle();
        updateStatusBar();
    }
    // QFileSystemWatcher 於檔案被 rename/replace 後可能移除監看，重新加入
    watchPath(path);
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

void MainWindow::applyTextOp(const std::function<QString(const QString &)> &op)
{
    EditorWidget *e = currentEditor();
    if (!e || e->isReadOnly())
        return;
    e->beginUndoAction();
    if (e->hasSelectedText()) {
        const QByteArray b = op(e->selectedText()).toUtf8();
        e->SendScintilla(QsciScintilla::SCI_REPLACESEL, b.constData());
    } else {
        const QByteArray b = op(e->text()).toUtf8();
        e->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, 0UL);
        e->SendScintilla(QsciScintilla::SCI_SETTARGETEND,
                         static_cast<unsigned long>(e->length()));
        e->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                         static_cast<unsigned long>(b.size()), b.constData());
    }
    e->endUndoAction();
}

void MainWindow::moveCurrentLines(bool up)
{
    EditorWidget *e = currentEditor();
    if (!e || e->isReadOnly())
        return;
    int lineFrom = 0, idxFrom = 0, lineTo = 0, idxTo = 0;
    if (e->hasSelectedText())
        e->getSelection(&lineFrom, &idxFrom, &lineTo, &idxTo);
    else
        e->getCursorPosition(&lineFrom, &idxFrom), lineTo = lineFrom;

    int newFirst = lineFrom;
    const QString whole = e->text();
    const QString out = up ? macpad::features::TextOps::moveLinesUp(whole, lineFrom, lineTo, &newFirst)
                           : macpad::features::TextOps::moveLinesDown(whole, lineFrom, lineTo, &newFirst);
    if (out == whole)
        return;
    e->beginUndoAction();
    const QByteArray b = out.toUtf8();
    e->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, 0UL);
    e->SendScintilla(QsciScintilla::SCI_SETTARGETEND, static_cast<unsigned long>(e->length()));
    e->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                     static_cast<unsigned long>(b.size()), b.constData());
    e->endUndoAction();
    e->setCursorPosition(newFirst, 0);
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
    caseMenu->addAction(tr("iNVERT cASE"), this, [this] { applyTextOp(&TextOps::invertCase); });

    // 行操作
    QMenu *lineMenu = editMenu->addMenu(tr("Line Operations"));
    lineMenu->addAction(tr("Sort Ascending"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesAscending(s); }); });
    lineMenu->addAction(tr("Sort Descending"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesDescending(s); }); });
    lineMenu->addAction(tr("Sort Numeric"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::sortLinesNumeric(s); }); });
    lineMenu->addAction(tr("Remove Duplicate Lines"), this,
                        [this] { applyTextOp(&TextOps::removeDuplicateLines); });
    lineMenu->addAction(tr("Remove Empty Lines"), this,
                        [this] { applyTextOp([](const QString &s){ return TextOps::removeEmptyLines(s); }); });
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
        const QString out = macpad::features::ColumnEditor::insertNumberColumn(
            e->text(), lineFrom, lineTo, col, dlg.spec());
        e->beginUndoAction();
        const QByteArray b = out.toUtf8();
        e->SendScintilla(QsciScintilla::SCI_SETTARGETSTART, 0UL);
        e->SendScintilla(QsciScintilla::SCI_SETTARGETEND, static_cast<unsigned long>(e->length()));
        e->SendScintilla(QsciScintilla::SCI_REPLACETARGET,
                         static_cast<unsigned long>(b.size()), b.constData());
        e->endUndoAction();
    });

    // 插入
    editMenu->addAction(tr("Insert Date/Time"), this, [this] {
        if (EditorWidget *e = currentEditor())
            e->insert(QDateTime::currentDateTime().toString(Qt::ISODate));
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

    // 自動完成（從文件既有字詞）
    editMenu->addAction(tr("Word Completion"), QKeySequence(Qt::CTRL | Qt::Key_Space), this,
                        [this] { if (auto *e = currentEditor()) e->autoCompleteFromDocument(); });

    editMenu->addSeparator();

    // 唯讀切換
    QAction *roAct = editMenu->addAction(tr("Read-Only"));
    roAct->setCheckable(true);
    connect(roAct, &QAction::toggled, this, [this](bool on) {
        if (auto *e = currentEditor()) e->setReadOnly(on);
    });
    // 切換分頁時同步唯讀勾選狀態
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, roAct] {
        if (auto *e = currentEditor()) {
            QSignalBlocker b(roAct);
            roAct->setChecked(e->isReadOnly());
        }
    });

    // Character Panel（開啟/聚焦 ASCII 插入面板）
    editMenu->addAction(tr("Character Panel"), this, [this] {
        if (m_charPanel) { m_charPanel->show(); m_charPanel->raise(); }
    });
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
    searchMenu->addAction(tr("Replace…"), QKeySequence::Replace, this, &MainWindow::showReplace);
    searchMenu->addAction(tr("Incremental Search"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I),
                          this, [this] { showIncrementalSearch(); });
    searchMenu->addSeparator();
    searchMenu->addAction(tr("Go to…"), QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] {
        EditorWidget *e = currentEditor();
        if (!e) return;
        bool ok = false;
        const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line:"),
                                              1, 1, e->lines(), 1, &ok);
        if (ok) { e->setCursorPosition(line - 1, 0); e->ensureLineVisible(line - 1); e->setFocus(); }
    });
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
}

void MainWindow::showFind()
{
    if (!m_findDialog)
        m_findDialog = new macpad::features::FindReplaceDialog(this);
    m_findDialog->setEditor(currentEditor());
    m_findDialog->showFind(/*replaceMode=*/false);
}

void MainWindow::showReplace()
{
    if (!m_findDialog)
        m_findDialog = new macpad::features::FindReplaceDialog(this);
    m_findDialog->setEditor(currentEditor());
    m_findDialog->showFind(/*replaceMode=*/true);
}

// ===== Notepad++ 對等：檔案操作 =====

void MainWindow::reloadFromDisk()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled())
        return;
    if (e->isDirty()) {
        const auto ret = QMessageBox::question(this, tr("Reload from Disk"),
            tr("「%1」有未儲存的變更，重新載入將捨棄它們，確定？")
                .arg(QFileInfo(e->filePath()).fileName()),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }
    QString err;
    if (e->loadFile(e->filePath(), &err)) {
        updateTabTitle();
        updateStatusBar();
        statusBar()->showMessage(tr("已重新載入"), 2000);
    } else {
        QMessageBox::warning(this, tr("Reload from Disk"), err);
    }
}

void MainWindow::saveAll()
{
    const int cur = m_tabs->currentIndex();
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorWidget *e = editorAt(i);
        if (e && e->isDirty()) {
            m_tabs->setCurrentIndex(i);
            saveCurrent();  // 未命名檔會轉為 Save As
        }
    }
    m_tabs->setCurrentIndex(cur);
    statusBar()->showMessage(tr("已全部儲存"), 2000);
}

void MainWindow::saveCopyAs()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save a Copy As"),
                                                      e->isUntitled() ? QString() : e->filePath());
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(macpad::core::FileEncoding::encode(e->text(), e->encoding()));
        statusBar()->showMessage(tr("副本已儲存：%1").arg(path), 3000);
    } else {
        QMessageBox::warning(this, tr("Save a Copy As"), tr("無法寫入：%1").arg(path));
    }
}

void MainWindow::renameCurrentFile()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    if (e->isUntitled()) {          // 尚未存檔 → 等同 Save As
        saveCurrentAs();
        return;
    }
    const QString oldPath = e->filePath();
    const QString newPath = QFileDialog::getSaveFileName(this, tr("Rename"), oldPath);
    if (newPath.isEmpty() || newPath == oldPath)
        return;
    QString err;
    if (!e->saveFile(newPath, &err)) {   // 以新名寫入（含未存變更）
        QMessageBox::warning(this, tr("Rename"), err);
        return;
    }
    QFile::remove(oldPath);              // 刪除舊檔完成改名
    watchPath(newPath);
    updateTabTitle();
    updateStatusBar();
    statusBar()->showMessage(tr("已改名為：%1").arg(QFileInfo(newPath).fileName()), 3000);
}

void MainWindow::closeAllTabs()
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        const int before = m_tabs->count();
        m_tabs->setCurrentIndex(i);
        closeTab(i);
        if (m_tabs->count() == before)   // 使用者取消
            return;
    }
}

void MainWindow::closeAllButCurrent()
{
    EditorWidget *keep = currentEditor();
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        if (editorAt(i) == keep)
            continue;
        const int before = m_tabs->count();
        closeTab(i);
        if (m_tabs->count() == before)
            return;
    }
}

void MainWindow::moveCurrentToTrash()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled()) {
        QMessageBox::information(this, tr("Move to Recycle Bin"), tr("此分頁尚未存成檔案。"));
        return;
    }
    const QString path = e->filePath();
    const auto ret = QMessageBox::question(this, tr("Move to Recycle Bin"),
        tr("要把「%1」移到垃圾桶嗎？").arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Move to Recycle Bin"), tr("無法移到垃圾桶。"));
        return;
    }
    // 檔案已不在磁碟 → 直接關閉分頁（不再提示存檔）
    const int idx = m_tabs->currentIndex();
    EditorPane *pane = paneAt(idx);
    m_tabs->removeTab(idx);
    if (pane)
        pane->deleteLater();
    if (m_tabs->count() == 0)
        newFile();
}

void MainWindow::restoreClosedTab()
{
    if (m_closedFiles.isEmpty()) {
        statusBar()->showMessage(tr("沒有最近關閉的檔案"), 2000);
        return;
    }
    const QString path = m_closedFiles.takeLast();
    openFile(path);
}

void MainWindow::revealInFinder()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled())
        return;
    QProcess::startDetached(QStringLiteral("open"),
                            {QStringLiteral("-R"), e->filePath()});
}

void MainWindow::openInDefaultApp()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(e->filePath()));
}

// ===== Notepad++ 對等：搜尋 =====

void MainWindow::findNextDir(bool forward)
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    QString q = m_lastFindText;
    if (q.isEmpty())
        q = e->selectedText();
    if (q.isEmpty())
        return;
    m_lastFindText = q;
    e->findFirst(q, /*re=*/false, /*cs=*/false, /*wo=*/false, /*wrap=*/true, forward);
}

void MainWindow::selectAndFind(bool forward)
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    const QString q = e->selectedText();
    if (q.isEmpty())
        return;
    m_lastFindText = q;
    e->findFirst(q, false, false, false, true, forward);
}

void MainWindow::markSelectionOccurrences()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    const QString q = e->selectedText();
    if (q.isEmpty())
        return;
    const int n = e->markAll(q, false, false, false);
    statusBar()->showMessage(tr("已標記 %1 處").arg(n), 2000);
}

void MainWindow::clearAllMarks()
{
    if (EditorWidget *e = currentEditor())
        e->clearMarks();
}

// ===== Notepad++ 對等：檢視 / 分頁 =====

void MainWindow::toggleAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();  // 變更 flag 後須重新 show
}

void MainWindow::activateTabRelative(int delta)
{
    const int n = m_tabs->count();
    if (n <= 0)
        return;
    m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + n) % n);
}

void MainWindow::moveCurrentTab(int delta)
{
    const int idx = m_tabs->currentIndex();
    const int dst = idx + delta;
    if (idx < 0 || dst < 0 || dst >= m_tabs->count())
        return;
    m_tabs->tabBar()->moveTab(idx, dst);
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

EditorPane *MainWindow::currentPane() const
{
    return qobject_cast<EditorPane *>(m_tabs->currentWidget());
}

EditorPane *MainWindow::paneAt(int index) const
{
    return qobject_cast<EditorPane *>(m_tabs->widget(index));
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

EditorWidget *MainWindow::addEditorTab()
{
    auto *pane = new EditorPane(m_tabs);
    EditorWidget *editor = pane->primary();

    connect(editor, &EditorWidget::dirtyChanged, this, &MainWindow::updateTabTitle);
    connect(editor, &EditorWidget::metaChanged, this, &MainWindow::updateStatusBar);
    connect(editor, &EditorWidget::lexerChanged, this, [this, editor] { themeEditor(editor); });
    connect(editor, &QsciScintilla::cursorPositionChanged, this,
            [this](int, int) { updateStatusBar(); });
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

    themeEditor(editor);
    applyViewPrefs(editor);

    const int idx = m_tabs->addTab(pane, editor->displayName());
    m_tabs->setCurrentIndex(idx);
    return editor;
}

void MainWindow::newFile()
{
    addEditorTab();
    updateStatusBar();
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open File"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openFile(const QString &path)
{
    const QString absPath = QFileInfo(path).absoluteFilePath();

    // 已開啟則聚焦既有分頁（FR-001 邊界：不重複開）
    const int existing = indexOfPath(absPath);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    // 若目前分頁是空白未動的 Untitled，直接沿用它
    EditorWidget *editor = currentEditor();
    if (!editor || !editor->isUntitled() || editor->isDirty())
        editor = addEditorTab();

    QString err;
    if (!editor->loadFile(absPath, &err)) {
        QMessageBox::warning(this, tr("Open Failed"),
                             tr("無法開啟 %1：\n%2").arg(absPath, err));
        return;
    }
    // UDL：若副檔名匹配已載入的使用者自訂語言，套用 UDL lexer（FR-032）
    if (const auto *def = m_udl.findForExtension(QFileInfo(absPath).suffix()))
        editor->setLexer(new macpad::features::UdlLexer(*def, editor));

    macpad::persistence::RecentFiles::add(absPath);  // FR-017
    watchPath(absPath);                              // FR-018
    rebuildRecentMenu();
    updateTabTitle();
    updateStatusBar();
    refreshPanels();                                 // FR-029
}

void MainWindow::openFileAtLine(const QString &path, int line, int column)
{
    openFile(path);
    if (EditorWidget *e = currentEditor()) {
        e->setCursorPosition(qMax(0, line - 1), qMax(0, column - 1));
        e->ensureLineVisible(qMax(0, line - 1));
        e->setFocus();
    }
}

bool MainWindow::saveCurrent()
{
    EditorWidget *editor = currentEditor();
    if (!editor)
        return false;
    if (editor->isUntitled())
        return saveCurrentAs();

    QString err;
    if (!editor->saveFile(editor->filePath(), &err)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("無法儲存 %1：\n%2").arg(editor->filePath(), err));
        return false;
    }
    updateTabTitle();
    return true;
}

bool MainWindow::saveCurrentAs()
{
    EditorWidget *editor = currentEditor();
    if (!editor)
        return false;

    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                      editor->filePath());
    if (path.isEmpty())
        return false;

    QString err;
    if (!editor->saveFile(path, &err)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("無法儲存 %1：\n%2").arg(path, err));
        return false;
    }
    updateTabTitle();
    return true;
}

bool MainWindow::maybeSave(EditorWidget *editor)
{
    if (!editor || !editor->isDirty())
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("macpad++"),
        tr("「%1」有未儲存的變更，是否儲存？").arg(editor->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    switch (ret) {
    case QMessageBox::Save:
        m_tabs->setCurrentWidget(editor);
        return saveCurrent();
    case QMessageBox::Discard:
        return true;
    default:
        return false;  // Cancel
    }
}

void MainWindow::closeTab(int index)
{
    EditorPane *pane = paneAt(index);
    EditorWidget *ed = pane ? pane->primary() : nullptr;
    if (!maybeSave(ed))
        return;

    // 記住剛關閉的檔案，供 Restore Recent Closed File
    if (ed && !ed->isUntitled()) {
        m_closedFiles.removeAll(ed->filePath());
        m_closedFiles.append(ed->filePath());
        if (m_closedFiles.size() > 20)
            m_closedFiles.removeFirst();
    }

    m_tabs->removeTab(index);
    if (pane)
        pane->deleteLater();

    // 0 分頁時給一個空白分頁（避免空視窗；歡迎頁最小版）
    if (m_tabs->count() == 0)
        newFile();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 逐一確認未存分頁（FR-001 AC2）
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!maybeSave(editorAt(i))) {
            event->ignore();
            return;
        }
    }
    saveSession();  // FR-016：關閉前保存 session
    event->accept();
}

// --- Session / Recent（FR-016/017）--------------------------------------

macpad::persistence::SessionState MainWindow::buildCurrentSession() const
{
    using namespace macpad::persistence;
    SessionState state;
    state.activeIndex = m_tabs->currentIndex();
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorWidget *e = editorAt(i);
        if (!e || e->isUntitled())
            continue;  // 只還原已命名檔（DR-002）
        TabState t;
        t.path = e->filePath();
        int line = 0, index = 0;
        e->getCursorPosition(&line, &index);
        t.line = line;
        t.index = index;
        t.firstVisibleLine = e->firstVisibleLine();
        state.tabs.push_back(t);
    }
    return state;
}

void MainWindow::saveSession()
{
    macpad::persistence::SessionStore::save(buildCurrentSession());
}

void MainWindow::restoreSession()
{
    openSessionState(macpad::persistence::SessionStore::load());
}

void MainWindow::openSessionState(const macpad::persistence::SessionState &state)
{
    using namespace macpad::persistence;
    for (const TabState &t : state.tabs) {
        if (!QFileInfo::exists(t.path))
            continue;  // 檔案已刪除則略過（FR-016 AC2）
        EditorWidget *editor = addEditorTab();
        QString err;
        if (!editor->loadFile(t.path, &err)) {
            // 讀取失敗略過該分頁，不中斷還原
            const int idx = m_tabs->indexOf(editor);
            if (idx >= 0) { m_tabs->removeTab(idx); editor->deleteLater(); }
            continue;
        }
        editor->setCursorPosition(t.line, t.index);
        editor->SendScintilla(EditorWidget::SCI_SETFIRSTVISIBLELINE, t.firstVisibleLine);
    }
    if (state.activeIndex >= 0 && state.activeIndex < m_tabs->count())
        m_tabs->setCurrentIndex(state.activeIndex);
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList recent = macpad::persistence::RecentFiles::load();
    if (recent.isEmpty()) {
        QAction *none = m_recentMenu->addAction(tr("(無最近檔案)"));
        none->setEnabled(false);
        return;
    }
    for (const QString &path : recent) {
        m_recentMenu->addAction(path, this, [this, path] { openFile(path); });
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear Menu"), this, [this] {
        macpad::persistence::RecentFiles::clear();
        rebuildRecentMenu();
    });
}

int MainWindow::indexOfPath(const QString &absPath) const
{
    if (absPath.isEmpty())
        return -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (editorAt(i)->filePath() == absPath)
            return i;
    }
    return -1;
}

void MainWindow::updateTabTitle()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorWidget *e = editorAt(i);
        m_tabs->setTabText(i, e->displayName());
        m_tabs->setTabToolTip(i, e->filePath());
    }
    refreshDocList();
}

void MainWindow::refreshDocList()
{
    if (!m_docList)
        return;
    QStringList names;
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorWidget *e = editorAt(i);
        names << (e ? e->displayName() : tr("Untitled"));
    }
    m_docList->refresh(names, m_tabs->currentIndex());
}

void MainWindow::refreshPanels()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    const QString suffix = e->isUntitled() ? QString() : QFileInfo(e->filePath()).suffix();
    if (m_funcList && m_funcList->isVisible())
        m_funcList->update(e->text(), suffix);
    if (m_docMap && m_docMap->isVisible())
        m_docMap->attach(e);
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
    m_stMode->setText(s.overtype ? QStringLiteral("OVR") : QStringLiteral("INS"));
}
