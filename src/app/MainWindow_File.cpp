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


// 將設定檔的備份模式（persistence::BackupMode）轉為 BackupService 使用的 features::BackupMode
// （兩者列舉語意一致但屬不同模組，避免 persistence 直接依賴 features）。
static macpad::features::BackupMode toFeatureBackupMode(macpad::persistence::BackupMode m)
{
    using PM = macpad::persistence::BackupMode;
    using FM = macpad::features::BackupMode;
    switch (m) {
    case PM::Simple:  return FM::Simple;
    case PM::Verbose: return FM::Verbose;
    default:          return FM::None;
    }
}


// 儲存成功後的備份（FR-054，best-effort：失敗不阻擋儲存流程）
static void backupAfterSave(const QString &path, const QByteArray &content)
{
    const auto s = macpad::persistence::SettingsStore::load();
    if (s.backupMode == macpad::persistence::BackupMode::None)
        return;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    macpad::features::BackupService::backupOnSave(path, content, toFeatureBackupMode(s.backupMode),
                                                  s.backupDir, timestamp);
}


void MainWindow::newFile()
{
    EditorWidget *editor = addEditorTab();
    // 套用偏好之新文件預設 EOL/編碼（FR-053），不標記 dirty
    const auto s = macpad::persistence::SettingsStore::load();
    if (editor)
        editor->applyNewDocumentDefaults(s.defaultEol, s.defaultEncoding);
    updateStatusBar();
}


void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open File"), startDirForDialog());
    if (!path.isEmpty()) {
        m_lastDir = QFileInfo(path).absolutePath();  // defaultDirPolicy=RememberLast 用
        openFile(path);
    }
}


void MainWindow::openFile(const QString &path)
{
    const QString absPath = QFileInfo(path).absoluteFilePath();

    // sessionFileExt：若副檔名匹配偏好設定的 session 副檔名，改以 session 還原開啟（FR-016 延伸）
    const auto sset = macpad::persistence::SettingsStore::load();
    if (!sset.sessionFileExt.isEmpty()
        && QFileInfo(absPath).suffix().compare(sset.sessionFileExt, Qt::CaseInsensitive) == 0
        && QFileInfo::exists(absPath)) {
        openSessionFile(absPath);
        macpad::persistence::RecentFiles::add(absPath);
        rebuildRecentMenu();
        return;
    }

    // 已開啟則聚焦既有分頁（FR-001 邊界：不重複開）——兩個檢視都找
    if (focusExistingPath(absPath))
        return;

    // 大檔守衛（FR-053 largeFileMB）：超過門檻先確認，避免誤開巨檔卡住
    const int largeMB = macpad::persistence::SettingsStore::load().largeFileMB;
    const qint64 sizeMB = QFileInfo(absPath).size() / (1024 * 1024);
    if (largeMB > 0 && sizeMB >= largeMB) {
        const auto btn = QMessageBox::question(this, tr("Open Large File"),
            tr("「%1」約 %2 MB，超過門檻 %3 MB。仍要開啟嗎？")
                .arg(QFileInfo(absPath).fileName()).arg(sizeMB).arg(largeMB));
        if (btn != QMessageBox::Yes)
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
    backupAfterSave(editor->filePath(), editor->text().toUtf8());  // FR-054，best-effort
    updateTabTitle();
    return true;
}


bool MainWindow::saveCurrentAs()
{
    EditorWidget *editor = currentEditor();
    if (!editor)
        return false;

    const QString startPath = editor->isUntitled() ? startDirForDialog() : editor->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), startPath);
    if (path.isEmpty())
        return false;

    m_lastDir = QFileInfo(path).absolutePath();  // defaultDirPolicy=RememberLast 用
    QString err;
    if (!editor->saveFile(path, &err)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("無法儲存 %1：\n%2").arg(path, err));
        return false;
    }
    backupAfterSave(path, editor->text().toUtf8());  // FR-054，best-effort
    updateTabTitle();
    return true;
}


void MainWindow::saveAll()
{
    // Dual-View：兩個檢視都存；略過 clone（與來源共享文件，存來源即涵蓋）
    QTabWidget *prevActive = currentTabWidget();
    const int prevIdx = prevActive->currentIndex();
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        const int cur = w->currentIndex();
        for (int i = 0; i < w->count(); ++i) {
            EditorPane *p = paneIn(w, i);
            if (!p || p->isClone())
                continue;
            EditorWidget *e = p->primary();
            if (e && e->isDirty()) {
                setActiveTabWidget(w);
                w->setCurrentIndex(i);
                saveCurrent();  // 未命名檔會轉為 Save As
            }
        }
        if (cur >= 0 && cur < w->count())
            w->setCurrentIndex(cur);
    }
    setActiveTabWidget(prevActive);
    if (prevIdx >= 0 && prevIdx < prevActive->count())
        prevActive->setCurrentIndex(prevIdx);
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
    // Dual-View：關閉兩個檢視的所有分頁
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = w->count() - 1; i >= 0; --i) {
            const int before = w->count();
            w->setCurrentIndex(i);
            closeTabIn(w, i);
            if (w->count() == before)   // 使用者取消
                return;
        }
    }
}


void MainWindow::closeAllButCurrent()
{
    EditorWidget *keep = currentEditor();
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = w->count() - 1; i >= 0; --i) {
            if (editorIn(w, i) == keep)
                continue;
            const int before = w->count();
            closeTabIn(w, i);
            if (w->count() == before)
                return;
        }
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
    QTabWidget *w = currentTabWidget();
    const int idx = w->currentIndex();
    EditorPane *pane = paneIn(w, idx);
    w->removeTab(idx);
    if (pane)
        pane->deleteLater();
    updateSecondViewVisibility();
    if (m_tabs->count() == 0 && (!m_tabs2 || m_tabs2->count() == 0))
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
    macpad::platform::revealInFileManager(e->filePath());
}


void MainWindow::openInDefaultApp()
{
    EditorWidget *e = currentEditor();
    if (!e || e->isUntitled())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(e->filePath()));
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
        // 先把該編輯器所在的檢視/分頁設為作用中，saveCurrent 才會存到正確文件（Dual-View）
        for (QTabWidget *w : {m_tabs, m_tabs2}) {
            if (!w)
                continue;
            for (int i = 0; i < w->count(); ++i) {
                if (editorIn(w, i) == editor) {
                    setActiveTabWidget(w);
                    w->setCurrentIndex(i);
                }
            }
        }
        return saveCurrent();
    case QMessageBox::Discard:
        return true;
    default:
        return false;  // Cancel
    }
}


void MainWindow::closeTab(int index)
{
    // 選單/工具列入口：關閉作用中檢視的指定分頁
    closeTabIn(currentTabWidget(), index);
}


void MainWindow::closeTabIn(QTabWidget *w, int index)
{
    if (!w)
        return;
    EditorPane *pane = paneIn(w, index);
    EditorWidget *ed = pane ? pane->primary() : nullptr;
    // clone 分頁與來源共享文件：關閉 clone 不應觸發存檔提示（來源仍在或已各自處理）
    const bool isClone = pane && pane->isClone();
    if (!isClone && !maybeSave(ed))
        return;

    // 記住剛關閉的檔案，供 Restore Recent Closed File（clone 不記，避免重覆）
    if (!isClone && ed && !ed->isUntitled()) {
        m_closedFiles.removeAll(ed->filePath());
        m_closedFiles.append(ed->filePath());
        if (m_closedFiles.size() > 20)
            m_closedFiles.removeFirst();
    }

    w->removeTab(index);
    if (pane)
        pane->deleteLater();

    updateSecondViewVisibility();

    // 兩個檢視都空了才補一張空白分頁（避免空視窗）；第二檢視空了只隱藏
    if (m_tabs->count() == 0 && (!m_tabs2 || m_tabs2->count() == 0))
        newFile();
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    // Notepad++ session 快照：啟用時「未命名（untitled）未存緩衝」關閉不提示——其內容由
    // saveSession/buildCurrentSession 持久化、下次啟動靜默還原，且 untitled 無對應磁碟檔，跳過提示無資料遺失風險。
    // 但「已命名 dirty 檔」即使啟用快照仍提示存檔：磁碟檔案不會被寫入，若下次未套用 session
    //（-nosession / restoreOnLaunch 關閉 / session.json 遺失）將永久遺失未存編輯，故不可靜默跳過（資料安全）。
    // 停用快照時維持傳統行為：所有未存分頁逐一確認（FR-001 AC2）。
    const bool sessionSnapshot = macpad::persistence::SettingsStore::load().enableSessionSnapshot;
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        for (int i = 0; i < w->count(); ++i) {
            EditorPane *p = paneIn(w, i);
            if (p && p->isClone())
                continue;  // clone 共享文件由來源負責
            EditorWidget *e = p ? p->primary() : nullptr;
            if (sessionSnapshot && e && e->isUntitled())
                continue;  // untitled 未存緩衝跨重啟保留，關閉不打擾
            if (!maybeSave(e)) {
                event->ignore();
                return;
            }
        }
    }
    saveSession();  // FR-016：關閉前保存 session（快照啟用時含未存內容）
    if (m_projectPanel)
        m_projectPanel->save();  // 保存 Project Panel 樹狀內容（projects.json）
    macpad::features::BackupService::clearSnapshots();  // 正常關閉 → 清空當機復原快照
    event->accept();
}
