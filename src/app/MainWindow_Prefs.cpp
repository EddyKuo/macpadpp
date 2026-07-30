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
#include "features/update/UpdateChecker.h"
#include "features/update/UpdateDownloader.h"
#include "platform/DesktopIntegration.h"

#include <QLocale>
#include <QProgressDialog>
#include <QPushButton>
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
#include <QSystemTrayIcon>
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
    editor->setAdvancedAutoIndent(s.advancedAutoIndent);       // 依語言追加一級縮排
    editor->setUndoSelectionHistory(s.undoSelectionHistory);   // Undo/Redo 納入選取歷史
    editor->setSelectionDragDropEnabled(s.selectionDragDrop);  // 是否允許拖放選取文字
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
    editor->SendScintilla(QsciScintillaBase::SCI_SETWORDCHARS,
                          static_cast<quintptr>(0), wordChars.constData());
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
    // 分頁列：可見性 / 垂直排列 / 關閉鈕 / 多列換行
    for (QTabWidget *w : {m_tabs, m_tabs2}) {
        if (!w)
            continue;
        w->tabBar()->setVisible(s.showTabBar);
        w->setTabPosition(s.tabBarVertical ? QTabWidget::West : QTabWidget::North);
        w->setTabsClosable(s.tabBarShowCloseButton);
        // 多列換行由 MultiRowTabBar 真正實作（先前只能以停用捲動鈕近似）。
        // 垂直排列時分頁本來就是縱向堆疊，多列無意義，故僅在水平時啟用。
        if (auto *bar = qobject_cast<macpad::ui::MultiRowTabBar *>(w->tabBar()))
            bar->setMultiRow(s.tabBarMultiLine && !s.tabBarVertical);
        else
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
            e->setPolicyReadOnly(on);   // 政策鎖定：Clear Read-Only Flag 不得解除
    });
}


// 檢查更新（複刻 Notepad++ 的 Check for Updates / auto-updater）。
// silent=true 為啟動時的自動檢查：只在「真的有新版」時才打擾使用者，失敗完全靜默
// （沒網路是常態，不該每次開檔都跳錯誤）。
// 有新版時提供「直接下載」：抓取符合本平台的發佈檔並驗證完整性，完成後在檔案管理器
// 中顯示（最後的安裝/解壓由使用者執行——發佈物是免安裝 zip / DMG 而非安裝程式，
// 且未經簽章，不由程式自行替換執行中的二進位）。
void MainWindow::checkForUpdates(bool silent)
{
    auto *checker = new macpad::features::UpdateChecker(this);
    connect(checker, &macpad::features::UpdateChecker::finished, this,
            [this, checker, silent](bool hasUpdate, const QString &latest,
                                    const QString &url, const QString &err,
                                    const QString &assetUrl, const QString &assetSize) {
        checker->deleteLater();

        if (!err.isEmpty()) {
            if (!silent)
                QMessageBox::warning(this, tr("Check for Updates"),
                                     tr("無法取得更新資訊：%1").arg(err));
            return;
        }
        if (!hasUpdate) {
            if (!silent)
                QMessageBox::information(this, tr("Check for Updates"),
                                         tr("目前已是最新版本（%1）。")
                                             .arg(QString::fromLatin1(MACPAD_VERSION)));
            return;
        }

        // 該版沒有本平台的發佈檔時，只能導向 release 頁面（不臆測要下載哪個檔）
        if (assetUrl.isEmpty()) {
            const auto btn = QMessageBox::question(
                this, tr("Check for Updates"),
                tr("有新版本可用：%1（目前為 %2）。\n要開啟下載頁面嗎？")
                    .arg(latest, QString::fromLatin1(MACPAD_VERSION)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (btn == QMessageBox::Yes && !url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
            return;
        }

        const qint64 bytes = assetSize.toLongLong();
        const QString sizeText = bytes > 0
            ? tr("（約 %1）").arg(QLocale().formattedDataSize(bytes)) : QString();
        QMessageBox box(this);
        box.setWindowTitle(tr("Check for Updates"));
        box.setText(tr("有新版本可用：%1（目前為 %2）。")
                        .arg(latest, QString::fromLatin1(MACPAD_VERSION)));
        box.setInformativeText(tr("可直接下載安裝檔%1，或改為開啟下載頁面。")
                                   .arg(sizeText));
        QPushButton *dl = box.addButton(tr("下載"), QMessageBox::AcceptRole);
        QPushButton *page = box.addButton(tr("開啟頁面"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(dl);
        box.exec();

        if (box.clickedButton() == page) {
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
            return;
        }
        if (box.clickedButton() != dl)
            return;

        downloadUpdate(assetUrl, bytes);
    });
    checker->check(QString::fromLatin1(MACPAD_VERSION));
}


// 下載新版安裝檔並顯示進度；完成後在檔案管理器中顯示，由使用者執行安裝/解壓。
// 不自行替換執行中的二進位：發佈物為未簽章的免安裝 zip / DMG，程式自我覆寫的
// 風險（供應鏈一旦被汙染即直接落地）與收益不成比例。
void MainWindow::downloadUpdate(const QString &assetUrl, qint64 expectedBytes)
{
    auto *dl = new macpad::features::UpdateDownloader(this);
    auto *progress = new QProgressDialog(tr("正在下載新版本…"), tr("取消"), 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    connect(dl, &macpad::features::UpdateDownloader::progress, progress,
            [progress](qint64 received, qint64 total) {
        if (total > 0) {
            progress->setMaximum(100);
            progress->setValue(static_cast<int>(received * 100 / total));
            progress->setLabelText(
                tr("正在下載新版本…\n%1 / %2")
                    .arg(QLocale().formattedDataSize(received),
                         QLocale().formattedDataSize(total)));
        } else {
            progress->setMaximum(0);   // 總量未知 → 不定量進度條
        }
    });
    connect(progress, &QProgressDialog::canceled, dl,
            &macpad::features::UpdateDownloader::cancel);

    connect(dl, &macpad::features::UpdateDownloader::finished, this,
            [this, dl, progress, expectedBytes](bool ok, const QString &path,
                                                const QString &error) {
        progress->close();
        progress->deleteLater();
        dl->deleteLater();

        if (!ok) {
            // 使用者主動取消不算錯誤，不再彈窗打擾
            if (error != tr("已取消"))
                QMessageBox::warning(this, tr("Check for Updates"),
                                     tr("下載失敗：%1").arg(error));
            return;
        }
        // 伺服器未回報大小時完整性無從驗證，必須說明白，不能讓使用者以為已驗過
        const QString verified = expectedBytes > 0
            ? tr("檔案大小已核對無誤。")
            : tr("注意：伺服器未提供檔案大小，無法核對完整性。");
        QMessageBox::information(
            this, tr("Check for Updates"),
            tr("已下載至：\n%1\n\n%2\n關閉 macpad++ 後再安裝/解壓縮以完成更新。")
                .arg(path, verified));
        macpad::platform::revealInFileManager(path);
    });

    dl->start(assetUrl, expectedBytes);
}


// -systemtray：常駐系統匣。QSystemTrayIcon 為跨平台 API——Windows 顯示於通知區、
// macOS 顯示於選單列狀態區——故兩平台共用這一份實作，無需 #ifdef。
// 系統不支援（如無頭環境/CI）時回傳 false 並保持主視窗正常運作，不視為錯誤。
bool MainWindow::enableSystemTray()
{
    if (m_tray)
        return true;   // 已啟用，冪等
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;

    // 圖示來源：優先用視窗圖示（main.cpp 已 setWindowIcon），空的話直接取資源，
    // 否則系統匣會是一塊看不見的空白，等同功能失效。
    QIcon trayIcon = windowIcon();
    if (trayIcon.isNull())
        trayIcon = QIcon(QStringLiteral(":/icons/app-icon.svg"));
    m_tray = new QSystemTrayIcon(trayIcon, this);
    m_tray->setToolTip(QStringLiteral("macpad++"));

    auto *menu = new QMenu(this);
    menu->addAction(tr("Show Window"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(tr("Hide Window"), this, [this] { hide(); });
    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    // 點擊圖示切換顯示/隱藏（macOS 選單列點擊只會叫出選單，故僅在 Windows 觸發此路徑）
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                    return;
                if (isVisible() && !isMinimized()) {
                    hide();
                } else {
                    showNormal();
                    raise();
                    activateWindow();
                }
            });

    m_tray->show();
    return true;
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
                e->setPolicyReadOnly(true);   // 監控中唯讀（tail -f）；屬政策鎖定
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
