// 單元測試：WorkspaceDock（Folder as Workspace，FR-030）與 ProjectPanelDock（Project Panel）
//
// 這兩個 dock 的核心邏輯都藏在「與 QTreeWidget 互動」與「右鍵選單 action」裡，
// 因此本測試以真實的暫存目錄樹（QTemporaryDir）驅動它們，並直接檢查樹狀節點的內容，
// 而不是只建構物件了事。
//
// === 為什麼需要 openContextMenu()／DialogResponder 這兩個輔助設施？ ===
// 兩個 dock 的 showContextMenu() 結尾是 QMenu::exec()，而多數 action 的 lambda 內部
// 又會叫出 QInputDialog / QMessageBox / QFileDialog 的 modal 靜態函式。
// 在 QT_QPA_PLATFORM=offscreen 底下沒有人可以按按鈕，直接呼叫會讓測試永久卡死。
// 解法是「在 exec() 進入巢狀事件迴圈之後，用 timer 從外部操作它」：
//   * openContextMenu()：以 timer 找出彈出的 QMenu（它是 QTreeWidget 的直接子物件），
//     擷取 action 清單、選擇性觸發其中一個，最後 close() 讓 exec() 返回。
//   * DialogResponder：全程運作的 timer，掃到 modal 對話框就依「腳本」回應
//     （輸入文字並 accept／取消／按 Yes 或 Ok），讓 action lambda 的完整路徑得以執行。
// 測試全程不呼叫任何會阻塞的 modal API，也不會真的開啟 Finder／終端機（見 guardsEmptyPaths）。

#include <QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <functional>

#include "persistence/AppPaths.h"
#include "persistence/ProjectStore.h"
#include "ui/ProjectPanelDock.h"
#include "ui/WorkspaceDock.h"

using macpad::persistence::AppPaths;
using macpad::persistence::Project;
using macpad::persistence::ProjectNode;
using macpad::persistence::ProjectNodeType;
using macpad::persistence::ProjectStore;
using macpad::persistence::ProjectWorkspace;
using macpad::ui::ProjectPanelDock;
using macpad::ui::WorkspaceDock;

namespace {

// WorkspaceDock 內部用來記錄節點路徑的 role（與 WorkspaceDock.cpp 的 kPathRole 一致）。
// 測試在 guardsEmptyPaths 需要把節點路徑清空，以走到 lambda 的「空路徑就不外呼」守衛分支，
// 避免真的把 Finder／終端機叫起來。
constexpr int kWsPathRole = Qt::UserRole + 1;

// --- 對話框自動應答腳本 ---------------------------------------------------
struct DialogScript {
    bool acceptInput = false;   // QInputDialog：true=填入 inputText 後 accept，false=按取消
    QString inputText;
    int messageButton = static_cast<int>(QMessageBox::No);  // QMessageBox 要按的標準按鈕
    int inputSeen = 0;          // 實際回應次數；用來斷言「對話框真的被叫出來過」
    int messageSeen = 0;
    int fileSeen = 0;
};
DialogScript g_script;

// 掃描目前顯示中的 modal 對話框並依腳本回應。由 DialogResponder 的 timer 週期呼叫。
void respondToVisibleDialog()
{
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (!w->isVisible())
            continue;
        if (auto *input = qobject_cast<QInputDialog *>(w)) {
            ++g_script.inputSeen;
            if (g_script.acceptInput) {
                input->setTextValue(g_script.inputText);
                input->accept();
            } else {
                input->reject();
            }
            return;
        }
        if (auto *box = qobject_cast<QMessageBox *>(w)) {
            ++g_script.messageSeen;
            // question() 有 Yes/No，warning() 只有 Ok；找不到指定按鈕就退回按 Ok。
            QAbstractButton *btn =
                box->button(static_cast<QMessageBox::StandardButton>(g_script.messageButton));
            if (!btn)
                btn = box->button(QMessageBox::Ok);
            if (btn)
                btn->click();
            else
                box->reject();
            return;
        }
        if (auto *fd = qobject_cast<QFileDialog *>(w)) {
            // 檔案／目錄選擇一律取消：測試不依賴檔案對話框的瀏覽行為，
            // 只驗證「使用者取消時呼叫端不應有任何副作用」。
            ++g_script.fileSeen;
            fd->reject();
            return;
        }
    }
}

// 整個測試期間常駐的回應器。
class DialogResponder {
public:
    DialogResponder()
    {
        m_timer.setInterval(1);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, []() { respondToVisibleDialog(); });
        m_timer.start();
    }

private:
    QTimer m_timer;
};

// --- 右鍵選單擷取 ---------------------------------------------------------
struct MenuEntry {
    QString text;
    bool enabled = false;
    bool separator = false;
    bool checkable = false;
    bool checked = false;
};

QStringList entryTexts(const QVector<MenuEntry> &entries)
{
    QStringList out;
    for (const MenuEntry &e : entries) {
        if (!e.separator)
            out << e.text;
    }
    return out;
}

const MenuEntry *entryNamed(const QVector<MenuEntry> &entries, const QString &text)
{
    for (const MenuEntry &e : entries) {
        if (e.text == text)
            return &e;
    }
    return nullptr;
}

QAction *actionNamed(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions()) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

using MenuHandler = std::function<void(QMenu *)>;

// 對 tree 送出 customContextMenuRequested(pos)，等 dock 的 showContextMenu() 把 QMenu 彈出來，
// 擷取所有 action 的狀態，選擇性執行 handler（通常是觸發某個 action），最後關閉選單。
QVector<MenuEntry> openContextMenu(QTreeWidget *tree, const QPoint &pos,
                                   const MenuHandler &handler = MenuHandler())
{
    QVector<MenuEntry> entries;
    QTimer probe;
    probe.setInterval(1);
    QObject::connect(&probe, &QTimer::timeout, &probe, [&]() {
        // QMenu 以 m_tree 為 QObject parent 建立在 showContextMenu() 的堆疊上，
        // 因此只在 exec() 期間存在，用 findChildren 一定抓得到、也不會誤抓別的選單。
        const QList<QMenu *> menus = tree->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        if (menus.isEmpty())
            return;
        probe.stop();
        QMenu *menu = menus.first();
        for (QAction *a : menu->actions()) {
            MenuEntry e;
            e.text = a->text();
            e.enabled = a->isEnabled();
            e.separator = a->isSeparator();
            e.checkable = a->isCheckable();
            e.checked = a->isChecked();
            entries.push_back(e);
        }
        if (handler)
            handler(menu);
        menu->close();
    });
    probe.start();
    QMetaObject::invokeMethod(tree, "customContextMenuRequested", Q_ARG(QPoint, pos));
    probe.stop();
    return entries;
}

// --- 樹狀節點查詢 ---------------------------------------------------------
QStringList childNames(const QTreeWidgetItem *parent)
{
    QStringList out;
    for (int i = 0; i < parent->childCount(); ++i)
        out << parent->child(i)->text(0);
    return out;
}

QTreeWidgetItem *childNamed(QTreeWidgetItem *parent, const QString &name)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name)
            return parent->child(i);
    }
    return nullptr;
}

void writeFile(const QString &path, const QByteArray &content = QByteArray("x"))
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(content);
    f.close();
}

}  // namespace

class TestWorkspaceDocks : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_root;
    DialogResponder *m_responder = nullptr;

    // 每個測試都重建一份乾淨的目錄樹：
    //   root/.hidden_dir/  root/.hidden.txt   ← 驗證 Show Hidden Files
    //   root/alpha/nested/deep.txt            ← 驗證延遲展開（placeholder）
    //   root/alpha/a1.cpp
    //   root/beta/                            ← 空目錄
    //   root/main.cpp  root/notes.txt  root/readme.md
    void buildTree()
    {
        QDir d(m_root);
        d.mkpath(QStringLiteral(".hidden_dir"));
        d.mkpath(QStringLiteral("alpha/nested"));
        d.mkpath(QStringLiteral("beta"));
        writeFile(d.filePath(QStringLiteral(".hidden.txt")));
        writeFile(d.filePath(QStringLiteral("alpha/a1.cpp")));
        writeFile(d.filePath(QStringLiteral("alpha/nested/deep.txt")));
        writeFile(d.filePath(QStringLiteral("main.cpp")));
        writeFile(d.filePath(QStringLiteral("notes.txt")));
        writeFile(d.filePath(QStringLiteral("readme.md")));
    }

    // 取得 dock 的內部樹；並讓它有真實幾何（visualItemRect 需要），才能用座標打開右鍵選單。
    static QTreeWidget *showAndTree(QDockWidget *dock)
    {
        dock->resize(360, 520);
        dock->show();
        QCoreApplication::processEvents();
        return dock->findChild<QTreeWidget *>();
    }

private slots:
    void initTestCase()
    {
        // ProjectStore 寫入 AppPaths 目錄。除了測試模式，再把設定目錄導到本測試專屬的
        // 暫存目錄，避免與同樣操作 projects.json 的 test_projectstore 平行執行時互相干擾。
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_tmp.isValid());
        AppPaths::setConfigDirOverride(m_tmp.filePath(QStringLiteral("config")));
        m_responder = new DialogResponder;
    }

    void cleanupTestCase()
    {
        delete m_responder;
        m_responder = nullptr;
        AppPaths::setConfigDirOverride(QString());
    }

    void init()
    {
        g_script = DialogScript();
        QFile::remove(AppPaths::filePath(QStringLiteral("projects.json")));

        // 每個測試獨立一棵目錄樹，避免前一個測試的建檔／改名互相影響。
        m_root = m_tmp.filePath(QStringLiteral("case%1").arg(++m_caseCounter));
        QVERIFY(QDir().mkpath(m_root));
        buildTree();
    }

    // ==================== WorkspaceDock ====================

    // addRoot 應建立根節點、列出「資料夾在前、檔案在後」的內容，
    // 且資料夾節點帶一個 placeholder 子節點（延遲展開）。
    void wsAddRootPopulates()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        QVERIFY(tree);
        dock.addRoot(m_root);

        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem *root = tree->topLevelItem(0);
        QCOMPARE(root->text(0), QFileInfo(m_root).fileName());
        QVERIFY(root->isExpanded());

        // 3 個資料夾（含隱藏）+ 4 個檔案（含隱藏）
        const QStringList names = childNames(root);
        QCOMPARE(names.size(), 7);
        QVERIFY(names.indexOf(QStringLiteral("beta")) < names.indexOf(QStringLiteral("main.cpp")));
        QVERIFY(names.contains(QStringLiteral(".hidden_dir")));
        QVERIFY(names.contains(QStringLiteral(".hidden.txt")));

        // 資料夾 → 有 placeholder；檔案 → 無子節點
        QTreeWidgetItem *alpha = childNamed(root, QStringLiteral("alpha"));
        QVERIFY(alpha);
        QCOMPARE(alpha->childCount(), 1);
        QCOMPARE(alpha->data(0, Qt::UserRole + 2).toBool(), true);   // isDir
        QTreeWidgetItem *mainCpp = childNamed(root, QStringLiteral("main.cpp"));
        QVERIFY(mainCpp);
        QCOMPARE(mainCpp->childCount(), 0);
        QCOMPARE(mainCpp->data(0, Qt::UserRole + 2).toBool(), false);

        QCOMPARE(dock.roots(), QStringList{QDir(m_root).absolutePath()});
    }

    // 重複 addRoot（含非正規化路徑）應被忽略；setRoot 則取代全部根目錄。
    void wsAddRootDedupAndSetRoot()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        dock.addRoot(m_root + QStringLiteral("/alpha/.."));   // 正規化後同一個路徑
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(dock.roots().size(), 1);

        // 多根：再加一個子目錄當第二個根
        dock.addRoot(m_root + QStringLiteral("/alpha"));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(dock.roots().size(), 2);

        // setRoot 清空既有根目錄
        dock.setRoot(m_root + QStringLiteral("/beta"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(dock.roots(), QStringList{QDir(m_root + QStringLiteral("/beta")).absolutePath()});
    }

    // removeRoot：存在則移除節點與清單，不存在則安全無動作。
    void wsRemoveRoot()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        dock.addRoot(m_root + QStringLiteral("/alpha"));

        dock.removeRoot(QStringLiteral("/definitely/not/added"));
        QCOMPARE(tree->topLevelItemCount(), 2);

        dock.removeRoot(m_root);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(dock.roots(), QStringList{QDir(m_root + QStringLiteral("/alpha")).absolutePath()});
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("alpha"));
    }

    // 展開資料夾時才真正讀取磁碟（placeholder 被實際內容取代）；
    // 已展開過的節點再次展開不應重複讀取。
    void wsLazyExpand()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *alpha = childNamed(tree->topLevelItem(0), QStringLiteral("alpha"));
        QVERIFY(alpha);
        QCOMPARE(alpha->childCount(), 1);   // 只有 placeholder

        alpha->setExpanded(true);           // 觸發 itemExpanded → onItemExpanded
        QCOMPARE(childNames(alpha), (QStringList{QStringLiteral("nested"), QStringLiteral("a1.cpp")}));

        // 再收合再展開：內容已存在，onItemExpanded 應提早 return（不會重複塞入）
        alpha->setExpanded(false);
        alpha->setExpanded(true);
        QCOMPARE(alpha->childCount(), 2);

        // 檔案節點沒有子節點 → onItemExpanded 的 childCount 守衛
        QTreeWidgetItem *mainCpp = childNamed(tree->topLevelItem(0), QStringLiteral("main.cpp"));
        QVERIFY(mainCpp);
        mainCpp->setExpanded(true);
        QCOMPARE(mainCpp->childCount(), 0);
    }

    // 檔名過濾只作用於檔案，資料夾一律保留；清空過濾器會還原。
    void wsNameFilters()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);

        dock.setNameFilters({QStringLiteral("*.cpp")});
        QStringList names = childNames(tree->topLevelItem(0));
        QCOMPARE(names.size(), 4);   // 3 資料夾 + main.cpp
        QVERIFY(names.contains(QStringLiteral("main.cpp")));
        QVERIFY(!names.contains(QStringLiteral("notes.txt")));
        QVERIFY(names.contains(QStringLiteral("alpha")));

        // 過濾器也要套用到之後展開的子目錄
        QTreeWidgetItem *alpha = childNamed(tree->topLevelItem(0), QStringLiteral("alpha"));
        alpha->setExpanded(true);
        QCOMPARE(childNames(alpha), (QStringList{QStringLiteral("nested"), QStringLiteral("a1.cpp")}));

        dock.setNameFilters(QStringList());
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), 7);
    }

    // 展開狀態的保存與還原：expandedPaths 回傳排序後的絕對路徑，
    // setExpandedPaths 需能逐層 populate 出深層節點再展開。
    void wsExpandedPathsRoundTrip()
    {
        const QString absRoot = QDir(m_root).absolutePath();
        WorkspaceDock src;
        QTreeWidget *srcTree = showAndTree(&src);
        src.addRoot(m_root);
        QTreeWidgetItem *alpha = childNamed(srcTree->topLevelItem(0), QStringLiteral("alpha"));
        alpha->setExpanded(true);
        childNamed(alpha, QStringLiteral("nested"))->setExpanded(true);

        const QStringList expanded = src.expandedPaths();
        QStringList want{absRoot, absRoot + QStringLiteral("/alpha"),
                         absRoot + QStringLiteral("/alpha/nested")};
        want.sort();
        QCOMPARE(expanded, want);

        // 還原到另一個全新的 dock
        WorkspaceDock dst;
        QTreeWidget *dstTree = showAndTree(&dst);
        dst.addRoot(m_root);
        // 混入一個不存在的路徑，驗證安全忽略
        QStringList toRestore = expanded;
        toRestore << absRoot + QStringLiteral("/no/such/dir");
        dst.setExpandedPaths(toRestore);
        QCOMPARE(dst.expandedPaths(), want);

        QTreeWidgetItem *dstAlpha = childNamed(dstTree->topLevelItem(0), QStringLiteral("alpha"));
        QVERIFY(dstAlpha);
        QTreeWidgetItem *dstNested = childNamed(dstAlpha, QStringLiteral("nested"));
        QVERIFY(dstNested);
        QVERIFY(dstNested->isExpanded());
        QCOMPARE(childNames(dstNested), QStringList{QStringLiteral("deep.txt")});
    }

    // 雙擊：只有檔案節點才 emit fileActivated；資料夾與 placeholder 不 emit。
    void wsDoubleClickEmitsForFilesOnly()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QSignalSpy spy(&dock, &WorkspaceDock::fileActivated);

        QTreeWidgetItem *root = tree->topLevelItem(0);
        QTreeWidgetItem *mainCpp = childNamed(root, QStringLiteral("main.cpp"));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, mainCpp), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QDir(m_root).absoluteFilePath(QStringLiteral("main.cpp")));

        // 資料夾節點：不 emit
        QTreeWidgetItem *alpha = childNamed(root, QStringLiteral("alpha"));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, alpha), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);

        // placeholder 節點：不 emit（它沒有路徑，雙擊必須無害）
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, alpha->child(0)), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);

        // 空白處雙擊（item == nullptr）
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, nullptr), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);
    }

    // 空白處右鍵：只有與「整個工作區」有關的三個項目，不出現節點相關動作。
    void wsContextMenuOnEmptyArea()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);

        const QPoint emptyPos(4, tree->viewport()->height() - 4);
        QVERIFY(!tree->itemAt(emptyPos));   // 確認真的是空白處，否則此測試無意義

        const QVector<MenuEntry> entries = openContextMenu(tree, emptyPos);
        QCOMPARE(entryTexts(entries),
                 (QStringList{QStringLiteral("Add Folder…"), QStringLiteral("Set Filter…"),
                              QStringLiteral("Show Hidden Files")}));
        const MenuEntry *hidden = entryNamed(entries, QStringLiteral("Show Hidden Files"));
        QVERIFY(hidden);
        QVERIFY(hidden->checkable);
        QVERIFY(hidden->checked);   // 預設顯示隱藏檔
    }

    // 根節點右鍵：Remove Root 可用，Rename/Delete 停用（根不能改名或刪除）。
    void wsContextMenuOnRootItem()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint pos = tree->visualItemRect(tree->topLevelItem(0)).center();

        const QVector<MenuEntry> entries = openContextMenu(tree, pos);
        const QStringList texts = entryTexts(entries);
        QVERIFY(texts.contains(QStringLiteral("Remove Root")));
        QVERIFY(texts.contains(QStringLiteral("New File…")));
        QVERIFY(texts.contains(QStringLiteral("Copy Full Path")));
        QVERIFY(texts.contains(QStringLiteral("Find in This Folder…")));
        QVERIFY(texts.contains(QStringLiteral("Reveal in Finder")));
        QVERIFY(texts.contains(QStringLiteral("Open Terminal Here")));
        // 根節點是資料夾 → 不提供「以預設應用程式開啟」
        QVERIFY(!texts.contains(QStringLiteral("Open in Default Application")));

        QVERIFY(entryNamed(entries, QStringLiteral("Remove Root"))->enabled);
        QVERIFY(!entryNamed(entries, QStringLiteral("Rename…"))->enabled);
        QVERIFY(!entryNamed(entries, QStringLiteral("Delete"))->enabled);
    }

    // 檔案節點右鍵：Remove Root 停用、Rename/Delete 可用，且多一個「以預設應用程式開啟」。
    void wsContextMenuOnFileItem()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("main.cpp"));
        const QVector<MenuEntry> entries = openContextMenu(tree, tree->visualItemRect(file).center());

        QVERIFY(!entryNamed(entries, QStringLiteral("Remove Root"))->enabled);
        QVERIFY(entryNamed(entries, QStringLiteral("Rename…"))->enabled);
        QVERIFY(entryNamed(entries, QStringLiteral("Delete"))->enabled);
        QVERIFY(entryNamed(entries, QStringLiteral("Open in Default Application")));
    }

    // placeholder 節點上按右鍵：只會出現工作區層級的三個項目
    // （placeholder 沒有路徑，對它做新增/改名/刪除都是未定義行為，必須被擋掉）。
    void wsContextMenuOnPlaceholder()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *alpha = childNamed(tree->topLevelItem(0), QStringLiteral("alpha"));
        QVERIFY(alpha);
        // 擋掉 itemExpanded 訊號才能讓 alpha 展開但「不」被 populate，
        // 這樣它底下的 placeholder 才會真的顯示出來、可以用座標點到。
        tree->blockSignals(true);
        alpha->setExpanded(true);
        tree->blockSignals(false);
        QCOMPARE(alpha->childCount(), 1);
        QTreeWidgetItem *placeholder = alpha->child(0);

        const QVector<MenuEntry> entries =
            openContextMenu(tree, tree->visualItemRect(placeholder).center());
        QCOMPARE(entryTexts(entries),
                 (QStringList{QStringLiteral("Add Folder…"), QStringLiteral("Set Filter…"),
                              QStringLiteral("Show Hidden Files")}));

        // 空目錄展開後 placeholder 應被清除（不留下假的子節點）
        QTreeWidgetItem *beta = childNamed(tree->topLevelItem(0), QStringLiteral("beta"));
        QVERIFY(beta);
        beta->setExpanded(true);
        QCOMPARE(beta->childCount(), 0);
    }

    // 觸發「Remove Root」action，根目錄應真的從工作區消失。
    void wsTriggerRemoveRoot()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint pos = tree->visualItemRect(tree->topLevelItem(0)).center();

        openContextMenu(tree, pos, [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Remove Root"));
            QVERIFY(a);
            a->trigger();
        });
        QVERIFY(dock.roots().isEmpty());
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    // 「Copy Full Path」／「Copy File Name」把對應字串放進剪貼簿。
    void wsTriggerCopyActions()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("notes.txt"));
        const QPoint pos = tree->visualItemRect(file).center();
        const QString absPath = QDir(m_root).absoluteFilePath(QStringLiteral("notes.txt"));

        QApplication::clipboard()->setText(QStringLiteral("<sentinel>"));
        openContextMenu(tree, pos, [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Copy Full Path"));
            QVERIFY(a);
            a->trigger();
        });
        QCOMPARE(QApplication::clipboard()->text(), absPath);

        openContextMenu(tree, pos, [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Copy File Name"));
            QVERIFY(a);
            a->trigger();
        });
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("notes.txt"));
    }

    // 「Find in This Folder…」：對檔案要送出其所在目錄，對資料夾送出自身。
    void wsTriggerFindInFolder()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QSignalSpy spy(&dock, &WorkspaceDock::findInFolderRequested);

        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("main.cpp"));
        openContextMenu(tree, tree->visualItemRect(file).center(), [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Find in This Folder…"));
            QVERIFY(a);
            a->trigger();
        });
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QDir(m_root).absolutePath());

        QTreeWidgetItem *alpha = childNamed(tree->topLevelItem(0), QStringLiteral("alpha"));
        openContextMenu(tree, tree->visualItemRect(alpha).center(), [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Find in This Folder…"))->trigger();
        });
        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QDir(m_root).absoluteFilePath(QStringLiteral("alpha")));
    }

    // 切換「Show Hidden Files」會重新整理整棵樹；再切回來要復原。
    void wsToggleShowHidden()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint emptyPos(4, tree->viewport()->height() - 4);
        QVERIFY(!tree->itemAt(emptyPos));

        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Show Hidden Files"));
            QVERIFY(a);
            a->setChecked(false);   // 觸發 toggled(false)
        });
        QStringList names = childNames(tree->topLevelItem(0));
        QCOMPARE(names.size(), 5);
        QVERIFY(!names.contains(QStringLiteral(".hidden.txt")));
        QVERIFY(!names.contains(QStringLiteral(".hidden_dir")));

        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Show Hidden Files"));
            QVERIFY(a);
            QVERIFY(!a->isChecked());   // 選單重建時要反映目前狀態
            a->setChecked(true);
        });
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), 7);
    }

    // 「Set Filter…」：輸入 "*.cpp; *.md" 應被切成兩個 trim 過的樣式；按取消則不變。
    void wsTriggerSetFilter()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint emptyPos(4, tree->viewport()->height() - 4);

        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("*.cpp; *.md");
        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Set Filter…"))->trigger();
        });
        QCOMPARE(g_script.inputSeen, 1);
        QStringList names = childNames(tree->topLevelItem(0));
        QCOMPARE(names.size(), 5);   // 3 資料夾 + main.cpp + readme.md
        QVERIFY(names.contains(QStringLiteral("readme.md")));
        QVERIFY(!names.contains(QStringLiteral("notes.txt")));

        // 取消 → 過濾器維持不變
        g_script.acceptInput = false;
        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Set Filter…"))->trigger();
        });
        QCOMPARE(g_script.inputSeen, 2);
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), 5);

        // 清空輸入 → 還原顯示全部
        g_script.acceptInput = true;
        g_script.inputText = QString();
        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Set Filter…"))->trigger();
        });
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), 7);
    }

    // 「Add Folder…」使用者按取消 → 不應新增任何根目錄。
    void wsTriggerAddFolderCancelled()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint emptyPos(4, tree->viewport()->height() - 4);

        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Add Folder…"))->trigger();
        });
        QCOMPARE(g_script.fileSeen, 1);
        QCOMPARE(dock.roots().size(), 1);
    }

    // 「New File…」：成功建檔後樹要重新整理；名稱含不存在的子目錄則跳警告且不建檔。
    void wsTriggerNewFile()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint rootPos = tree->visualItemRect(tree->topLevelItem(0)).center();

        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("created.txt");
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New File…"))->trigger();
        });
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("created.txt"))));
        QVERIFY(childNames(tree->topLevelItem(0)).contains(QStringLiteral("created.txt")));

        // 從「檔案節點」新增：目標目錄取其所在資料夾，重新整理的是父節點
        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("notes.txt"));
        g_script.inputText = QStringLiteral("sibling.txt");
        openContextMenu(tree, tree->visualItemRect(file).center(), [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New File…"))->trigger();
        });
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("sibling.txt"))));
        QVERIFY(childNames(tree->topLevelItem(0)).contains(QStringLiteral("sibling.txt")));

        // 失敗路徑：子目錄不存在 → QFile::open 失敗 → 警告視窗（由 responder 按 Ok）
        const int before = g_script.messageSeen;
        g_script.inputText = QStringLiteral("nodir/bad.txt");
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New File…"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, before + 1);
        QVERIFY(!childNames(tree->topLevelItem(0)).contains(QStringLiteral("bad.txt")));

        // 取消 / 空名稱 → 什麼都不做
        g_script.acceptInput = false;
        const int countBefore = childNames(tree->topLevelItem(0)).size();
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New File…"))->trigger();
        });
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), countBefore);
    }

    // 「New Folder…」：成功建資料夾；名稱不合法時跳警告。
    void wsTriggerNewFolder()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        const QPoint rootPos = tree->visualItemRect(tree->topLevelItem(0)).center();

        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("gamma");
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New Folder…"))->trigger();
        });
        QVERIFY(QFileInfo(QDir(m_root).filePath(QStringLiteral("gamma"))).isDir());
        QTreeWidgetItem *gamma = childNamed(tree->topLevelItem(0), QStringLiteral("gamma"));
        QVERIFY(gamma);
        QCOMPARE(gamma->childCount(), 1);   // 新資料夾也要有 placeholder

        // 已存在同名 → mkdir 失敗 → 警告
        const int before = g_script.messageSeen;
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New Folder…"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, before + 1);

        // 取消
        g_script.acceptInput = false;
        const int count = childNames(tree->topLevelItem(0)).size();
        openContextMenu(tree, rootPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New Folder…"))->trigger();
        });
        QCOMPARE(childNames(tree->topLevelItem(0)).size(), count);
    }

    // 「Rename…」：改名成功要反映到磁碟與樹；同名不動作；目標已存在則失敗跳警告。
    void wsTriggerRename()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("notes.txt"));
        const QPoint pos = tree->visualItemRect(file).center();

        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("renamed.txt");
        openContextMenu(tree, pos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Rename…"))->trigger();
        });
        QVERIFY(!QFile::exists(QDir(m_root).filePath(QStringLiteral("notes.txt"))));
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("renamed.txt"))));
        QVERIFY(childNames(tree->topLevelItem(0)).contains(QStringLiteral("renamed.txt")));

        // 改成已存在的名稱 → QFile::rename 失敗 → 警告
        QTreeWidgetItem *renamed = childNamed(tree->topLevelItem(0), QStringLiteral("renamed.txt"));
        const QPoint pos2 = tree->visualItemRect(renamed).center();
        const int before = g_script.messageSeen;
        g_script.inputText = QStringLiteral("main.cpp");
        openContextMenu(tree, pos2, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Rename…"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, before + 1);
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("renamed.txt"))));

        // 輸入與原名相同 → 直接 return，不動磁碟
        g_script.inputText = QStringLiteral("renamed.txt");
        openContextMenu(tree, pos2, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Rename…"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, before + 1);
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("renamed.txt"))));
    }

    // 「Delete」：按 No 不刪；按 Yes 但檔案已不在磁碟上 → moveToTrash 失敗 → 警告。
    // （刻意不製造成功刪除的情境：那會把測試檔案丟進使用者的垃圾桶。）
    void wsTriggerDelete()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *file = childNamed(tree->topLevelItem(0), QStringLiteral("readme.md"));
        const QPoint pos = tree->visualItemRect(file).center();

        g_script.messageButton = static_cast<int>(QMessageBox::No);
        openContextMenu(tree, pos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Delete"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, 1);
        QVERIFY(QFile::exists(QDir(m_root).filePath(QStringLiteral("readme.md"))));

        // 按 Yes，但路徑已失效 → 失敗警告（question + warning 共兩個對話框）
        QVERIFY(QFile::remove(QDir(m_root).filePath(QStringLiteral("readme.md"))));
        g_script.messageButton = static_cast<int>(QMessageBox::Yes);
        openContextMenu(tree, pos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Delete"))->trigger();
        });
        QCOMPARE(g_script.messageSeen, 3);
    }

    // Reveal in Finder／Open Terminal Here／Open in Default Application 都會外呼系統程式，
    // 測試中不能真的執行。這裡把節點路徑清空後再觸發，驗證三者的「空路徑守衛」確實生效
    // （若守衛失效，這個測試會在 CI 上把 Finder/終端機叫起來，屬於明確的迴歸訊號）。
    void wsGuardsEmptyPaths()
    {
        WorkspaceDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addRoot(m_root);
        QTreeWidgetItem *dirItem = childNamed(tree->topLevelItem(0), QStringLiteral("beta"));
        QVERIFY(dirItem);
        // Open Terminal Here 的 lambda 在「建立選單時」就把目錄複製走，
        // 因此必須在開啟選單之前清空路徑才走得到守衛分支。
        dirItem->setData(0, kWsPathRole, QString());

        openContextMenu(tree, tree->visualItemRect(dirItem).center(), [](QMenu *menu) {
            QAction *reveal = actionNamed(menu, QStringLiteral("Reveal in Finder"));
            QAction *term = actionNamed(menu, QStringLiteral("Open Terminal Here"));
            QVERIFY(reveal);
            QVERIFY(term);
            reveal->trigger();
            term->trigger();
        });

        // 「以預設應用程式開啟」只對檔案出現；同樣先清空路徑再觸發
        QTreeWidgetItem *fileItem = childNamed(tree->topLevelItem(0), QStringLiteral("main.cpp"));
        openContextMenu(tree, tree->visualItemRect(fileItem).center(), [fileItem](QMenu *menu) {
            QAction *a = actionNamed(menu, QStringLiteral("Open in Default Application"));
            QVERIFY(a);
            fileItem->setData(0, kWsPathRole, QString());
            a->trigger();
        });
        QCOMPARE(fileItem->data(0, kWsPathRole).toString(), QString());
    }

    // ==================== ProjectPanelDock ====================

    // addProject：拒絕空名稱與重複名稱，且不得超過 kMaxProjects。
    void ppAddProjectLimits()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        QVERIFY(tree);

        QVERIFY(!dock.addProject(QString()));
        QVERIFY(dock.addProject(QStringLiteral("P1")));
        QVERIFY(!dock.addProject(QStringLiteral("P1")));   // 重複
        for (int i = 2; i <= ProjectStore::kMaxProjects; ++i)
            QVERIFY(dock.addProject(QStringLiteral("P%1").arg(i)));
        QCOMPARE(tree->topLevelItemCount(), ProjectStore::kMaxProjects);
        QVERIFY(!dock.addProject(QStringLiteral("overflow")));   // 超過上限
        QCOMPARE(tree->topLevelItemCount(), ProjectStore::kMaxProjects);
    }

    // removeProject：名稱需完全相符；不存在則無動作。
    void ppRemoveProject()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.addProject(QStringLiteral("Alpha"));
        dock.addProject(QStringLiteral("Beta"));

        dock.removeProject(QStringLiteral("alpha"));   // 大小寫不同 → 不移除
        QCOMPARE(tree->topLevelItemCount(), 2);
        dock.removeProject(QStringLiteral("Alpha"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Beta"));
    }

    // activeProjectName：無 project 回空字串；有選取時回選取節點所屬 project；否則回第一個。
    void ppActiveProjectName()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        QCOMPARE(dock.activeProjectName(), QString());

        dock.addProject(QStringLiteral("First"));
        dock.addProject(QStringLiteral("Second"));
        QCOMPARE(dock.activeProjectName(), QStringLiteral("First"));

        tree->setCurrentItem(tree->topLevelItem(1));
        QCOMPARE(dock.activeProjectName(), QStringLiteral("Second"));
    }

    // load() 從 projects.json 重建樹（含巢狀資料夾、有/無名稱的檔案節點），
    // save() 再寫回，內容需完整 round-trip。
    void ppLoadSaveRoundTrip()
    {
        const QString fileA = QDir(m_root).absoluteFilePath(QStringLiteral("main.cpp"));
        const QString fileB = QDir(m_root).absoluteFilePath(QStringLiteral("alpha/a1.cpp"));

        ProjectWorkspace ws;
        Project p;
        p.name = QStringLiteral("Proj");

        ProjectNode folder;
        folder.type = ProjectNodeType::Folder;
        folder.name = QStringLiteral("sources");
        folder.path = QDir(m_root).absoluteFilePath(QStringLiteral("alpha"));

        ProjectNode nestedFile;
        nestedFile.type = ProjectNodeType::File;
        nestedFile.name = QStringLiteral("a1.cpp");
        nestedFile.path = fileB;
        folder.children.append(nestedFile);

        ProjectNode topFile;   // 沒有 name → 顯示名稱由路徑推導
        topFile.type = ProjectNodeType::File;
        topFile.path = fileA;

        ProjectNode orphan;    // 沒有 path → 退回以 name 當路徑（舊版 projects.json 相容）
        orphan.type = ProjectNodeType::File;
        orphan.name = QStringLiteral("orphan.txt");

        ProjectNode blank;     // name/path 皆空 → 節點仍建立，但不得出現在檔案清單裡
        blank.type = ProjectNodeType::File;

        p.roots << folder << topFile << orphan << blank;
        ws.projects.append(p);
        QVERIFY(ProjectStore::save(ws));

        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.load();
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem *proj = tree->topLevelItem(0);
        QCOMPARE(proj->text(0), QStringLiteral("Proj"));
        QVERIFY(proj->isExpanded());
        QCOMPARE(childNames(proj),
                 (QStringList{QStringLiteral("sources"), QStringLiteral("main.cpp"),
                              QStringLiteral("orphan.txt"), QString()}));
        QTreeWidgetItem *sources = childNamed(proj, QStringLiteral("sources"));
        QVERIFY(sources);
        QCOMPARE(childNames(sources), QStringList{QStringLiteral("a1.cpp")});

        // 作用中 project 要能從「深層節點」往上追到所屬 project
        tree->setCurrentItem(childNamed(sources, QStringLiteral("a1.cpp")));
        QCOMPARE(dock.activeProjectName(), QStringLiteral("Proj"));

        // 檔案清單：巢狀資料夾要遞迴展開；無 path 的 orphan 退回用 name 當路徑；
        // name/path 全空的節點則被略過（不得產生空字串路徑污染搜尋範圍）。
        const QStringList wantFiles{fileB, fileA, QStringLiteral("orphan.txt")};
        QCOMPARE(dock.filePathsForProject(), wantFiles);
        QCOMPARE(dock.allFilePaths(), wantFiles);
        QCOMPARE(dock.filePathsForProject(QStringLiteral("Proj")), wantFiles);
        QVERIFY(dock.filePathsForProject(QStringLiteral("NoSuchProject")).isEmpty());

        // 寫回並重新載入 → 結構一致
        QVERIFY(dock.save());
        const ProjectWorkspace out = ProjectStore::load();
        QCOMPARE(out.projects.size(), 1);
        QCOMPARE(out.projects[0].roots.size(), 4);
        QCOMPARE(out.projects[0].roots[0].type, ProjectNodeType::Folder);
        QCOMPARE(out.projects[0].roots[0].name, QStringLiteral("sources"));
        QCOMPARE(out.projects[0].roots[0].children.size(), 1);
        QCOMPARE(out.projects[0].roots[0].children[0].path, fileB);
        QCOMPARE(out.projects[0].roots[1].type, ProjectNodeType::File);
        QCOMPARE(out.projects[0].roots[1].path, fileA);

        // load() 應先清空舊內容
        dock.load();
        QCOMPARE(tree->topLevelItemCount(), 1);
    }

    // 雙擊：只有 File 節點 emit openFileRequested。
    void ppDoubleClickEmitsForFilesOnly()
    {
        ProjectWorkspace ws;
        Project p;
        p.name = QStringLiteral("Proj");
        ProjectNode folder;
        folder.type = ProjectNodeType::Folder;
        folder.name = QStringLiteral("grp");
        ProjectNode f;
        f.type = ProjectNodeType::File;
        f.name = QStringLiteral("main.cpp");
        f.path = QDir(m_root).absoluteFilePath(QStringLiteral("main.cpp"));
        folder.children.append(f);
        p.roots << folder;
        ws.projects.append(p);
        QVERIFY(ProjectStore::save(ws));

        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.load();
        QSignalSpy spy(&dock, &ProjectPanelDock::openFileRequested);

        QTreeWidgetItem *proj = tree->topLevelItem(0);
        QTreeWidgetItem *grp = childNamed(proj, QStringLiteral("grp"));
        QTreeWidgetItem *fileItem = childNamed(grp, QStringLiteral("main.cpp"));

        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, fileItem), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), f.path);

        // 資料夾、project、空白處都不 emit
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, grp), Q_ARG(int, 0)));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, proj), Q_ARG(int, 0)));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked",
                                          Q_ARG(QTreeWidgetItem *, nullptr), Q_ARG(int, 0)));
        QCOMPARE(spy.size(), 1);
    }

    // 空白處右鍵只有「New Project…」，且達上限時停用。
    void ppContextMenuOnEmptyArea()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        const QPoint emptyPos(4, tree->viewport()->height() - 4);
        QVERIFY(!tree->itemAt(emptyPos));

        QVector<MenuEntry> entries = openContextMenu(tree, emptyPos);
        QCOMPARE(entryTexts(entries), QStringList{QStringLiteral("New Project…")});
        QVERIFY(entries.first().enabled);

        for (int i = 0; i < ProjectStore::kMaxProjects; ++i)
            dock.addProject(QStringLiteral("P%1").arg(i));
        entries = openContextMenu(tree, emptyPos);
        QVERIFY(!entryNamed(entries, QStringLiteral("New Project…"))->enabled);
    }

    // Project／Folder 節點可新增子節點；File 節點只能改名或移除。
    void ppContextMenuPerNodeKind()
    {
        ProjectWorkspace ws;
        Project p;
        p.name = QStringLiteral("Proj");
        ProjectNode folder;
        folder.type = ProjectNodeType::Folder;
        folder.name = QStringLiteral("grp");
        ProjectNode f;
        f.type = ProjectNodeType::File;
        f.name = QStringLiteral("main.cpp");
        f.path = QDir(m_root).absoluteFilePath(QStringLiteral("main.cpp"));
        folder.children.append(f);
        p.roots << folder;
        ws.projects.append(p);
        QVERIFY(ProjectStore::save(ws));

        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        dock.load();
        QTreeWidgetItem *proj = tree->topLevelItem(0);
        QTreeWidgetItem *grp = childNamed(proj, QStringLiteral("grp"));
        grp->setExpanded(true);
        QTreeWidgetItem *fileItem = childNamed(grp, QStringLiteral("main.cpp"));

        QStringList texts = entryTexts(openContextMenu(tree, tree->visualItemRect(proj).center()));
        QVERIFY(texts.contains(QStringLiteral("Add Folder (Virtual)…")));
        QVERIFY(texts.contains(QStringLiteral("Add Folder from Disk…")));
        QVERIFY(texts.contains(QStringLiteral("Add Files…")));
        QVERIFY(texts.contains(QStringLiteral("Rename…")));
        QVERIFY(texts.contains(QStringLiteral("Remove Project")));   // project 用不同文案

        texts = entryTexts(openContextMenu(tree, tree->visualItemRect(grp).center()));
        QVERIFY(texts.contains(QStringLiteral("Add Files…")));
        QVERIFY(texts.contains(QStringLiteral("Remove")));
        QVERIFY(!texts.contains(QStringLiteral("Remove Project")));

        texts = entryTexts(openContextMenu(tree, tree->visualItemRect(fileItem).center()));
        QVERIFY(!texts.contains(QStringLiteral("Add Files…")));
        QVERIFY(!texts.contains(QStringLiteral("Add Folder (Virtual)…")));
        QVERIFY(texts.contains(QStringLiteral("Rename…")));
        QVERIFY(texts.contains(QStringLiteral("Remove")));
    }

    // 從空白處右鍵新增 project；取消時不新增。
    void ppTriggerNewProject()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        const QPoint emptyPos(4, tree->viewport()->height() - 4);

        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("MyProject");
        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New Project…"))->trigger();
        });
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("MyProject"));

        g_script.acceptInput = false;
        openContextMenu(tree, emptyPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("New Project…"))->trigger();
        });
        QCOMPARE(tree->topLevelItemCount(), 1);
    }

    // 虛擬資料夾新增／磁碟來源取消／改名／移除，全部走右鍵選單。
    void ppTriggerNodeActions()
    {
        ProjectPanelDock dock;
        QTreeWidget *tree = showAndTree(&dock);
        QVERIFY(dock.addProject(QStringLiteral("Proj")));
        QTreeWidgetItem *proj = tree->topLevelItem(0);
        const QPoint projPos = tree->visualItemRect(proj).center();

        // 新增虛擬資料夾
        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("virtual");
        openContextMenu(tree, projPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Add Folder (Virtual)…"))->trigger();
        });
        QCOMPARE(childNames(proj), QStringList{QStringLiteral("virtual")});

        // 取消新增 → 不增加節點
        g_script.acceptInput = false;
        openContextMenu(tree, projPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Add Folder (Virtual)…"))->trigger();
        });
        QCOMPARE(proj->childCount(), 1);

        // 磁碟資料夾／檔案：使用者取消檔案對話框 → 不增加節點
        openContextMenu(tree, projPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Add Folder from Disk…"))->trigger();
        });
        openContextMenu(tree, projPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Add Files…"))->trigger();
        });
        QCOMPARE(g_script.fileSeen, 2);
        QCOMPARE(proj->childCount(), 1);

        // 改名子節點
        QTreeWidgetItem *virt = proj->child(0);
        proj->setExpanded(true);
        g_script.acceptInput = true;
        g_script.inputText = QStringLiteral("renamed-group");
        openContextMenu(tree, tree->visualItemRect(virt).center(), [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Rename…"))->trigger();
        });
        QCOMPARE(proj->child(0)->text(0), QStringLiteral("renamed-group"));

        // 取消改名 → 名稱不變
        g_script.acceptInput = false;
        openContextMenu(tree, tree->visualItemRect(virt).center(), [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Rename…"))->trigger();
        });
        QCOMPARE(proj->child(0)->text(0), QStringLiteral("renamed-group"));

        // 移除子節點（非 project → 從父節點拔除）
        openContextMenu(tree, tree->visualItemRect(proj->child(0)).center(), [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Remove"))->trigger();
        });
        QCOMPARE(proj->childCount(), 0);

        // 移除整個 project
        openContextMenu(tree, projPos, [](QMenu *menu) {
            actionNamed(menu, QStringLiteral("Remove Project"))->trigger();
        });
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

private:
    int m_caseCounter = 0;
};

QTEST_MAIN(TestWorkspaceDocks)
#include "test_workspacedocks.moc"
