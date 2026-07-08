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


void MainWindow::rebuildMacroShortcuts()
{
    qDeleteAll(m_macroShortcuts);
    m_macroShortcuts.clear();
    const QString scPath = macpad::persistence::AppPaths::filePath(
        QStringLiteral("macro_shortcuts.json"));
    const QString mPath = macpad::persistence::AppPaths::filePath(QStringLiteral("macros.json"));
    const QJsonObject sc = macpad::persistence::JsonFile::load(scPath);
    const QJsonObject macros = macpad::persistence::JsonFile::load(mPath);
    for (auto it = sc.begin(); it != sc.end(); ++it) {
        const QString name = it.key();
        const QString keyStr = it.value().toString();
        if (keyStr.isEmpty() || !macros.contains(name))
            continue;
        const QString macroStr = macros.value(name).toString();
        auto *shortcut = new QShortcut(QKeySequence(keyStr), this);
        connect(shortcut, &QShortcut::activated, this, [this, macroStr] {
            EditorWidget *e = currentEditor();
            if (!e)
                return;
            QsciMacro macro(e);
            macro.load(macroStr);
            macro.play();
        });
        m_macroShortcuts.append(shortcut);
    }
}


void MainWindow::rebuildRunShortcuts()
{
    qDeleteAll(m_runShortcuts);
    m_runShortcuts.clear();
    for (const auto &entry : macpad::features::RunCommandStore::load()) {
        if (entry.shortcut.isEmpty())
            continue;
        const QString cmd = entry.command;
        auto *sc = new QShortcut(QKeySequence(entry.shortcut), this);
        connect(sc, &QShortcut::activated, this, [this, cmd] { runSavedCommand(cmd); });
        m_runShortcuts.append(sc);
    }
}


void MainWindow::runSavedCommand(const QString &command)
{
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
        if (e->hasSelectedText()) {
            vars.currentWord = e->selectedText();
        } else {
            int line = 0, col = 0;
            e->getCursorPosition(&line, &col);
            vars.currentWord = e->wordAtLineIndex(line, col);
        }
    }
    m_runDock->setVars(vars);
    m_runDock->show();
    m_runDock->raise();
    m_runDock->runCommand(command);
}


void MainWindow::openMacroManager()
{
    const QString mPath = macpad::persistence::AppPaths::filePath(QStringLiteral("macros.json"));
    const QString scPath = macpad::persistence::AppPaths::filePath(
        QStringLiteral("macro_shortcuts.json"));
    const QJsonObject macros = macpad::persistence::JsonFile::load(mPath);
    if (macros.isEmpty()) {
        QMessageBox::information(this, tr("Macro Manager"), tr("尚無已儲存的巨集"));
        return;
    }
    const QJsonObject sc = macpad::persistence::JsonFile::load(scPath);
    QMap<QString, macpad::ui::MacroData> data;
    for (auto it = macros.begin(); it != macros.end(); ++it) {
        macpad::ui::MacroData d;
        d.shortcut = QKeySequence(sc.value(it.key()).toString());
        data.insert(it.key(), d);
    }
    macpad::ui::MacroManagerDialog dlg(this);
    dlg.setMacros(data);
    connect(&dlg, &macpad::ui::MacroManagerDialog::macroShortcutChanged, this,
            [this, scPath](const QString &name, const QKeySequence &keyseq) {
        QJsonObject cur = macpad::persistence::JsonFile::load(scPath);
        if (keyseq.isEmpty())
            cur.remove(name);
        else
            cur.insert(name, keyseq.toString());
        macpad::persistence::JsonFile::save(scPath, cur);
        rebuildMacroShortcuts();
    });
    connect(&dlg, &macpad::ui::MacroManagerDialog::macroRenamed, this,
            [this, mPath, scPath](const QString &oldName, const QString &newName) {
        QJsonObject m = macpad::persistence::JsonFile::load(mPath);
        if (m.contains(oldName)) {
            m.insert(newName, m.value(oldName));
            m.remove(oldName);
            macpad::persistence::JsonFile::save(mPath, m);
        }
        QJsonObject cur = macpad::persistence::JsonFile::load(scPath);
        if (cur.contains(oldName)) {
            cur.insert(newName, cur.value(oldName));
            cur.remove(oldName);
            macpad::persistence::JsonFile::save(scPath, cur);
        }
        rebuildMacroShortcuts();
    });
    connect(&dlg, &macpad::ui::MacroManagerDialog::macroDeleted, this,
            [this, mPath, scPath](const QString &name) {
        QJsonObject m = macpad::persistence::JsonFile::load(mPath);
        if (m.contains(name)) {
            m.remove(name);
            macpad::persistence::JsonFile::save(mPath, m);
        }
        QJsonObject cur = macpad::persistence::JsonFile::load(scPath);
        if (cur.contains(name)) {
            cur.remove(name);
            macpad::persistence::JsonFile::save(scPath, cur);
        }
        rebuildMacroShortcuts();
    });
    dlg.exec();
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


void MainWindow::showFindInProjects()
{
    if (!m_projectPanel)
        return;
    const QStringList files = m_projectPanel->allFilePaths();
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Find in Projects"),
            tr("Project Panel 中沒有任何檔案。請先於 Project Panel 建立 project 並加入檔案。"));
        m_projectPanel->show();
        m_projectPanel->raise();
        return;
    }
    bool ok = false;
    const QString pattern = QInputDialog::getText(this, tr("Find in Projects"),
        tr("Find what:"), QLineEdit::Normal, m_lastFindText, &ok);
    if (!ok || pattern.isEmpty())
        return;
    m_lastFindText = pattern;
    if (!m_findInFiles) {
        m_findInFiles = new macpad::features::FindInFilesDock(this);
        addDockWidget(Qt::BottomDockWidgetArea, m_findInFiles);
        connect(m_findInFiles, &macpad::features::FindInFilesDock::openLocation,
                this, &MainWindow::openFileAtLine);
    }
    m_findInFiles->show();
    m_findInFiles->raise();
    // 以 Project Panel 的檔案清單為範圍，在 worker thread 搜尋（重用 Find in Files 結果渲染）
    m_findInFiles->findInProjectFiles(pattern, files);
}


void MainWindow::gotoLineOrOffset()
{
    EditorWidget *e = currentEditor();
    if (!e)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Go to…"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *lineRadio = new QRadioButton(tr("Line"), &dlg);
    auto *offsetRadio = new QRadioButton(tr("Character offset"), &dlg);
    lineRadio->setChecked(true);
    auto *spin = new QSpinBox(&dlg);
    spin->setMinimum(1);
    auto updateMax = [e, spin, lineRadio] {
        spin->setMaximum(lineRadio->isChecked() ? qMax(1, e->lines()) : qMax(1, e->length()));
    };
    updateMax();
    connect(lineRadio, &QRadioButton::toggled, &dlg, [updateMax](bool) { updateMax(); });
    layout->addWidget(lineRadio);
    layout->addWidget(offsetRadio);
    layout->addWidget(spin);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int v = spin->value();
    if (lineRadio->isChecked()) {
        e->setCursorPosition(v - 1, 0);
        e->ensureLineVisible(v - 1);
    } else {
        // 字元位移模式：以原始位置定位（SCI_GOTOPOS），並確保所在行可見
        const int line = int(e->SendScintilla(QsciScintilla::SCI_LINEFROMPOSITION,
                                              static_cast<unsigned long>(v)));
        e->SendScintilla(QsciScintilla::SCI_GOTOPOS, static_cast<unsigned long>(v));
        e->ensureLineVisible(line);
    }
    e->setFocus();
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


// ===== Notepad++ 對等：檢視 / 分頁 =====

void MainWindow::toggleAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();  // 變更 flag 後須重新 show
}
