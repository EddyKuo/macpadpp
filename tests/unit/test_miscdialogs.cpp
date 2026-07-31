// 單元測試：三個「小對話框」的 UI 層行為
//   - ui/WindowsListDialog：視窗清單（排序 / 多選 / 四個動作 signal）
//   - ui/MacroManagerDialog：巨集管理（重新命名 / 刪除 / 指派快捷鍵）
//   - ui/ColumnEditorDialog：欄位編輯設定（數字模式 spec / 文字模式）
//
// 為什麼要另外測「對話框」：tests/unit/test_columneditor.cpp 測的是
// features/columneditor 的純邏輯（formatNumber / insertNumberColumn），
// 完全沒有碰到對話框把使用者輸入轉成 NumberSeqSpec 的那一段。這裡補的正是
// 「UI 控件 → 對外契約（spec()/signal）」這條路徑，兩者不重疊。
//
// 測試在 QT_QPA_PLATFORM=offscreen 下執行，因此本檔一律不直接呼叫任何阻塞的
// modal API。MacroManagerDialog 的三個動作內部會自己 exec() 子對話框
// （QInputDialog / QMessageBox / 自訂快捷鍵對話框），無法從外部繞過，
// 所以用下面的 ModalDriver：先啟動一個 QTimer，再觸發動作；計時器在巢狀事件
// 迴圈中抓住 activeModalWidget()，填值後按下按鈕把它關掉。即使沒有預期的
// modal 出現或處理器用盡，driver 也會強制關閉並記錄，確保測試永不卡死。

#include <QtTest>

#include <functional>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>

#include "ui/ColumnEditorDialog.h"
#include "ui/MacroManagerDialog.h"
#include "ui/WindowsListDialog.h"

using namespace macpad::ui;

namespace {

// 依按鈕文字（子字串）找按鈕——對話框沒有設 objectName，且測試不載入翻譯，
// 因此文字是目前唯一穩定的識別方式。
QPushButton *findButton(const QWidget *root, const QString &contains)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text().contains(contains, Qt::CaseInsensitive))
            return b;
    }
    return nullptr;
}

// 把 QFormLayout 的「標籤文字 → 欄位控件」攤平成表。
// 用標籤而非 findChildren 的順序來取控件：addRow() 會把控件 reparent，
// 依建立順序取用非常脆弱，改動 form 的排列就會靜默取錯控件。
QHash<QString, QWidget *> formFields(const QWidget *root)
{
    QHash<QString, QWidget *> map;
    const auto forms = root->findChildren<QFormLayout *>();
    for (QFormLayout *form : forms) {
        for (int r = 0; r < form->rowCount(); ++r) {
            QLayoutItem *labelItem = form->itemAt(r, QFormLayout::LabelRole);
            QLayoutItem *fieldItem = form->itemAt(r, QFormLayout::FieldRole);
            if (!labelItem || !fieldItem || !fieldItem->widget())
                continue;
            auto *label = qobject_cast<QLabel *>(labelItem->widget());
            if (label)
                map.insert(label->text(), fieldItem->widget());
        }
    }
    return map;
}

// 巢狀 modal 對話框的驅動器：見檔頭說明。
class ModalDriver : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(QWidget *)>;

    explicit ModalDriver(QWidget *owner) : m_owner(owner)
    {
        m_timer.setInterval(5);
        connect(&m_timer, &QTimer::timeout, this, &ModalDriver::tick);
    }

    void push(Handler h) { m_handlers.push_back(std::move(h)); }

    // 觸發會 exec() 的動作；trigger 內部阻塞期間由 m_timer 拆解 modal
    void run(const std::function<void()> &trigger)
    {
        m_idleTicks = 0;
        m_timer.start();
        trigger();
        m_timer.stop();
    }

    int handledCount() const { return m_handled; }
    int forcedCount() const { return m_forced; }   // 沒有對應 handler 而被強制關掉的 modal 數

private slots:
    void tick()
    {
        if (m_cooldown > 0) {   // 剛關掉一個 modal，等它真的消失再看下一個
            --m_cooldown;
            return;
        }
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal || modal == m_owner) {
            if (++m_idleTicks > 600)   // 約 3 秒沒有 modal：停掉計時器，避免空轉
                m_timer.stop();
            return;
        }
        m_idleTicks = 0;
        if (m_handlers.isEmpty()) {
            ++m_forced;   // 非預期的 modal：強制關閉，寧可測試失敗也不要卡死
            if (auto *dlg = qobject_cast<QDialog *>(modal))
                dlg->reject();
            else
                modal->close();
        } else {
            const Handler h = m_handlers.takeFirst();
            ++m_handled;
            h(modal);
        }
        m_cooldown = 2;
    }

private:
    QWidget *m_owner = nullptr;
    QTimer m_timer;
    QList<Handler> m_handlers;
    int m_handled = 0;
    int m_forced = 0;
    int m_cooldown = 0;
    int m_idleTicks = 0;
};

// 常用 handler：按下 QMessageBox 的某個標準鍵
ModalDriver::Handler clickMessageBox(QMessageBox::StandardButton which)
{
    return [which](QWidget *w) {
        auto *mb = qobject_cast<QMessageBox *>(w);
        QVERIFY(mb);
        QAbstractButton *b = mb->button(which);
        if (!b && !mb->buttons().isEmpty())
            b = mb->buttons().first();
        QVERIFY(b);
        b->click();
    };
}

// 常用 handler：填入 QInputDialog 的文字並按 OK / Cancel
ModalDriver::Handler answerInput(const QString &text, bool accept)
{
    return [text, accept](QWidget *w) {
        auto *dlg = qobject_cast<QInputDialog *>(w);
        QVERIFY(dlg);
        dlg->setTextValue(text);
        if (accept)
            dlg->accept();
        else
            dlg->reject();
    };
}

// 與 WindowsListDialog.cpp 內部一致：第一欄的 UserRole+2 存放原始 entries 索引。
// 測試不能假設「視覺列號 == entries 索引」——對話框建構時就已啟用排序，
// 表格會立刻依名稱欄重排，這正是 selectedRows() 存在的理由。
constexpr int kRowRole = Qt::UserRole + 2;

int originalIndex(const QTableWidget *table, int visualRow)
{
    return table->item(visualRow, 0)->data(kRowRole).toInt();
}

int visualRowOf(const QTableWidget *table, int original)
{
    for (int r = 0; r < table->rowCount(); ++r) {
        if (originalIndex(table, r) == original)
            return r;
    }
    return -1;
}

// 依「原始索引」建立多列選取。QTableWidget::selectRow() 會清掉既有選取，
// 因此多選必須直接操作 selectionModel（等同使用者按住 Ctrl 點選）。
void selectOriginals(QTableWidget *table, const QVector<int> &originals)
{
    table->clearSelection();
    for (int o : originals) {
        const int r = visualRowOf(table, o);
        QVERIFY(r >= 0);
        table->selectionModel()->select(
            table->model()->index(r, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

QVector<WindowsListEntry> sampleEntries()
{
    // 刻意讓「名稱字母序」「大小」「時間」三種排序結果互不相同，
    // 這樣排序測試才能真的分辨欄位是依值排序還是依字串排序。
    QVector<WindowsListEntry> v;
    WindowsListEntry a;
    a.name = QStringLiteral("alpha.cpp");
    a.path = QStringLiteral("/tmp/alpha.cpp");
    a.type = QStringLiteral("C++ file");
    a.size = 9000;   // 顯示會是 "8.79 KiB"，字典序會排在 "10..." 之後
    a.modified = QDateTime(QDate(2024, 5, 1), QTime(10, 0));
    WindowsListEntry b;
    b.name = QStringLiteral("charlie.txt");
    b.path = QStringLiteral("/tmp/charlie.txt");
    b.type = QStringLiteral("Normal text file");
    b.size = 120000;
    b.modified = QDateTime(QDate(2023, 1, 2), QTime(8, 30));
    WindowsListEntry c;
    c.name = QStringLiteral("bravo");
    c.path.clear();   // 未命名檔：路徑空、修改時間無效
    c.type = QStringLiteral("Normal text file");
    c.size = 12;
    v << a << b << c;
    return v;
}

}  // namespace

class TestMiscDialogs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // ---------------------------------------------------------------- WindowsList

    void windowsList_populatesTable()
    {
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->columnCount(), 5);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Name"));
        QCOMPARE(table->horizontalHeaderItem(4)->text(), QStringLiteral("Modified"));

        const int alpha = visualRowOf(table, 0);
        QVERIFY(alpha >= 0);
        QCOMPARE(table->item(alpha, 0)->text(), QStringLiteral("alpha.cpp"));
        QCOMPARE(table->item(alpha, 1)->text(), QStringLiteral("/tmp/alpha.cpp"));
        QCOMPARE(table->item(alpha, 2)->text(), QStringLiteral("C++ file"));
        QVERIFY(!table->item(alpha, 4)->text().isEmpty());

        // 未命名檔（空路徑 + 無效 QDateTime）：路徑與時間都應是空字串，不是 "Invalid"
        const int unnamed = visualRowOf(table, 2);
        QVERIFY(unnamed >= 0);
        QCOMPARE(table->item(unnamed, 0)->text(), QStringLiteral("bravo"));
        QCOMPARE(table->item(unnamed, 1)->text(), QString());
        QCOMPARE(table->item(unnamed, 4)->text(), QString());
        // 表格內容不可被就地編輯（只是清單，不是編輯器）
        QVERIFY(!(table->item(0, 0)->flags() & Qt::ItemIsEditable));
    }

    void windowsList_selectsFirstRowAndEnablesActions()
    {
        // 建構後應自動選取「表格第一列」（排序後的第一列，未必是 entries[0]）
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        QCOMPARE(dlg.selectedRows(), QVector<int>({originalIndex(table, 0)}));
        QVERIFY(findButton(&dlg, QStringLiteral("Activate"))->isEnabled());
        QVERIFY(findButton(&dlg, QStringLiteral("Save"))->isEnabled());
        QVERIFY(findButton(&dlg, QStringLiteral("Close Window"))->isEnabled());
        QVERIFY(findButton(&dlg, QStringLiteral("Sort Tabs"))->isEnabled());
    }

    void windowsList_emptyDisablesEverything()
    {
        // 沒有任何文件時所有動作都必須是 disabled，否則按下去會對空選取發 signal
        WindowsListDialog dlg({});
        QCOMPARE(dlg.findChild<QTableWidget *>()->rowCount(), 0);
        QVERIFY(dlg.selectedRows().isEmpty());
        QVERIFY(!findButton(&dlg, QStringLiteral("Activate"))->isEnabled());
        QVERIFY(!findButton(&dlg, QStringLiteral("Save"))->isEnabled());
        QVERIFY(!findButton(&dlg, QStringLiteral("Close Window"))->isEnabled());
        QVERIFY(!findButton(&dlg, QStringLiteral("Sort Tabs"))->isEnabled());
    }

    void windowsList_sortTabsNeedsMoreThanOneRow()
    {
        // 只有一個分頁時「排序分頁」無意義，須 disabled（其餘動作仍可用）
        QVector<WindowsListEntry> one{sampleEntries().first()};
        WindowsListDialog dlg(one);
        QVERIFY(findButton(&dlg, QStringLiteral("Activate"))->isEnabled());
        QVERIFY(!findButton(&dlg, QStringLiteral("Sort Tabs"))->isEnabled());
    }

    void windowsList_clearingSelectionDisablesActions()
    {
        // itemSelectionChanged → updateButtonStates 的連線
        WindowsListDialog dlg(sampleEntries());
        dlg.findChild<QTableWidget *>()->clearSelection();
        QVERIFY(dlg.selectedRows().isEmpty());
        QVERIFY(!findButton(&dlg, QStringLiteral("Activate"))->isEnabled());
        QVERIFY(!findButton(&dlg, QStringLiteral("Save"))->isEnabled());
    }

    void windowsList_sortsSizeByValueNotText()
    {
        // 大小欄若照顯示字串排序會得到 "12 bytes" < "117 KiB" < "8.79 KiB" 的錯誤結果；
        // 這裡驗證的是 SortableItem 用 UserRole 的 qint64 排序。
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        table->sortItems(3, Qt::AscendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("bravo"));        // 12
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("alpha.cpp"));    // 9000
        QCOMPARE(table->item(2, 0)->text(), QStringLiteral("charlie.txt"));  // 120000

        table->sortItems(3, Qt::DescendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("charlie.txt"));
        QCOMPARE(table->item(2, 0)->text(), QStringLiteral("bravo"));
    }

    void windowsList_sortsModifiedByDateTime()
    {
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        table->sortItems(4, Qt::AscendingOrder);
        // 無效時間（未命名檔）最小，其次 2023、2024
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("bravo"));
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("charlie.txt"));
        QCOMPARE(table->item(2, 0)->text(), QStringLiteral("alpha.cpp"));
    }

    void windowsList_sortsNameAsText()
    {
        // 名稱欄沒有排序鍵，應退回 QTableWidgetItem 的字串比較
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        table->sortItems(0, Qt::AscendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("alpha.cpp"));
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("bravo"));
        QCOMPARE(table->item(2, 0)->text(), QStringLiteral("charlie.txt"));
    }

    void windowsList_selectedRowsAreOriginalIndicesAfterSorting()
    {
        // 關鍵行為：排序後回報的必須是「建構時的 entries 索引」，不是視覺列號，
        // 否則 MainWindow 會對錯誤的分頁執行動作。
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        table->sortItems(3, Qt::AscendingOrder);   // 視覺順序：bravo(2), alpha(0), charlie(1)
        table->clearSelection();
        table->selectRow(0);
        QCOMPARE(dlg.selectedRows(), QVector<int>({2}));
        table->selectRow(2);
        QCOMPARE(dlg.selectedRows(), QVector<int>({1}));
    }

    void windowsList_multiSelectionReturnsSortedUniqueIndices()
    {
        // 每列有 5 欄，SelectRows 會回傳 5 個 item；selectedRows() 必須只取第一欄，
        // 且結果依原始索引升冪（此處以視覺順序 2,1 選取，期望輸出 1,2）
        WindowsListDialog dlg(sampleEntries());
        auto *table = dlg.findChild<QTableWidget *>();
        table->sortItems(3, Qt::AscendingOrder);   // bravo(2), alpha(0), charlie(1)
        selectOriginals(table, {2, 1});            // 刻意以「非升冪」的順序選取
        QCOMPARE(table->selectedItems().size(), 10);   // 2 列 × 5 欄
        QCOMPARE(dlg.selectedRows(), QVector<int>({1, 2}));
    }

    void windowsList_activateEmitsFirstRowAndAccepts()
    {
        WindowsListDialog dlg(sampleEntries());
        QSignalSpy activate(&dlg, &WindowsListDialog::activateRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto *table = dlg.findChild<QTableWidget *>();
        selectOriginals(table, {2, 1});
        findButton(&dlg, QStringLiteral("Activate"))->click();
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().first().toInt(), 1);   // 多選時啟動索引最小者
        QCOMPARE(accepted.count(), 1);
    }

    void windowsList_doubleClickActivates()
    {
        // 雙擊列 = Activate 的捷徑；用真的滑鼠事件驗證連線而不是直接呼叫 slot
        WindowsListDialog dlg(sampleEntries());
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QSignalSpy activate(&dlg, &WindowsListDialog::activateRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto *table = dlg.findChild<QTableWidget *>();
        const int row = visualRowOf(table, 1);
        QVERIFY(row >= 0);
        const QPoint pos = table->visualItemRect(table->item(row, 0)).center();
        // 先單擊選取該列，再雙擊。offscreen 平台下視窗剛顯示後的第一個 press
        // 會被吃掉（不會合成 DblClick），因此這個「先點一下」是必要的暖身。
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
        QCOMPARE(dlg.selectedRows(), QVector<int>({1}));
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().first().toInt(), 1);
        QCOMPARE(accepted.count(), 1);
    }

    void windowsList_saveEmitsAllSelectedAndKeepsDialogOpen()
    {
        // Save 是唯一「不關閉對話框」的動作（可連續存多批）
        WindowsListDialog dlg(sampleEntries());
        QSignalSpy save(&dlg, &WindowsListDialog::saveRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto *table = dlg.findChild<QTableWidget *>();
        selectOriginals(table, {2, 0});
        findButton(&dlg, QStringLiteral("Save"))->click();
        QCOMPARE(save.count(), 1);
        QCOMPARE(save.first().first().value<QVector<int>>(), QVector<int>({0, 2}));
        QCOMPARE(accepted.count(), 0);
    }

    void windowsList_closeEmitsSelectionAndAccepts()
    {
        WindowsListDialog dlg(sampleEntries());
        QSignalSpy closed(&dlg, &WindowsListDialog::closeRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto *table = dlg.findChild<QTableWidget *>();
        selectOriginals(table, {1});
        findButton(&dlg, QStringLiteral("Close Window"))->click();
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().first().value<QVector<int>>(), QVector<int>({1}));
        QCOMPARE(accepted.count(), 1);   // 分頁已關閉，清單失效 → 對話框關閉
    }

    void windowsList_sortTabsEmitsAndAccepts()
    {
        WindowsListDialog dlg(sampleEntries());
        QSignalSpy sorted(&dlg, &WindowsListDialog::sortTabsRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        findButton(&dlg, QStringLiteral("Sort Tabs"))->click();
        QCOMPARE(sorted.count(), 1);
        QCOMPARE(accepted.count(), 1);
    }

    void windowsList_okJustCloses()
    {
        WindowsListDialog dlg(sampleEntries());
        QSignalSpy activate(&dlg, &WindowsListDialog::activateRequested);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        findButton(&dlg, QStringLiteral("OK"))->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(activate.count(), 0);   // OK 不是 Activate，不可順手切分頁
    }

    // ---------------------------------------------------------------- MacroManager

    void macro_tableListsMacrosAlphabeticallyWithShortcuts()
    {
        MacroManagerDialog dlg;
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 0);

        QMap<QString, MacroData> macros;
        macros.insert(QStringLiteral("zeta"), MacroData{QKeySequence(QStringLiteral("Ctrl+Z"))});
        macros.insert(QStringLiteral("alpha"), MacroData{QKeySequence()});
        dlg.setMacros(macros);

        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("alpha"));
        QCOMPARE(table->item(0, 1)->text(), QString());   // 未指派快捷鍵 → 空
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("zeta"));
        QCOMPARE(table->item(1, 1)->text(), QKeySequence(QStringLiteral("Ctrl+Z")).toString());
        // 清單不可就地編輯，改名必須走 Rename 流程（才會發出 signal）
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
    }

    void macro_setMacrosCanBeCalledAgainToResync()
    {
        MacroManagerDialog dlg;
        auto *table = dlg.findChild<QTableWidget *>();
        dlg.setMacros({{QStringLiteral("one"), MacroData{}},
                       {QStringLiteral("two"), MacroData{}}});
        QCOMPARE(table->rowCount(), 2);
        dlg.setMacros({{QStringLiteral("only"), MacroData{}}});
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("only"));
    }

    void macro_actionsWithoutSelectionOnlyWarn()
    {
        // 空清單時三個動作都只能跳提示，絕不可發出任何變更 signal
        MacroManagerDialog dlg;
        QSignalSpy renamed(&dlg, &MacroManagerDialog::macroRenamed);
        QSignalSpy deleted(&dlg, &MacroManagerDialog::macroDeleted);
        QSignalSpy shortcut(&dlg, &MacroManagerDialog::macroShortcutChanged);

        for (const QString &label : {QStringLiteral("Shortcut"), QStringLiteral("Rename"),
                                     QStringLiteral("Delete")}) {
            ModalDriver driver(&dlg);
            driver.push(clickMessageBox(QMessageBox::Ok));
            QPushButton *b = findButton(&dlg, label);
            QVERIFY(b);
            driver.run([b] { b->click(); });
            QCOMPARE(driver.handledCount(), 1);
            QCOMPARE(driver.forcedCount(), 0);
        }
        QCOMPARE(renamed.count(), 0);
        QCOMPARE(deleted.count(), 0);
        QCOMPARE(shortcut.count(), 0);
    }

    void macro_renameEmitsAndUpdatesOrder()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{QKeySequence(QStringLiteral("Ctrl+B"))}},
                       {QStringLiteral("gamma"), MacroData{}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->selectRow(0);   // beta
        QSignalSpy renamed(&dlg, &MacroManagerDialog::macroRenamed);

        ModalDriver driver(&dlg);
        driver.push(answerInput(QStringLiteral("  zulu  "), true));   // 前後空白須被 trim
        QPushButton *b = findButton(&dlg, QStringLiteral("Rename"));
        driver.run([b] { b->click(); });

        QCOMPARE(driver.handledCount(), 1);
        QCOMPARE(driver.forcedCount(), 0);
        QCOMPARE(renamed.count(), 1);
        QCOMPARE(renamed.first().at(0).toString(), QStringLiteral("beta"));
        QCOMPARE(renamed.first().at(1).toString(), QStringLiteral("zulu"));
        // 重新命名後清單重新依字母序排列，且快捷鍵跟著搬過去
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("gamma"));
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("zulu"));
        QCOMPARE(table->item(1, 1)->text(), QKeySequence(QStringLiteral("Ctrl+B")).toString());
    }

    void macro_renameCancelledOrUnchangedDoesNothing()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{}}});
        dlg.findChild<QTableWidget *>()->selectRow(0);
        QSignalSpy renamed(&dlg, &MacroManagerDialog::macroRenamed);
        QPushButton *b = findButton(&dlg, QStringLiteral("Rename"));

        ModalDriver cancel(&dlg);
        cancel.push(answerInput(QStringLiteral("whatever"), false));   // 按 Cancel
        cancel.run([b] { b->click(); });

        ModalDriver same(&dlg);
        same.push(answerInput(QStringLiteral("beta"), true));          // 名稱沒變
        same.run([b] { b->click(); });

        ModalDriver blank(&dlg);
        blank.push(answerInput(QStringLiteral("   "), true));          // trim 後為空
        blank.run([b] { b->click(); });

        QCOMPARE(renamed.count(), 0);
        QCOMPARE(dlg.findChild<QTableWidget *>()->item(0, 0)->text(), QStringLiteral("beta"));
    }

    void macro_renameToExistingNameIsRejected()
    {
        // 撞名時會連開兩個 modal：QInputDialog 之後是警告框
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{}},
                       {QStringLiteral("gamma"), MacroData{}}});
        dlg.findChild<QTableWidget *>()->selectRow(0);   // beta
        QSignalSpy renamed(&dlg, &MacroManagerDialog::macroRenamed);

        ModalDriver driver(&dlg);
        driver.push(answerInput(QStringLiteral("gamma"), true));
        driver.push(clickMessageBox(QMessageBox::Ok));
        QPushButton *b = findButton(&dlg, QStringLiteral("Rename"));
        driver.run([b] { b->click(); });

        QCOMPARE(driver.handledCount(), 2);
        QCOMPARE(driver.forcedCount(), 0);
        QCOMPARE(renamed.count(), 0);
        auto *table = dlg.findChild<QTableWidget *>();
        QCOMPARE(table->rowCount(), 2);   // 兩個巨集都還在，沒有被覆蓋
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("beta"));
    }

    void macro_deleteConfirmedEmitsAndRemovesRow()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{}},
                       {QStringLiteral("gamma"), MacroData{}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->selectRow(1);   // gamma
        QSignalSpy deleted(&dlg, &MacroManagerDialog::macroDeleted);

        ModalDriver driver(&dlg);
        driver.push(clickMessageBox(QMessageBox::Yes));
        QPushButton *b = findButton(&dlg, QStringLiteral("Delete"));
        driver.run([b] { b->click(); });

        QCOMPARE(deleted.count(), 1);
        QCOMPARE(deleted.first().first().toString(), QStringLiteral("gamma"));
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("beta"));
    }

    void macro_deleteDeclinedKeepsMacro()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{}}});
        dlg.findChild<QTableWidget *>()->selectRow(0);
        QSignalSpy deleted(&dlg, &MacroManagerDialog::macroDeleted);

        ModalDriver driver(&dlg);
        driver.push(clickMessageBox(QMessageBox::No));
        QPushButton *b = findButton(&dlg, QStringLiteral("Delete"));
        driver.run([b] { b->click(); });

        QCOMPARE(deleted.count(), 0);
        QCOMPARE(dlg.findChild<QTableWidget *>()->rowCount(), 1);
    }

    void macro_modifyShortcutEmitsNewSequence()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{QKeySequence(QStringLiteral("Ctrl+B"))}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->selectRow(0);
        QSignalSpy changed(&dlg, &MacroManagerDialog::macroShortcutChanged);

        const QKeySequence want(QStringLiteral("Ctrl+Shift+M"));
        ModalDriver driver(&dlg);
        driver.push([want](QWidget *w) {
            // 子對話框應帶入目前快捷鍵作為預設值，並顯示巨集名稱
            auto *edit = w->findChild<QKeySequenceEdit *>();
            QVERIFY(edit);
            QCOMPARE(edit->keySequence(), QKeySequence(QStringLiteral("Ctrl+B")));
            edit->setKeySequence(want);
            auto *box = w->findChild<QDialogButtonBox *>();
            QVERIFY(box);
            box->button(QDialogButtonBox::Ok)->click();
        });
        QPushButton *b = findButton(&dlg, QStringLiteral("Shortcut"));
        driver.run([b] { b->click(); });

        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toString(), QStringLiteral("beta"));
        QCOMPARE(changed.first().at(1).value<QKeySequence>(), want);
        QCOMPARE(table->item(0, 1)->text(), want.toString());   // 表格即時更新
    }

    void macro_modifyShortcutResetClearsSequence()
    {
        // Reset 用來「取消指派」——應發出空 QKeySequence，而不是不發 signal
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{QKeySequence(QStringLiteral("Ctrl+B"))}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->selectRow(0);
        QSignalSpy changed(&dlg, &MacroManagerDialog::macroShortcutChanged);

        ModalDriver driver(&dlg);
        driver.push([](QWidget *w) {
            auto *box = w->findChild<QDialogButtonBox *>();
            QVERIFY(box);
            box->button(QDialogButtonBox::Reset)->click();
            box->button(QDialogButtonBox::Ok)->click();
        });
        QPushButton *b = findButton(&dlg, QStringLiteral("Shortcut"));
        driver.run([b] { b->click(); });

        QCOMPARE(changed.count(), 1);
        QVERIFY(changed.first().at(1).value<QKeySequence>().isEmpty());
        QCOMPARE(table->item(0, 1)->text(), QString());
    }

    void macro_modifyShortcutCancelKeepsOldSequence()
    {
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{QKeySequence(QStringLiteral("Ctrl+B"))}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->selectRow(0);
        QSignalSpy changed(&dlg, &MacroManagerDialog::macroShortcutChanged);

        ModalDriver driver(&dlg);
        driver.push([](QWidget *w) {
            auto *edit = w->findChild<QKeySequenceEdit *>();
            QVERIFY(edit);
            edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+K")));
            auto *box = w->findChild<QDialogButtonBox *>();
            box->button(QDialogButtonBox::Cancel)->click();
        });
        QPushButton *b = findButton(&dlg, QStringLiteral("Shortcut"));
        driver.run([b] { b->click(); });

        QCOMPARE(changed.count(), 0);
        QCOMPARE(table->item(0, 1)->text(), QKeySequence(QStringLiteral("Ctrl+B")).toString());
    }

    void macro_fallsBackToCurrentRowWhenSelectionEmpty()
    {
        // 有 current cell 但沒有 selection（例如選取被程式清掉）時，
        // 仍應以 current row 為操作對象，而不是誤判為「未選取」。
        MacroManagerDialog dlg;
        dlg.setMacros({{QStringLiteral("beta"), MacroData{}},
                       {QStringLiteral("gamma"), MacroData{}}});
        auto *table = dlg.findChild<QTableWidget *>();
        table->setCurrentCell(1, 0);
        table->clearSelection();
        QVERIFY(table->selectionModel()->selectedRows().isEmpty());
        QSignalSpy deleted(&dlg, &MacroManagerDialog::macroDeleted);

        ModalDriver driver(&dlg);
        driver.push(clickMessageBox(QMessageBox::Yes));
        QPushButton *b = findButton(&dlg, QStringLiteral("Delete"));
        driver.run([b] { b->click(); });

        QCOMPARE(deleted.count(), 1);
        QCOMPARE(deleted.first().first().toString(), QStringLiteral("gamma"));
    }

    void macro_closeButtonAcceptsDialog()
    {
        MacroManagerDialog dlg;
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        auto *box = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(box);
        box->button(QDialogButtonBox::Close)->click();
        QCOMPARE(accepted.count(), 1);
    }

    // ---------------------------------------------------------------- ColumnEditor

    void column_defaultsMatchNotepadPlusPlus()
    {
        ColumnEditorDialog dlg;
        QVERIFY(!dlg.isTextMode());   // 預設 Number 模式
        const auto s = dlg.spec();
        QCOMPARE(s.start, 1);
        QCOMPARE(s.increment, 1);
        QCOMPARE(s.base, 10);
        QCOMPARE(s.width, 0);
        QVERIFY(s.upperHex);
        QCOMPARE(dlg.repeatCount(), 1);
        QCOMPARE(dlg.textToInsert(), QString());
    }

    void column_specReflectsNumberFields()
    {
        ColumnEditorDialog dlg;
        const auto fields = formFields(&dlg);
        auto *start = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Initial number")));
        auto *inc = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Increase by")));
        auto *repeat = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Repeat")));
        auto *width =
            qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Leading zeros (width)")));
        QVERIFY(start && inc && repeat && width);

        start->setValue(100);
        inc->setValue(-5);   // 遞減數列也必須能表達
        repeat->setValue(3);
        width->setValue(4);

        const auto s = dlg.spec();
        QCOMPARE(s.start, 100);
        QCOMPARE(s.increment, -5);
        QCOMPARE(s.width, 4);
        QCOMPARE(dlg.repeatCount(), 3);
    }

    void column_spinBoxRangesAllowNegativeAndLargeValues()
    {
        ColumnEditorDialog dlg;
        const auto fields = formFields(&dlg);
        auto *start = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Initial number")));
        auto *repeat = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Repeat")));
        auto *width =
            qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Leading zeros (width)")));
        QVERIFY(start && repeat && width);
        // 起始值可為負；重複次數最小為 1（0 會導致除以零）；補零寬度上限 20
        QCOMPARE(start->minimum(), -1000000);
        QCOMPARE(repeat->minimum(), 1);
        QCOMPARE(width->minimum(), 0);
        QCOMPARE(width->maximum(), 20);
    }

    void column_baseSelectionChangesSpecAndInputDisplay()
    {
        // Notepad++ v8.8.6：選 Hex 後輸入欄本身也要以 16 進位顯示/接受輸入
        ColumnEditorDialog dlg;
        const auto fields = formFields(&dlg);
        auto *base = qobject_cast<QComboBox *>(fields.value(QStringLiteral("Base")));
        auto *start = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Initial number")));
        auto *inc = qobject_cast<QSpinBox *>(fields.value(QStringLiteral("Increase by")));
        QVERIFY(base && start && inc);
        QCOMPARE(base->count(), 4);

        base->setCurrentIndex(base->findData(16));
        QCOMPARE(dlg.spec().base, 16);
        QCOMPARE(start->displayIntegerBase(), 16);
        QCOMPARE(inc->displayIntegerBase(), 16);
        start->setValue(255);
        QCOMPARE(start->text(), QStringLiteral("ff"));

        base->setCurrentIndex(base->findData(8));
        QCOMPARE(dlg.spec().base, 8);
        QCOMPARE(start->text(), QStringLiteral("377"));

        base->setCurrentIndex(base->findData(2));
        QCOMPARE(dlg.spec().base, 2);

        base->setCurrentIndex(base->findData(10));
        QCOMPARE(dlg.spec().base, 10);
        QCOMPARE(start->text(), QStringLiteral("255"));
    }

    void column_switchingToTextModeSwapsPages()
    {
        ColumnEditorDialog dlg;
        dlg.show();   // 可見性只有在對話框顯示後才有意義（不使用 exec()）
        const auto fields = formFields(&dlg);
        auto *numberField = fields.value(QStringLiteral("Initial number"));
        auto *textField = qobject_cast<QLineEdit *>(fields.value(QStringLiteral("Text to insert")));
        QVERIFY(numberField && textField);
        QVERIFY(numberField->isVisible());
        QVERIFY(!textField->isVisible());
        QVERIFY(!textField->placeholderText().isEmpty());

        // 兩個 radio 都沒有 objectName，用顯示文字辨識
        QRadioButton *number = nullptr;
        QRadioButton *text = nullptr;
        const auto radios = dlg.findChildren<QRadioButton *>();
        for (QRadioButton *r : radios) {
            if (r->text() == QStringLiteral("Number"))
                number = r;
            else if (r->text() == QStringLiteral("Text"))
                text = r;
        }
        QVERIFY(number && text);
        QVERIFY(number->isChecked());

        text->setChecked(true);
        QVERIFY(dlg.isTextMode());
        QVERIFY(!number->isChecked());   // 互斥（同一 QButtonGroup）
        QVERIFY(!numberField->isVisible());
        QVERIFY(textField->isVisible());

        textField->setText(QStringLiteral("// TODO: "));
        QCOMPARE(dlg.textToInsert(), QStringLiteral("// TODO: "));

        number->setChecked(true);
        QVERIFY(!dlg.isTextMode());
        QVERIFY(numberField->isVisible());
        QVERIFY(!textField->isVisible());
    }

    void column_okAndCancelResolveDialog()
    {
        ColumnEditorDialog ok;
        QSignalSpy accepted(&ok, &QDialog::accepted);
        ok.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(ok.result(), int(QDialog::Accepted));

        ColumnEditorDialog cancel;
        QSignalSpy rejected(&cancel, &QDialog::rejected);
        cancel.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(cancel.result(), int(QDialog::Rejected));
    }
};

QTEST_MAIN(TestMiscDialogs)
#include "test_miscdialogs.moc"
