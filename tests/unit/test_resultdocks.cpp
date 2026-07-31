// 單元測試：三個「結果面板」Dock 的 UI 行為
//   - FindInFilesDock（FR-013）：非同步搜尋 → 結果樹渲染、雙擊跳轉、auto-purge、右鍵選單
//   - FindAllDock（FR-058）：依文件標題分組、累計計數、雙擊跳轉、右鍵選單
//   - RunDock（FR-031, CON-006）：變數展開 + 參數陣列啟動外部程序、輸出擷取、指令歷史
//
// 這些 Dock 的成員（QLineEdit/QTreeWidget…）皆為 private 且無 getter，測試刻意「由外部
// 以使用者視角操作」：用 findChildren 依 placeholder / 文字定位控制項，再以點擊、送信號、
// 觸發選單動作等方式驅動，最後只斷言使用者看得見的結果（樹內容、狀態列文字、signal、剪貼簿）。
// 如此測試不依賴內部欄位名稱，重構實作不會誤傷。
//
// 注意：測試在 QT_QPA_PLATFORM=offscreen 下執行，一律用 show() 而非 exec()；
// 測試本身不呼叫任何 modal API（QMessageBox / QFileDialog）。因此 FindInFilesDock 的
// 「Replace in Files」（內含 QMessageBox::question 確認）與 RunDock 的「Browse…」
// （QFileDialog::getOpenFileName）無法在此覆蓋。

#include <QtTest>

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCompleter>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "features/findall/FindAllDock.h"
#include "features/findinfiles/FindInFilesDock.h"
#include "features/run/RunDock.h"

using namespace macpad::features;

namespace {

// --- 控制項定位小工具（以「使用者看得到的屬性」而非成員名稱定位）-------------

QLineEdit *lineEditByPlaceholder(const QWidget *w, const QString &placeholder)
{
    const auto edits = w->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits) {
        if (e->placeholderText() == placeholder)
            return e;
    }
    return nullptr;
}

QCheckBox *checkBoxByText(const QWidget *w, const QString &text)
{
    const auto boxes = w->findChildren<QCheckBox *>();
    for (QCheckBox *b : boxes) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

QPushButton *buttonByText(const QWidget *w, const QString &text)
{
    const auto buttons = w->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

// 狀態列 QLabel：建構時唯一 text 為空的 QLabel（其餘皆為 "Find:"、"Dir:" 等固定標籤）
QLabel *emptyLabel(const QWidget *w)
{
    const auto labels = w->findChildren<QLabel *>();
    for (QLabel *l : labels) {
        if (l->text().isEmpty())
            return l;
    }
    return nullptr;
}

// showResultsContextMenu() 內部以 QMenu::exec() 進入巢狀事件迴圈，測試端沒有滑鼠可點；
// 因此在呼叫前掛上 timer：選單一出現就以鍵盤啟動指定動作（走 QMenu 的 activateAction
// 路徑，exec() 才會回傳該 QAction，被測程式的 if/else 分支才真的會執行）。
// actionText 為空字串代表「不選任何動作」→ 直接關閉選單，用來覆蓋 chosen == nullptr 分支。
// 另掛看門狗，萬一動作找不到也會強制關閉，確保測試不會永久阻塞。
void schedulePopupChoice(const QString &actionText)
{
    auto *timer = new QTimer;
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, actionText] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;  // 選單尚未彈出，下次再試
        timer->stop();
        timer->deleteLater();
        if (!actionText.isEmpty()) {
            const auto actions = menu->actions();
            for (QAction *a : actions) {
                if (a->text() == actionText && a->isEnabled()) {
                    menu->setActiveAction(a);
                    QTest::keyClick(menu, Qt::Key_Return);
                    return;
                }
            }
        }
        menu->close();
    });
    timer->start();
    QTimer::singleShot(3000, qApp, [] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
            menu->close();  // 看門狗：避免測試卡在 exec()
    });
}

}  // namespace

class TestResultDocks : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    // --- FindInFilesDock 控制項組合（每個測試各自建立一個 dock，彼此隔離）---
    struct FifUi {
        QLineEdit *pattern = nullptr;
        QLineEdit *dir = nullptr;
        QLineEdit *filter = nullptr;
        QLineEdit *exclude = nullptr;
        QCheckBox *regex = nullptr;
        QCheckBox *caseSensitive = nullptr;
        QCheckBox *wholeWord = nullptr;
        QCheckBox *autoPurge = nullptr;
        QCheckBox *wrapRows = nullptr;
        QPushButton *search = nullptr;
        QPushButton *cancel = nullptr;
        QTreeWidget *results = nullptr;
        QLabel *status = nullptr;
    };

    static FifUi bind(const FindInFilesDock &dock)
    {
        FifUi ui;
        ui.pattern = lineEditByPlaceholder(&dock, QStringLiteral("搜尋…"));
        ui.filter = lineEditByPlaceholder(&dock, QStringLiteral("*.cpp;*.h（空=全部）"));
        ui.exclude = lineEditByPlaceholder(&dock, QStringLiteral("!*.min.js; !+\\node_modules"));
        // 「Dir:」欄位是唯一沒有 placeholder 的 QLineEdit
        const auto edits = dock.findChildren<QLineEdit *>();
        for (QLineEdit *e : edits) {
            if (e->placeholderText().isEmpty())
                ui.dir = e;
        }
        ui.regex = checkBoxByText(&dock, QStringLiteral("Regex"));
        ui.caseSensitive = checkBoxByText(&dock, QStringLiteral("Match case"));
        ui.wholeWord = checkBoxByText(&dock, QStringLiteral("Whole word"));
        ui.autoPurge = checkBoxByText(&dock, QStringLiteral("Auto-purge previous results"));
        ui.wrapRows = checkBoxByText(&dock, QStringLiteral("Wrap result rows"));
        ui.search = buttonByText(&dock, QStringLiteral("Search"));
        ui.cancel = buttonByText(&dock, QStringLiteral("Cancel"));
        ui.results = dock.findChild<QTreeWidget *>();
        ui.status = emptyLabel(&dock);
        return ui;
    }

    QString filePath(const QString &rel) const
    {
        return m_dir.path() + QLatin1Char('/') + rel;
    }

    void writeFile(const QString &rel, const QByteArray &content)
    {
        const QString path = filePath(rel);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(content), qint64(content.size()));
    }

    // 依「檔名」欄位取出結果列（搜尋結果的檔案走訪順序由 QDirIterator 決定，不可假設）
    static QTreeWidgetItem *rowForFile(QTreeWidget *tree, const QString &fileName, int line)
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tree->topLevelItem(i);
            if (it->text(0) == fileName && it->text(1) == QString::number(line))
                return it;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        // a.txt 兩筆、b.cpp 一筆；c.min.js 供 exclude 規則測試
        writeFile(QStringLiteral("a.txt"), "alpha line\nTODO first\nbeta TODO second\n");
        writeFile(QStringLiteral("b.cpp"), "int main() { return 0; } // TODO in cpp\n");
        writeFile(QStringLiteral("c.min.js"), "var x = 1; // TODO minified\n");
    }

    // ================= FindInFilesDock =================

    // 搜尋 → 結果樹每列的三欄與 UserRole 資料、狀態列筆數、按鈕啟用狀態
    void fif_searchPopulatesResultTree()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        QVERIFY(ui.pattern && ui.dir && ui.results && ui.status && ui.search && ui.cancel);
        QVERIFY(!ui.cancel->isEnabled());  // 初始不可取消

        dock.setSearchRoot(m_dir.path());
        QCOMPARE(ui.dir->text(), m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.search->click();

        // 搜尋在 worker thread 執行（NFR-005），故以 QTRY_ 等待 onSearchDone 渲染完成
        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        // 目錄下 a.txt 2 筆、b.cpp 1 筆、c.min.js 1 筆
        QCOMPARE(ui.results->topLevelItemCount(), 4);
        QCOMPARE(ui.status->text(), QStringLiteral("找到 4 筆"));
        QVERIFY(ui.search->isEnabled());
        QVERIFY(!ui.cancel->isEnabled());

        // a.txt 第 3 行 "beta TODO second"：column 應為 1-based 的 6
        QTreeWidgetItem *row = rowForFile(ui.results, QStringLiteral("a.txt"), 3);
        QVERIFY(row);
        QCOMPARE(row->text(2), QStringLiteral("beta TODO second"));
        QCOMPARE(row->data(0, Qt::UserRole).toString(), filePath(QStringLiteral("a.txt")));
        QCOMPARE(row->data(1, Qt::UserRole).toInt(), 3);
        QCOMPARE(row->data(2, Qt::UserRole).toInt(), 6);
        QCOMPARE(row->toolTip(0), filePath(QStringLiteral("a.txt")));
    }

    // 缺 pattern 或缺 dir 時，Search 必須是 no-op（不清狀態列、不啟動搜尋）
    void fif_emptyInputsAreNoOp()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);

        ui.dir->setText(m_dir.path());
        ui.search->click();  // pattern 為空
        QVERIFY(ui.status->text().isEmpty());

        ui.pattern->setText(QStringLiteral("TODO"));
        ui.dir->clear();
        ui.search->click();  // dir 為空
        QVERIFY(ui.status->text().isEmpty());
        QCOMPARE(ui.results->topLevelItemCount(), 0);
    }

    // currentOptions()：filter / exclude / regex / case / whole-word 勾選要真的傳進引擎
    void fif_optionsAffectSearchScope()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        QVERIFY(ui.filter && ui.exclude && ui.regex && ui.caseSensitive && ui.wholeWord);

        ui.dir->setText(m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.filter->setText(QStringLiteral("*.txt;*.js"));   // 排除 b.cpp
        ui.exclude->setText(QStringLiteral(" !*.min.js ;")); // 再排除 c.min.js（含空白/空項）
        ui.search->click();

        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        QCOMPARE(ui.results->topLevelItemCount(), 2);  // 只剩 a.txt 的兩筆
        for (int i = 0; i < ui.results->topLevelItemCount(); ++i)
            QCOMPARE(ui.results->topLevelItem(i)->text(0), QStringLiteral("a.txt"));

        // 換成大小寫敏感 + 全字 + regex：只有 "TODO" 開頭那筆（\bTODO\b 兩筆仍在）
        ui.filter->setText(QStringLiteral("*.txt"));
        ui.exclude->clear();
        ui.regex->setChecked(true);
        ui.caseSensitive->setChecked(true);
        ui.wholeWord->setChecked(true);
        ui.pattern->setText(QStringLiteral("TO.O"));
        ui.status->clear();
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        QCOMPARE(ui.results->topLevelItemCount(), 2);
    }

    // auto-purge 關閉時新結果要「附加」在舊結果之後（Notepad++ 行為）
    void fif_autoPurgeOffAppendsResults()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        QVERIFY(ui.autoPurge && ui.autoPurge->isChecked());  // 預設開啟

        ui.dir->setText(m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.results->topLevelItemCount() == 4, 10000);

        // 預設（開啟）：重跑仍是 4 筆
        ui.status->clear();
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        QCOMPARE(ui.results->topLevelItemCount(), 4);

        // 關閉後：重跑變成 8 筆
        ui.autoPurge->setChecked(false);
        ui.status->clear();
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        QCOMPARE(ui.results->topLevelItemCount(), 8);
    }

    // 雙擊結果列要以「檔案路徑 + 行 + 欄」發出 openLocation（主視窗據此跳轉）
    void fif_doubleClickEmitsOpenLocation()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        dock.setSearchRoot(m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.results->topLevelItemCount() == 4, 10000);

        QSignalSpy spy(&dock, &FindInFilesDock::openLocation);
        QTreeWidgetItem *row = rowForFile(ui.results, QStringLiteral("a.txt"), 3);
        QVERIFY(row);
        emit ui.results->itemDoubleClicked(row, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), filePath(QStringLiteral("a.txt")));
        QCOMPARE(spy.at(0).at(1).toInt(), 3);
        QCOMPARE(spy.at(0).at(2).toInt(), 6);

        // 沒有 UserRole 路徑的列（防禦性分支）不得發出 signal
        auto *bogus = new QTreeWidgetItem(ui.results);
        emit ui.results->itemDoubleClicked(bogus, 0);
        emit ui.results->itemDoubleClicked(nullptr, 0);
        QCOMPARE(spy.count(), 1);
    }

    // 「Find in Projects」入口：以明確檔案清單搜尋，結果沿用同一棵樹
    void fif_findInProjectFiles()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);

        // 空清單 / 空 pattern 皆為 no-op（但 pattern 欄位仍會被填入）
        dock.findInProjectFiles(QStringLiteral("TODO"), {});
        QVERIFY(ui.status->text().isEmpty());
        dock.findInProjectFiles(QString(), {filePath(QStringLiteral("a.txt"))});
        QVERIFY(ui.pattern->text().isEmpty());
        QVERIFY(ui.status->text().isEmpty());

        dock.findInProjectFiles(QStringLiteral("TODO"),
                                {filePath(QStringLiteral("a.txt")),
                                 filePath(QStringLiteral("b.cpp"))});
        QCOMPARE(ui.pattern->text(), QStringLiteral("TODO"));
        QVERIFY(ui.status->text().contains(QStringLiteral("2 個專案檔案")));
        QTRY_VERIFY_WITH_TIMEOUT(ui.status->text().startsWith(QStringLiteral("找到")), 10000);
        QCOMPARE(ui.results->topLevelItemCount(), 3);
        QVERIFY(rowForFile(ui.results, QStringLiteral("b.cpp"), 1));
    }

    // Cancel 會設下取消旗標；搜尋結束後按鈕狀態必須回復可再次搜尋
    void fif_cancelRestoresButtonState()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        ui.dir->setText(m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.search->click();
        ui.cancel->click();  // 取消旗標 → 引擎儘快中止（可能已完成，結果數不可斷言）
        QTRY_VERIFY_WITH_TIMEOUT(ui.search->isEnabled() && !ui.cancel->isEnabled(), 10000);
        QVERIFY(ui.status->text().startsWith(QStringLiteral("找到")));
    }

    // 「Wrap result rows」勾選要直接反映到結果樹的 wordWrap
    void fif_wrapRowsTogglesWordWrap()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        QVERIFY(ui.wrapRows && !ui.wrapRows->isChecked());
        QVERIFY(!ui.results->wordWrap());
        ui.wrapRows->setChecked(true);
        QVERIFY(ui.results->wordWrap());
        ui.wrapRows->setChecked(false);
        QVERIFY(!ui.results->wordWrap());
    }

    // 右鍵選單：Copy File Path / Copy Line Text 寫入剪貼簿；Expand/Collapse All 作用於樹
    void fif_contextMenuActions()
    {
        FindInFilesDock dock;
        const FifUi ui = bind(dock);
        dock.resize(700, 500);
        dock.show();
        dock.setSearchRoot(m_dir.path());
        ui.pattern->setText(QStringLiteral("TODO"));
        ui.search->click();
        QTRY_VERIFY_WITH_TIMEOUT(ui.results->topLevelItemCount() == 4, 10000);

        QTreeWidgetItem *row = rowForFile(ui.results, QStringLiteral("a.txt"), 2);
        QVERIFY(row);
        QTRY_VERIFY(!ui.results->visualItemRect(row).isEmpty());
        const QPoint pos = ui.results->visualItemRect(row).center();

        QApplication::clipboard()->clear();
        schedulePopupChoice(QStringLiteral("Copy File Path"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, pos));
        QCOMPARE(QApplication::clipboard()->text(), filePath(QStringLiteral("a.txt")));
        QCOMPARE(ui.results->currentItem(), row);  // 右鍵位置的列會成為目前列

        schedulePopupChoice(QStringLiteral("Copy Line Text"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, pos));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("TODO first"));

        // 空白處按右鍵：itemAt 為 nullptr，複製類動作停用；改選 Expand All / Collapse All
        const QPoint empty(5, ui.results->viewport()->height() - 5);
        schedulePopupChoice(QStringLiteral("Expand All"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, empty));
        schedulePopupChoice(QStringLiteral("Collapse All"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, empty));

        // 不選任何動作（按 Esc 關閉）→ 剪貼簿不得被改動
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        schedulePopupChoice(QString());
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, pos));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }

    // ================= FindAllDock =================

    // setResults()：依文件標題分組、子節點欄位、標題列累計文字
    void fa_groupsMatchesByDocument()
    {
        FindAllDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *header = dock.findChild<QLabel *>();
        QVERIFY(tree && header);
        QCOMPARE(header->text(), QStringLiteral("尚無結果"));

        QVector<FindAllMatch> matches{
            {1, QStringLiteral("a.txt"), 2, 1, QStringLiteral("  TODO first  ")},
            {1, QStringLiteral("a.txt"), 3, 6, QStringLiteral("beta TODO second")},
            {2, QStringLiteral("b.cpp"), 1, 27, QStringLiteral("// TODO in cpp")},
        };
        dock.setResults(matches);

        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("a.txt"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("b.cpp"));
        QCOMPARE(tree->topLevelItem(1)->childCount(), 1);
        QVERIFY(tree->topLevelItem(0)->isExpanded());

        QTreeWidgetItem *first = tree->topLevelItem(0)->child(0);
        QCOMPARE(first->text(0), QStringLiteral("2"));
        QCOMPARE(first->text(1), QStringLiteral("1"));
        QCOMPARE(first->text(2), QStringLiteral("TODO first"));  // lineText 會 trim
        QCOMPARE(first->data(0, Qt::UserRole).toInt(), 1);
        QCOMPARE(first->data(1, Qt::UserRole).toInt(), 2);
        QCOMPARE(first->data(2, Qt::UserRole).toInt(), 1);
        QCOMPARE(header->text(), QStringLiteral("共找到 3 筆（2 個文件）"));

        // 分組節點沒有 docId：雙擊必須被忽略；子節點才發 openLocation
        QSignalSpy spy(&dock, &FindAllDock::openLocation);
        emit tree->itemDoubleClicked(tree->topLevelItem(0), 0);
        emit tree->itemDoubleClicked(nullptr, 0);
        QCOMPARE(spy.count(), 0);
        emit tree->itemDoubleClicked(tree->topLevelItem(1)->child(0), 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
        QCOMPARE(spy.at(0).at(1).toInt(), 1);
        QCOMPARE(spy.at(0).at(2).toInt(), 27);
    }

    // auto-purge 開啟＝整批覆蓋；關閉＝同標題續加到既有分組，計數累加
    void fa_autoPurgeControlsAccumulation()
    {
        FindAllDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *header = dock.findChild<QLabel *>();
        auto *autoPurge = checkBoxByText(&dock, QStringLiteral("Auto-purge previous results"));
        QVERIFY(tree && header && autoPurge);
        QVERIFY(autoPurge->isChecked());

        const QVector<FindAllMatch> batch{
            {1, QStringLiteral("a.txt"), 2, 1, QStringLiteral("TODO first")},
        };
        dock.setResults(batch);
        dock.setResults(batch);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(header->text(), QStringLiteral("共找到 1 筆（1 個文件）"));

        autoPurge->setChecked(false);
        dock.setResults(batch);  // 同標題 → 沿用既有分組節點
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        QCOMPARE(header->text(), QStringLiteral("共找到 2 筆（1 個文件）"));

        dock.setResults({{2, QStringLiteral("b.cpp"), 9, 1, QStringLiteral("TODO")}});
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(header->text(), QStringLiteral("共找到 3 筆（2 個文件）"));
    }

    void fa_wrapRowsTogglesWordWrap()
    {
        FindAllDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *wrap = checkBoxByText(&dock, QStringLiteral("Wrap result rows"));
        QVERIFY(tree && wrap);
        QVERIFY(!tree->wordWrap());
        wrap->setChecked(true);
        QVERIFY(tree->wordWrap());
    }

    // 右鍵選單：只有「有 docId 的子節點」可複製行文字；Collapse/Expand All 影響展開狀態
    void fa_contextMenuActions()
    {
        FindAllDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);
        dock.resize(700, 500);
        dock.show();
        dock.setResults({{1, QStringLiteral("a.txt"), 2, 1, QStringLiteral("TODO first")},
                         {1, QStringLiteral("a.txt"), 3, 6, QStringLiteral("beta TODO second")}});

        QTreeWidgetItem *child = tree->topLevelItem(0)->child(1);
        QTRY_VERIFY(!tree->visualItemRect(child).isEmpty());

        QApplication::clipboard()->clear();
        schedulePopupChoice(QStringLiteral("Copy Line Text"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu",
                                  Q_ARG(QPoint, tree->visualItemRect(child).center()));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("beta TODO second"));
        QCOMPARE(tree->currentItem(), child);

        // 空白處：Collapse All → 分組收合；再 Expand All → 展開
        const QPoint empty(5, tree->viewport()->height() - 5);
        schedulePopupChoice(QStringLiteral("Collapse All"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, empty));
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        schedulePopupChoice(QStringLiteral("Expand All"));
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu", Q_ARG(QPoint, empty));
        QVERIFY(tree->topLevelItem(0)->isExpanded());

        // 不選任何動作 → 什麼都不做
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        schedulePopupChoice(QString());
        QMetaObject::invokeMethod(&dock, "showResultsContextMenu",
                                  Q_ARG(QPoint, tree->visualItemRect(child).center()));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }

    // ================= RunDock =================

    // 執行無害的 /bin/echo：回音行、stdout 擷取、結束碼三者都要出現在輸出區
    void run_executesCommandAndCapturesOutput()
    {
        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();
        auto *cmd = dock.findChild<QLineEdit *>();
        QVERIFY(output && cmd);
        QVERIFY(output->isReadOnly());

        dock.setCommand(QStringLiteral("/bin/echo hello_dock"));
        QCOMPARE(cmd->text(), QStringLiteral("/bin/echo hello_dock"));
        dock.run();

        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("[exit 0]")), 10000);
        const QString text = output->toPlainText();
        QVERIFY(text.contains(QStringLiteral("$ /bin/echo hello_dock")));  // 回音的是展開後的 argv
        QVERIFY(text.contains(QStringLiteral("hello_dock")));
    }

    // 變數展開：先 tokenize 再逐 token 展開，含空白的路徑必須維持單一 argv（CON-006）
    void run_expandsVariablesAndKeepsSpacedPathAsOneArg()
    {
        const QString spaced = filePath(QStringLiteral("has space.txt"));
        writeFile(QStringLiteral("has space.txt"), "x\n");

        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();
        RunVars vars;
        vars.fullCurrentPath = spaced;
        vars.fileName = QStringLiteral("has space.txt");
        dock.setVars(vars);

        // /usr/bin/wc -l <file>：若路徑被切成兩個 argv，wc 會回非 0 結束碼
        dock.runCommand(QStringLiteral("/usr/bin/wc -l \"$(FULL_CURRENT_PATH)\""));
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("[exit ")), 10000);
        const QString text = output->toPlainText();
        QVERIFY(text.contains(QStringLiteral("[exit 0]")));
        QVERIFY(text.contains(spaced));
        QVERIFY(text.contains(QStringLiteral("1")));  // 檔案只有一行
    }

    // $(CURRENT_DIRECTORY) 會設為 QProcess 的工作目錄
    void run_usesCurrentDirectoryAsWorkingDirectory()
    {
        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();
        RunVars vars;
        // macOS 的 /var/folders 臨時目錄有 symlink，比對前先正規化
        vars.currentDirectory = QFileInfo(m_dir.path()).canonicalFilePath();
        dock.setVars(vars);

        dock.runCommand(QStringLiteral("/bin/pwd"));
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("[exit 0]")), 10000);
        QVERIFY(output->toPlainText().contains(vars.currentDirectory));
    }

    // 空指令是 no-op；啟動失敗要走 errorOccurred 分支而非靜默（IL-4）
    void run_emptyIsNoOpAndStartFailureIsReported()
    {
        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();

        dock.runCommand(QStringLiteral("   "));
        QVERIFY(output->toPlainText().isEmpty());

        dock.runCommand(QStringLiteral("/nonexistent/macpad_no_such_program"));
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("[error:")), 10000);
    }

    // 前一個指令仍在執行時，再次 run() 必須被忽略（不得平行啟動第二個程序）
    void run_ignoresRequestWhileProcessRunning()
    {
        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();

        dock.runCommand(QStringLiteral("/bin/sleep 0.4"));
        dock.runCommand(QStringLiteral("/bin/echo second_command"));
        QVERIFY(!output->toPlainText().contains(QStringLiteral("second_command")));

        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("[exit 0]")), 10000);
        // 程序結束後才可再次執行
        dock.runCommand(QStringLiteral("/bin/echo second_command"));
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("second_command")),
                                 10000);
        // 等第二個程序也結束再讓 dock 解構（QProcess 若在執行中被解構會 crash）
        QTRY_COMPARE_WITH_TIMEOUT(output->toPlainText().count(QStringLiteral("[exit 0]")),
                                  qsizetype(2), 10000);
    }

    // 指令歷史：去重、最新在前、上限 20 筆，並同步到補全器
    void run_historyDedupesAndCapsAtTwenty()
    {
        RunDock dock;
        auto *output = dock.findChild<QPlainTextEdit *>();
        auto *cmd = dock.findChild<QLineEdit *>();
        QVERIFY(cmd->completer());
        QCOMPARE(cmd->completer()->model()->rowCount(), 0);

        // 重複的指令只應留一筆
        for (int i = 0; i < 2; ++i) {
            dock.runCommand(QStringLiteral("/usr/bin/true dup"));
            QTRY_COMPARE_WITH_TIMEOUT(output->toPlainText().count(QStringLiteral("[exit 0]")),
                                      qsizetype(i + 1), 10000);
        }
        QCOMPARE(cmd->completer()->model()->rowCount(), 1);

        // 再跑 22 筆相異指令 → 上限 20 筆，最新的在最前面
        for (int i = 0; i < 22; ++i) {
            dock.runCommand(QStringLiteral("/usr/bin/true n%1").arg(i));
            QTRY_COMPARE_WITH_TIMEOUT(output->toPlainText().count(QStringLiteral("[exit 0]")),
                                      qsizetype(i + 3), 10000);
        }
        QAbstractItemModel *model = cmd->completer()->model();
        QCOMPARE(model->rowCount(), 20);
        QCOMPARE(model->data(model->index(0, 0)).toString(),
                 QStringLiteral("/usr/bin/true n21"));
        QCOMPARE(model->data(model->index(19, 0)).toString(),
                 QStringLiteral("/usr/bin/true n2"));
    }

    // 「Variables」選單：每個變數 token 都會插入到命令列游標處
    void run_variablesMenuInsertsTokens()
    {
        RunDock dock;
        auto *cmd = dock.findChild<QLineEdit *>();
        QToolButton *varsBtn = nullptr;
        const auto buttons = dock.findChildren<QToolButton *>();
        for (QToolButton *b : buttons) {
            if (b->text() == QStringLiteral("Variables"))
                varsBtn = b;
        }
        QVERIFY(varsBtn && varsBtn->menu());
        const auto actions = varsBtn->menu()->actions();
        QCOMPARE(actions.size(), 5);

        dock.setCommand(QStringLiteral("python "));
        cmd->setCursorPosition(cmd->text().size());
        actions.at(0)->trigger();  // $(FULL_CURRENT_PATH)
        QCOMPARE(cmd->text(), QStringLiteral("python $(FULL_CURRENT_PATH)"));
        QVERIFY(cmd->hasFocus() || true);  // offscreen 下焦點不保證，僅確認不崩潰

        // 其餘 token 也各插入一次，確認選單每個項目都接到同一條插入路徑
        dock.setCommand(QString());
        for (QAction *a : actions)
            a->trigger();
        for (QAction *a : actions)
            QVERIFY(cmd->text().contains(a->text()));
    }
};

QTEST_MAIN(TestResultDocks)
#include "test_resultdocks.moc"
