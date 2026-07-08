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


macpad::persistence::SessionState MainWindow::buildCurrentSession() const
{
    using namespace macpad::persistence;
    // 兩個檢視的已命名檔攤平成單一清單（clone 為 untitled 自動略過，不會重覆）；
    // 每筆記錄所屬檢視（FR-062），還原時歸位。
    SessionState state;
    EditorWidget *active = currentEditor();
    state.activeIndex = 0;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorWidget *e = editorIn(w, i);
            if (!e || e->isUntitled())
                continue;  // 只還原已命名檔（DR-002）；clone 亦為 untitled 故排除
            if (e == active)
                state.activeIndex = int(state.tabs.size());
            TabState t;
            t.path = e->filePath();
            int line = 0, index = 0;
            e->getCursorPosition(&line, &index);
            t.line = line;
            t.index = index;
            t.firstVisibleLine = e->firstVisibleLine();
            // FR-052：選取範圍（無選取則留空字串）
            if (e->hasSelectedText()) {
                int aLine = 0, aIdx = 0, cLine = 0, cIdx = 0;
                e->getSelection(&aLine, &aIdx, &cLine, &cIdx);
                t.selection = QStringLiteral("%1,%2,%3,%4").arg(aLine).arg(aIdx).arg(cLine).arg(cIdx);
            }
            // FR-052：書籤行號
            t.bookmarks = e->bookmarkedLines();
            // FR-052：手動/自動判定出的語言鍵（無對應則留空，還原時以副檔名自動偵測）
            t.languageOverride = languageKeyForLexer(e->lexer());
            t.view = (w == m_tabs2) ? 1 : 0;   // FR-062：記錄所屬檢視
            state.tabs.push_back(t);
        }
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
    QVector<QPair<QTabWidget *, EditorPane *>> restored;  // 依序記錄（供 activeIndex 歸位）
    for (const TabState &t : state.tabs) {
        if (!QFileInfo::exists(t.path))
            continue;  // 檔案已刪除則略過（FR-016 AC2）
        // FR-062：把分頁還原到它原本所屬的檢視；view==1 但第二檢視尚未建立時退回主檢視。
        QTabWidget *target = (t.view == 1 && m_tabs2) ? m_tabs2 : m_tabs;
        setActiveTabWidget(target);         // addEditorTab 會加入作用中檢視
        EditorWidget *editor = addEditorTab();
        QString err;
        if (!editor->loadFile(t.path, &err)) {
            // 讀取失敗略過該分頁，不中斷還原
            const int idx = target->indexOf(target->widget(target->count() - 1));
            if (idx >= 0) { EditorPane *p = paneIn(target, idx); target->removeTab(idx);
                            if (p) p->deleteLater(); }
            continue;
        }
        restored.append({target, paneIn(target, target->count() - 1)});
        // FR-052：先還原書籤（逐行移動游標並切換，載入後全新分頁尚無書籤，切換即新增）
        for (int ln : t.bookmarks) {
            if (ln >= 0 && ln < editor->lines()) {
                editor->setCursorPosition(ln, 0);
                editor->toggleBookmark();
            }
        }
        editor->setCursorPosition(t.line, t.index);
        editor->SendScintilla(EditorWidget::SCI_SETFIRSTVISIBLELINE, t.firstVisibleLine);
        // FR-052：選取範圍
        if (!t.selection.isEmpty()) {
            const QStringList parts = t.selection.split(QLatin1Char(','));
            if (parts.size() == 4) {
                editor->setSelection(parts[0].toInt(), parts[1].toInt(),
                                     parts[2].toInt(), parts[3].toInt());
            }
        }
        // FR-052：語言覆寫（空字串代表沿用副檔名自動偵測，loadFile 已處理）
        if (!t.languageOverride.isEmpty()) {
            if (QsciLexer *lex = macpad::core::LexerFactory::createForLanguage(t.languageOverride, editor))
                editor->setLanguageLexer(lex);
        }
    }
    updateSecondViewVisibility();   // 第二檢視有還原分頁才顯示（FR-062）
    // 作用中分頁歸位：activeIndex 對應 buildCurrentSession 的攤平順序，取還原成功者中的對應項。
    if (!restored.isEmpty()) {
        const int ai = qBound(0, state.activeIndex, int(restored.size()) - 1);
        QTabWidget *w = restored[ai].first;
        setActiveTabWidget(w);
        if (restored[ai].second)
            w->setCurrentIndex(w->indexOf(restored[ai].second));
    }
}


void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    const auto s = macpad::persistence::SettingsStore::load();

    // 先清掉上一輪「直接列於 File 選單」的動作（recentFilesInSubmenu=false 模式）
    for (QAction *a : m_recentDirectActions) {
        if (m_fileMenu)
            m_fileMenu->removeAction(a);
        delete a;
    }
    m_recentDirectActions.clear();
    m_recentMenu->clear();

    // recentFilesMaxEntries=0 → 停用最近檔案追蹤
    if (s.recentFilesMaxEntries <= 0) {
        m_recentMenu->menuAction()->setVisible(true);
        QAction *none = m_recentMenu->addAction(tr("(最近檔案已停用)"));
        none->setEnabled(false);
        return;
    }

    QStringList recent = macpad::persistence::RecentFiles::load();
    if (recent.size() > s.recentFilesMaxEntries)
        recent = recent.mid(0, s.recentFilesMaxEntries);

    const bool inSub = s.recentFilesInSubmenu;
    m_recentMenu->menuAction()->setVisible(inSub);  // false 時隱藏「Open Recent」子選單入口

    const auto label = [&s](const QString &path) {
        return s.recentFilesShowFullPath ? path : QFileInfo(path).fileName();
    };

    if (inSub) {
        if (recent.isEmpty()) {
            QAction *none = m_recentMenu->addAction(tr("(無最近檔案)"));
            none->setEnabled(false);
            return;
        }
        for (const QString &path : recent)
            m_recentMenu->addAction(label(path), this, [this, path] { openFile(path); });
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(tr("Clear Menu"), this, [this] {
            macpad::persistence::RecentFiles::clear();
            rebuildRecentMenu();
        });
        return;
    }

    // 直接列於 File 選單：插入於「Open Recent」子選單入口之後（該入口此時隱藏，僅作定位錨點）
    if (!m_fileMenu || recent.isEmpty())
        return;
    QAction *anchor = m_recentMenu->menuAction();
    for (const QString &path : recent) {
        auto *a = new QAction(label(path), this);
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
        m_fileMenu->insertAction(anchor, a);
        m_recentDirectActions.append(a);
    }
    auto *clear = new QAction(tr("Clear Recent Files"), this);
    connect(clear, &QAction::triggered, this, [this] {
        macpad::persistence::RecentFiles::clear();
        rebuildRecentMenu();
    });
    m_fileMenu->insertAction(anchor, clear);
    m_recentDirectActions.append(clear);
    QAction *sep = m_fileMenu->insertSeparator(anchor);
    m_recentDirectActions.append(sep);
}


int MainWindow::indexOfPath(const QString &absPath) const
{
    if (absPath.isEmpty())
        return -1;
    QTabWidget *w = currentTabWidget();
    for (int i = 0; i < w->count(); ++i) {
        EditorWidget *e = editorIn(w, i);
        if (e && e->filePath() == absPath)
            return i;
    }
    return -1;
}


// 兩個檢視中尋找已開啟的檔案並聚焦（含切換作用中檢視）；回傳是否命中。
bool MainWindow::focusExistingPath(const QString &absPath)
{
    if (absPath.isEmpty())
        return false;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorPane *p = paneIn(w, i);
            if (!p || p->isClone())
                continue;  // clone 不算「已開啟本檔」的正本
            EditorWidget *e = p->primary();
            if (e && !e->isUntitled() && e->filePath() == absPath) {
                setActiveTabWidget(w);
                w->setCurrentIndex(i);
                return true;
            }
        }
    }
    return false;
}


// -openSession <file>：從指定 session 檔還原分頁（供命令列）
void MainWindow::openSessionFile(const QString &path)
{
    if (path.isEmpty())
        return;
    openSessionState(macpad::persistence::SessionStore::loadFromPath(path));
}


void MainWindow::watchPath(const QString &path)
{
    if (path.isEmpty() || m_watcher->files().contains(path))
        return;
    m_watcher->addPath(path);
}


void MainWindow::onFileChangedOnDisk(const QString &path)
{
    // FR-018：外部異動時提示（僅在視窗作用中避免打擾）——兩個檢視都找
    EditorWidget *editor = nullptr;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorWidget *e = editorIn(w, i);
            if (e && !e->isUntitled() && e->filePath() == path) {
                editor = e;
                break;
            }
        }
        if (editor)
            break;
    }
    if (!editor)
        return;

    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("檔案已在磁碟上被刪除：%1").arg(path), 5000);
        return;
    }

    // Monitoring（tail -f）：靜默自動重載並捲到檔尾，不打擾（無論偵測模式為何皆生效）
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

    // 檔案外部變更偵測模式（FR-053）：
    //   New Document 頁 autoDetectFileStatus=false 或 MISC 頁 Disabled → 不處理；
    //   EnabledSilent → 靜默自動重載（不打擾）；Enabled → 詢問使用者。
    using macpad::persistence::FileStatusAutoDetectMode;
    const auto s = macpad::persistence::SettingsStore::load();
    if (!s.autoDetectFileStatus
        || s.fileStatusAutoDetect == FileStatusAutoDetectMode::Disabled) {
        watchPath(path);
        return;
    }
    if (s.fileStatusAutoDetect == FileStatusAutoDetectMode::EnabledSilent) {
        QString err;
        editor->loadFile(path, &err);
        updateTabTitle();
        updateStatusBar();
        watchPath(path);
        return;
    }

    // 檔案被外部修改：詢問是否重新載入（Enabled 模式）
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


void MainWindow::applySnapshotTimerSettings(const macpad::persistence::Settings &s)
{
    if (!m_snapshotTimer)
        return;
    if (s.enableSessionSnapshot) {
        // 夾限區間，防止手改/損毀設定檔導致 0/負值 QTimer 空轉
        const int intervalSec = qBound(5, s.snapshotIntervalSec, 3600);
        m_snapshotTimer->setInterval(intervalSec * 1000);
        m_snapshotTimer->start();
    } else {
        m_snapshotTimer->stop();
    }
}
