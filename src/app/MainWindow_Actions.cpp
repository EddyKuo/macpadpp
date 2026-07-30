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
#include "features/print/DocumentPrinter.h"
#include "features/autocomplete/ApiDatabase.h"
#include "persistence/ThemeStore.h"
#include "persistence/PluginStore.h"
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
#include <QListWidget>
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
#include <QDialog>
#include <QHBoxLayout>
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


// Macro ▸ Run a Macro Multiple Times…（複刻 Notepad++）
// 提供「執行 N 次」與「執行到檔案結尾」兩種模式——後者是原本缺的那一半。
void MainWindow::runMacroMultipleTimes()
{
    EditorWidget *e = currentEditor();
    if (!e || m_savedMacro.isEmpty()) {
        statusBar()->showMessage(tr("尚無已錄製的巨集"), 2000);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Run a Macro Multiple Times"));
    auto *form = new QVBoxLayout(&dlg);

    auto *timesRadio = new QRadioButton(tr("Run macro"), &dlg);
    timesRadio->setChecked(true);
    auto *spin = new QSpinBox(&dlg);
    spin->setRange(1, 100000);
    spin->setValue(1);
    spin->setSuffix(tr(" times"));
    auto *row = new QHBoxLayout;
    row->addWidget(timesRadio);
    row->addWidget(spin);
    row->addStretch();
    form->addLayout(row);

    auto *eofRadio = new QRadioButton(tr("Run until the end of file"), &dlg);
    form->addWidget(eofRadio);
    // 兩個 radio 互斥；spin 僅在次數模式下可用
    QObject::connect(timesRadio, &QRadioButton::toggled, spin, &QWidget::setEnabled);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const bool untilEof = eofRadio->isChecked();
    const int times = spin->value();

    e->beginUndoAction();
    if (!untilEof) {
        for (int i = 0; i < times; ++i) {
            QsciMacro macro(e);
            macro.load(m_savedMacro);
            macro.play();
        }
    } else {
        // 執行到檔案結尾。兩個終止條件缺一不可：
        //  (1) 插入點抵達文件結尾；
        //  (2) 這一輪播放後插入點與長度都沒變 —— 代表巨集不會再推進，
        //      沒有這條就會在「不移動游標的巨集」上無限迴圈。
        // 另設硬上限作為最後防線，避免任何未預期情況把 UI 卡死。
        constexpr int kHardCap = 100000;
        int iterations = 0;
        while (iterations < kHardCap) {
            const int posBefore = static_cast<int>(
                e->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
            const int lenBefore = static_cast<int>(
                e->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
            if (posBefore >= lenBefore)
                break;   // 已在結尾

            QsciMacro macro(e);
            macro.load(m_savedMacro);
            macro.play();
            ++iterations;

            const int posAfter = static_cast<int>(
                e->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
            const int lenAfter = static_cast<int>(
                e->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
            if (posAfter == posBefore && lenAfter == lenBefore)
                break;   // 沒有推進 → 再跑也不會結束
        }
        statusBar()->showMessage(tr("Macro ran %1 time(s)").arg(iterations), 3000);
    }
    e->endUndoAction();
}


// Plugins ▸ Plugins Admin…（複刻 Notepad++）
// 先前只是個靜態 QMessageBox 資訊框；現在可逐項啟用/停用並持久化。
// 採 Notepad++ 語意「重啟後生效」：擴充在 onLoad 時會掛選單項與停靠面板，
// 執行中卸載無法乾淨還原這些副作用，硬做只會留下殘骸。
void MainWindow::openPluginsAdmin()
{
    // 內建擴充的完整清單（含目前被停用、因而未載入者）——不能只列 m_extensions，
    // 否則停用過的項目會從清單中消失，使用者再也無法重新啟用它。
    struct Known { QString id; QString name; QString version; };
    const QVector<Known> known = {
        {QStringLiteral("builtin.wordcount"), tr("Word Count"), QStringLiteral("1.0.0")},
        {QStringLiteral("builtin.markdownpreview"), tr("Markdown Preview"), QStringLiteral("1.0.0")},
    };

    const QSet<QString> disabled = macpad::persistence::PluginStore::disabledIds();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Plugins Admin"));
    dlg.resize(420, 300);
    auto *root = new QVBoxLayout(&dlg);

    auto *list = new QListWidget(&dlg);
    for (const Known &k : known) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  (%2)  v%3").arg(k.name, k.id, k.version), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(disabled.contains(k.id) ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, k.id);
    }
    root->addWidget(list);

    // 誠實揭露定位：這裡管理的是自建的 in-process 擴充，不是 Notepad++ 的 .dll 外掛。
    auto *note = new QLabel(
        tr("macpad++ 以內建 extension protocol 取代外掛機制。\n"
           "註：Notepad++ 的 .dll 外掛為 Windows 專屬原生二進位，本程式不載入。\n"
           "變更需重新啟動後生效。"), &dlg);
    note->setWordWrap(true);
    root->addWidget(note);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QSet<QString> newDisabled;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->checkState() == Qt::Unchecked)
            newDisabled.insert(item->data(Qt::UserRole).toString());
    }
    macpad::persistence::PluginStore::setDisabledIds(newDisabled);

    if (newDisabled != disabled)
        statusBar()->showMessage(tr("Plugin changes take effect after restart"), 4000);
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
                         static_cast<quintptr>(b.size()), b.constData());
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
                     static_cast<quintptr>(b.size()), b.constData());
    e->endUndoAction();
    e->setCursorPosition(newFirst, 0);
}


// 增量搜尋的「第 n 筆 / 共 m 筆」（複刻 Notepad++ v8.9.7）。
// 以純文字掃描計數（大小寫不敏感，與 findFirst 的參數一致），不動用 Scintilla 的搜尋狀態，
// 因此不會干擾目前的選取/游標；偏好關閉時直接清空標籤、不做任何掃描。
void MainWindow::updateIncrementalSearchCount(const QString &needle)
{
    if (!m_incCount)
        return;
    if (!macpad::persistence::SettingsStore::load().incrementalSearchCount || needle.isEmpty()) {
        m_incCount->clear();
        return;
    }
    EditorWidget *e = currentEditor();
    if (!e) {
        m_incCount->clear();
        return;
    }

    const QString text = e->text();
    int total = 0;
    int nth = 0;
    // 目前選取起點（即剛剛命中的位置）在第幾筆
    const int caretPos = static_cast<int>(e->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART));
    int from = 0;
    while (true) {
        const int at = text.indexOf(needle, from, Qt::CaseInsensitive);
        if (at < 0)
            break;
        ++total;
        // Scintilla 位置以 UTF-8 位元組計，QString 以 UTF-16 計；以位元組長度換算比對點
        const int bytePos = text.left(at).toUtf8().size();
        if (bytePos == caretPos)
            nth = total;
        from = at + 1;   // 允許重疊匹配，與 Notepad++ 的計數一致
    }

    if (total == 0)
        m_incCount->setText(tr("  找不到"));
    else if (nth > 0)
        m_incCount->setText(tr("  第 %1 / 共 %2 筆").arg(nth).arg(total));
    else
        m_incCount->setText(tr("  共 %1 筆").arg(total));
}


// 列印目前文件（工具列與 File ▸ Print… 共用；先前工具列會套用 Preferences ▸ Print，
// 選單那條卻是裸的 QsciPrinter，兩者行為不一致——此處統一走同一條路徑）。
bool MainWindow::preparePrinter(macpad::features::DocumentPrinter &printer, EditorWidget *&editor)
{
    editor = currentEditor();
    if (!editor)
        return false;

    const auto ps = macpad::persistence::SettingsStore::load();
    printer.setFilePath(editor->isUntitled() ? QString() : editor->filePath());
    printer.setHeaderTemplate(ps.printHeader);
    printer.setFooterTemplate(ps.printFooter);
    printer.setMagnification(0);
    // 深色主題直接照畫面配色列印會整頁塗黑，故色彩模式獨立設定（預設黑字白底）
    editor->SendScintilla(QsciScintillaBase::SCI_SETPRINTCOLOURMODE,
                          static_cast<unsigned long>(qBound(0, ps.printColourMode, 3)));
    const qreal m = qBound(0, ps.printMarginMm, 50);
    printer.setPageMargins(QMarginsF(m, m, m, m), QPageLayout::Millimeter);
    return true;
}

void MainWindow::sendToPrinter(macpad::features::DocumentPrinter &printer, EditorWidget *editor)
{
    const auto ps = macpad::persistence::SettingsStore::load();
    // FormFeed 視為分頁符（Notepad++ v8.9.7）；關閉時走原本的整份分頁。
    // printDocument 會在頁首/頁尾用到 $(NB_PAGES) 時先試排求總頁數。
    printer.printDocument(editor, ps.printFormFeedAsPageBreak);
}

void MainWindow::printCurrentDocument()
{
    macpad::features::DocumentPrinter printer;
    EditorWidget *e = nullptr;
    if (!preparePrinter(printer, e))
        return;

    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    sendToPrinter(printer, e);
}

// -quickPrint：免對話框直印。先前此路徑在 main.cpp 裡是裸的 QsciPrinter，
// 完全忽略 Preferences ▸ Print（頁首/頁尾/邊界/色彩模式/FormFeed），與另外兩條列印路徑不一致。
void MainWindow::quickPrintCurrentDocument()
{
    macpad::features::DocumentPrinter printer;
    EditorWidget *e = nullptr;
    if (!preparePrinter(printer, e))
        return;
    sendToPrinter(printer, e);
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
        // 符合筆數與「第 n 筆」顯示（複刻 Notepad++ v8.9.7 的 Count / nth of count）；
        // 由偏好 incrementalSearchCount 控制，關閉時不付出全文掃描成本。
        m_incCount = new QLabel(m_incBar);
        m_incBar->addWidget(m_incCount);
        auto *closeBtn = m_incBar->addAction(tr("✕"));
        addToolBar(Qt::BottomToolBarArea, m_incBar);

        // 邊打邊找：從目前游標位置往後搜尋
        connect(m_incSearch, &QLineEdit::textChanged, this, [this](const QString &q) {
            EditorWidget *e = currentEditor();
            if (!e || q.isEmpty()) {
                m_incCount->clear();
                return;
            }
            int line = 0, idx = 0;
            e->getCursorPosition(&line, &idx);
            e->findFirst(q, false, false, false, true, true, line, idx);
            updateIncrementalSearchCount(q);
        });
        // Enter 找下一個
        connect(m_incSearch, &QLineEdit::returnPressed, this, [this] {
            if (EditorWidget *e = currentEditor()) e->findNext();
            updateIncrementalSearchCount(m_incSearch->text());
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
    // 平台整合：appName 為空→系統預設程式；否則以指定 app 開啟（見 DesktopIntegration）。
    macpad::platform::openInApp(appName, path);
}


// ===== Notepad++ 對等：檢視 / 分頁 =====

void MainWindow::toggleAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();  // 變更 flag 後須重新 show
}
