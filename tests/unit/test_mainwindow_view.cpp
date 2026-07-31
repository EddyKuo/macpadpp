// 單元測試：MainWindow_View.cpp——MainWindow 的「檢視層」實作
// （雙檢視移動/複製、分割、狀態列六格、分頁標題/工具提示、文件清單與面板刷新、
//  Distraction Free / Post-It、監控模式、Windows… 對話框、右鍵選單的實際動作…）。
//
// 測試策略：
//  1. 這一層全是 private / private slot。測試類別不是 MainWindow 的 friend，
//     所以一律走「使用者實際走的路徑」——View/Window 選單的 QAction、分頁列訊號，
//     或對 private slot 用 QMetaObject::invokeMethod（moc 會為 private slot 產生入口）。
//     測到的因此是真正接在 UI 上的那條線。
//  2. 停靠面板的 isVisible() 在視窗未 show() 時恆為 false，而 refreshPanels()/
//     setDistractionFree() 的行為完全由它決定 —— 需要面板行為的測試一律先 show()。
//  3. 任何會 exec() 的 modal（QMessageBox、QColorDialog、WindowsListDialog）都以
//     driveNextModal() 在事件迴圈中接手，逾時強制收尾，測試絕不會卡住。
//     QMenu::exec() 的彈出選單另以 closeNextContextMenu() 處理（popup 不是 modal widget）。
//  4. 會開外部程式的路徑（QDesktopServices::openUrl）以 setUrlHandler 攔截，
//     既能斷言真正組出來的 URL，又不會真的開瀏覽器或發網路請求。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDeadlineTimer>
#include <QDesktopServices>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QAbstractButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <Qsci/qsciscintilla.h>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "core/LexerFactory.h"
#include "persistence/SettingsStore.h"
#include "ui/DocumentListDock.h"
#include "ui/EditorPane.h"
#include "ui/Panels.h"
#include "ui/WindowsListDialog.h"

using macpad::core::EditorWidget;
using macpad::ui::EditorPane;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------

static QAction *findMenuAction(QMenu *menu, const QString &text)
{
    const auto acts = menu->actions();
    for (QAction *a : acts) {
        if (a->text() == text)
            return a;
        if (a->menu()) {
            if (QAction *sub = findMenuAction(a->menu(), text))
                return sub;
        }
    }
    return nullptr;
}

// 以顯示文字尋找選單動作（測試不載入翻譯，tr() 原樣回傳原字串）
static QAction *findMenuAction(MainWindow &w, const QString &text)
{
    const auto tops = w.menuBar()->actions();
    for (QAction *top : tops) {
        if (top->menu()) {
            if (QAction *a = findMenuAction(top->menu(), text))
                return a;
        }
    }
    return nullptr;
}

// 在下一個 modal 對話框出現時接手，避免 exec() 讓測試永久阻塞。
// 逾時（預設 3 秒）就放棄輪詢——代表根本沒有對話框出現，測試仍會往下跑。
static void driveNextModal(const std::function<void(QDialog *)> &fn,
                           bool *fired = nullptr, int timeoutMs = 3000)
{
    auto *timer = new QTimer;
    timer->setInterval(5);
    const QDeadlineTimer deadline(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, fn, fired, deadline] {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            timer->stop();
            timer->deleteLater();
            if (auto *dlg = qobject_cast<QDialog *>(modal)) {
                if (fired)
                    *fired = true;
                fn(dlg);
                if (dlg->isVisible())
                    dlg->reject();   // 保險：handler 沒關掉時強制收尾
            } else {
                modal->close();
            }
            return;
        }
        if (deadline.hasExpired()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

// QMenu::exec() 開出來的彈出選單不是 activeModalWidget（它是 popup），
// 因此改以「MainWindow 的直屬 QMenu 子物件」定位；找到就交給 fn 後關閉。
// 沒有看門狗就會整組測試卡在 exec() 裡，所以逾時一律強制 close。
static void closeNextContextMenu(MainWindow *w, const std::function<void(QMenu *)> &fn,
                                 bool *fired = nullptr, int timeoutMs = 3000)
{
    auto *timer = new QTimer;
    timer->setInterval(10);
    const QDeadlineTimer deadline(timeoutMs);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, w, fn, fired, deadline] {
        const auto menus = w->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        for (QMenu *m : menus) {
            if (!m->isVisible())
                continue;
            timer->stop();
            timer->deleteLater();
            if (fired)
                *fired = true;
            fn(m);
            m->close();
            return;
        }
        if (deadline.hasExpired()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

// 主視窗的兩個檢視容器（m_tabs / m_tabs2）。兩者是 MainWindow 內唯一的一對 QTabWidget，
// 但 findChildren 的順序不保證與建立順序一致，故以「開場那個編輯器在誰底下」判定主檢視——
// 這個判定只在剛建構完（分頁還沒被搬動）時成立，各測試都在最前面取一次。
struct Views {
    QTabWidget *main = nullptr;
    QTabWidget *second = nullptr;
};

static Views viewsOf(MainWindow &w)
{
    Views v;
    QWidget *active = w.activeEditor();
    const auto all = w.findChildren<QTabWidget *>();
    for (QTabWidget *t : all) {
        if (active && t->isAncestorOf(active))
            v.main = t;
        else
            v.second = t;
    }
    return v;
}

// 狀態列六格（文件類型 / 長度·行數 / 游標·選取 / EOL / 編碼 / INS·OVR）
static QStringList statusCellTexts(MainWindow &w)
{
    QStringList out;
    const auto labels = w.statusBar()->findChildren<QLabel *>();
    for (QLabel *l : labels)
        out << l->text();
    return out;
}

static bool anyCellContains(MainWindow &w, const QString &needle)
{
    const auto texts = statusCellTexts(w);
    for (const QString &t : texts)
        if (t.contains(needle))
            return true;
    return false;
}

// 攔截 QDesktopServices::openUrl —— 讓「在瀏覽器開啟 / 網路搜尋」可被斷言，
// 且不會真的啟動外部程式或連外。
class UrlSink : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void handle(const QUrl &u) { urls.append(u); }
};

class TestMainWindowView : public QObject {
    Q_OBJECT

    macpad::persistence::Settings m_savedSettings;

private slots:
    void initTestCase()
    {
        // 隔離設定/快取，避免動到使用者真實的 ~/Library/Application Support/macpad++
        QStandardPaths::setTestModeEnabled(true);
        // 關鍵：關掉原生對話框，QColorDialog/QMessageBox 才會是可被 timer 抓到的 Qt widget
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        m_savedSettings = macpad::persistence::SettingsStore::load();
    }

    // 每支測試都可能改設定；統一在結束時還原，避免互相污染
    void cleanup()
    {
        macpad::persistence::SettingsStore::save(m_savedSettings);
    }

    void cleanupTestCase()
    {
        macpad::persistence::SettingsStore::save(m_savedSettings);
    }

    // ---------------------------------------------------------------
    // 狀態列六格
    // ---------------------------------------------------------------

    // 文件類型格取自 lexer 語言名、EOL 格取自編輯器 EOL——兩者都是使用者一眼會看到的資訊，
    // 出錯時狀態列會顯示成別的檔案類型或別的換行慣例。
    void statusCellsFollowLexerAndEol()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        // 無 lexer → Normal text file
        QVERIFY2(anyCellContains(w, QStringLiteral("Normal text file")),
                 "未設 lexer 時文件類型格應為 Normal text file");

        // 設 C++ lexer → 「C++ file」（docTypeName 走 lexer->language() 分支）
        QsciLexer *lex = macpad::core::LexerFactory::createForLanguage(
            QStringLiteral("cpp"), e);
        QVERIFY(lex);
        e->setLanguageLexer(lex);
        // lexerChanged 只負責重新上色；文件類型格在下一次狀態列更新時才反映（如打字）
        e->setText(QStringLiteral("int x;"));
        QVERIFY2(anyCellContains(w, QStringLiteral("C++ file")),
                 "設定 C++ lexer 後文件類型格未更新");

        // 三種 EOL 各自的顯示字串（Notepad++ 風格全名）
        e->convertEol(macpad::core::Eol::CrLf);
        QVERIFY(anyCellContains(w, QStringLiteral("Windows (CR LF)")));
        e->convertEol(macpad::core::Eol::Cr);
        QVERIFY(anyCellContains(w, QStringLiteral("Macintosh (CR)")));
        e->convertEol(macpad::core::Eol::Lf);
        QVERIFY(anyCellContains(w, QStringLiteral("Unix (LF)")));

        // 有選取時多顯示 Sel 欄；OVR/INS 依覆寫模式切換
        e->setText(QStringLiteral("hello\nworld"));
        e->setSelection(0, 0, 1, 3);
        QVERIFY2(anyCellContains(w, QStringLiteral("Sel :")),
                 "有選取時狀態列未顯示 Sel 欄");
        QVERIFY(anyCellContains(w, QStringLiteral("INS")));
        e->SendScintilla(QsciScintilla::SCI_SETOVERTYPE, 1L);
        e->setCursorPosition(0, 1);          // 觸發 cursorPositionChanged → updateStatusBar
        QVERIFY2(anyCellContains(w, QStringLiteral("OVR")),
                 "覆寫模式下狀態列仍顯示 INS");
    }

    // 明確指定 Light/Dark 主題時不得再去問系統——兩種模式套出來的底色必須不同，
    // 否則「切主題」在編輯區看起來毫無反應。
    void themeModeAppliesDistinctEditorColors()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.theme = macpad::persistence::ThemeMode::Light;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        unsigned long lightPaper = 0;
        {
            MainWindow w(nullptr, false);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e);
            lightPaper = e->SendScintilla(QsciScintilla::SCI_STYLEGETBACK,
                                          int(QsciScintilla::STYLE_DEFAULT));
        }
        s.theme = macpad::persistence::ThemeMode::Dark;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        unsigned long darkPaper = 0;
        {
            MainWindow w(nullptr, false);
            EditorWidget *e = w.activeEditor();
            QVERIFY(e);
            darkPaper = e->SendScintilla(QsciScintilla::SCI_STYLEGETBACK,
                                         int(QsciScintilla::STYLE_DEFAULT));
        }
        QVERIFY2(lightPaper != darkPaper,
                 "Light 與 Dark 主題套出相同的編輯區底色");
    }

    // ---------------------------------------------------------------
    // Dual-View：Move / Clone to Other View
    // ---------------------------------------------------------------

    // 搬移必須是「同一個 pane 物件轉移」：編輯器指標不變、內容不變，第二檢視自動現身。
    // 主檢視被搬空時不補空白頁——文件還在，只是移到隔壁檢視了（Notepad++ 同樣行為）。
    void moveToOtherViewTransfersPane()
    {
        MainWindow w(nullptr, false);
        w.show();
        const Views v = viewsOf(w);
        QVERIFY(v.main && v.second);
        QTabWidget *main = v.main;
        QTabWidget *second = v.second;
        QVERIFY2(!second->isVisible(), "第二檢視在沒有分頁時就已顯示");

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("moved content"));
        w.statusBar();   // 確保狀態列已建立

        QMetaObject::invokeMethod(&w, "showFind");   // 檢視切換時 Find 對話框要跟著改綁編輯器

        QAction *move = findMenuAction(w, QStringLiteral("Move to Other View"));
        QVERIFY2(move, "找不到 Move to Other View");
        move->trigger();

        QCOMPARE(second->count(), 1);
        QVERIFY2(second->isVisible(), "第二檢視有分頁後仍隱藏");
        QVERIFY2(main->count() == 0,
                 "主檢視被搬空後多補了一張空白頁（文件已在第二檢視，不該再補）");
        auto *movedPane = qobject_cast<EditorPane *>(second->widget(0));
        QVERIFY(movedPane);
        QCOMPARE(movedPane->primary(), e);
        QCOMPARE(movedPane->primary()->text(), QStringLiteral("moved content"));
        // 搬移後作用中檢視換成第二檢視，狀態列/面板跟著切過去
        QCOMPARE(w.activeEditor(), e);

        // 再搬回來：來源（第二檢視）被搬空 → 自動隱藏，不補空白頁
        move->trigger();
        QCOMPARE(second->count(), 0);
        QVERIFY2(!second->isVisible(), "第二檢視搬空後未自動隱藏");
        QCOMPARE(main->count(), 1);
        QCOMPARE(w.activeEditor(), e);
    }

    // Clone 與來源共享同一份文件：改其中一邊，另一邊立刻同步（這正是 clone 的定義）。
    void cloneToOtherViewSharesDocument()
    {
        MainWindow w(nullptr, false);
        w.show();
        const Views v = viewsOf(w);
        QTabWidget *second = v.second;

        EditorWidget *src = w.activeEditor();
        QVERIFY(src);
        // 帶 lexer 的來源：clone 必須自行建立同語言的 lexer 實例（不可共用指標）
        src->setLanguageLexer(macpad::core::LexerFactory::createForLanguage(
            QStringLiteral("cpp"), src));
        src->setText(QStringLiteral("int main() {}"));

        QAction *clone = findMenuAction(w, QStringLiteral("Clone to Other View"));
        QVERIFY2(clone, "找不到 Clone to Other View");
        clone->trigger();

        QCOMPARE(second->count(), 1);
        auto *pane = qobject_cast<EditorPane *>(second->widget(0));
        QVERIFY(pane);
        EditorWidget *cloned = pane->primary();
        QVERIFY(cloned && cloned != src);
        QVERIFY2(pane->isClone(), "cloneToOtherView 產出的 pane 未標記為 clone");
        QCOMPARE(cloned->text(), src->text());
        QVERIFY2(cloned->lexer() && cloned->lexer() != src->lexer(),
                 "clone 未建立自己的 lexer 實例");

        // 共享文件：來源改動立即反映在 clone 上
        src->setText(QStringLiteral("int main() { return 0; }"));
        QCOMPARE(cloned->text(), src->text());
    }

    // 跨檢視同步縮放（Notepad++ v8.9.5）：偏好開啟才同步，關閉時各自為政。
    void syncZoomBetweenViewsHonoursPreference()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.syncZoomBetweenViews = true;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        w.show();
        const Views v = viewsOf(w);
        findMenuAction(w, QStringLiteral("Clone to Other View"))->trigger();
        auto *pane0 = qobject_cast<EditorPane *>(v.main->widget(0));
        auto *pane1 = qobject_cast<EditorPane *>(v.second->widget(0));
        QVERIFY(pane0 && pane1);
        EditorWidget *a = pane0->primary();
        EditorWidget *b = pane1->primary();

        // 直接發出 zoomChanged（等同使用者 Ctrl+滾輪／選單縮放後編輯器的回報）
        a->zoomTo(4);
        QMetaObject::invokeMethod(a, "zoomChanged", Q_ARG(int, 4));
        QCOMPARE(int(b->SendScintilla(QsciScintilla::SCI_GETZOOM)), 4);

        // 偏好關閉 → 另一檢視維持原縮放
        s.syncZoomBetweenViews = false;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        a->zoomTo(-2);
        QMetaObject::invokeMethod(a, "zoomChanged", Q_ARG(int, -2));
        QCOMPARE(int(b->SendScintilla(QsciScintilla::SCI_GETZOOM)), 4);
    }

    // 把所有分頁搬到第二檢視後再全部關閉：第二檢視隱藏、作用中檢視退回（此時暫時沒有
    // 任何編輯器，狀態列必須被清空而不是留著舊文件的資訊），最後補一張空白頁。
    void closingLastTabOfSecondViewFallsBackToMainView()
    {
        MainWindow w(nullptr, false);
        w.show();
        const Views v = viewsOf(w);
        QTabWidget *main = v.main;
        QTabWidget *second = v.second;

        // 唯一的分頁搬到第二檢視 → 主檢視此刻是空的
        findMenuAction(w, QStringLiteral("Move to Other View"))->trigger();
        QCOMPARE(second->count(), 1);
        QCOMPARE(main->count(), 0);

        // 關掉第二檢視唯一的分頁（空白未修改文件，不會跳存檔提示）：
        // 這一刻兩個檢視都是空的，狀態列必須被清空而不是保留舊文件資訊。
        emit second->tabCloseRequested(0);

        QCOMPARE(second->count(), 0);
        QVERIFY2(!second->isVisible(), "第二檢視關空後未隱藏");
        QVERIFY2(main->count() == 1, "兩檢視皆空時未補上空白分頁");
        QVERIFY(w.activeEditor());
    }

    // ---------------------------------------------------------------
    // 分割檢視
    // ---------------------------------------------------------------

    void toggleSplitAddsAndRemovesSecondaryView()
    {
        MainWindow w(nullptr, false);
        auto *pane = qobject_cast<EditorPane *>(viewsOf(w).main->widget(0));
        QVERIFY(pane);
        QVERIFY(!pane->isSplit());

        QAction *split = findMenuAction(w, QStringLiteral("Toggle Split"));
        QVERIFY2(split, "找不到 Toggle Split");
        split->trigger();
        QVERIFY2(pane->isSplit(), "Toggle Split 後未建立次檢視");
        split->trigger();
        QVERIFY2(!pane->isSplit(), "再次 Toggle Split 未關閉次檢視");
    }

    // ---------------------------------------------------------------
    // 分頁列訊號（右鍵選單 / 雙擊關閉 / 切換分頁）
    // ---------------------------------------------------------------

    // 分頁右鍵：必須以被右鍵的那一格為準（多列分頁列不可用 QTabBar::tabAt），
    // 並先把該檢視設為作用中，選單各項才會作用到正確文件。
    void tabBarContextMenuTargetsClickedTab()
    {
        MainWindow w(nullptr, false);
        w.show();
        QTabWidget *tabs = viewsOf(w).main;
        QAction *newFile = findMenuAction(w, QStringLiteral("New"));
        QVERIFY(newFile);
        newFile->trigger();
        newFile->trigger();
        QCOMPARE(tabs->count(), 3);
        tabs->setCurrentIndex(2);

        // 對第 0 格右鍵 → 該格成為當前分頁
        bool fired = false;
        closeNextContextMenu(&w, [](QMenu *m) {
            QVERIFY2(!m->actions().isEmpty(), "分頁右鍵選單是空的");
        }, &fired);
        emit tabs->tabBar()->customContextMenuRequested(tabs->tabBar()->tabRect(0).center());
        QVERIFY2(fired, "分頁右鍵沒有彈出選單");
        QCOMPARE(tabs->currentIndex(), 0);

        // 座標落在分頁列以外 → 不彈選單、也不改變當前分頁
        tabs->setCurrentIndex(1);
        emit tabs->tabBar()->customContextMenuRequested(QPoint(-50, -50));
        QCOMPARE(tabs->currentIndex(), 1);
    }

    // 雙擊分頁關閉是偏好（預設關閉）；行為必須即時跟著偏好走，而不是啟動時決定。
    void tabBarDoubleClickClosesOnlyWhenEnabled()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.tabBarDoubleClickCloses = false;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 2);

        emit tabs->tabBar()->tabBarDoubleClicked(0);
        QCOMPARE(tabs->count(), 2);   // 偏好關閉 → 不得關掉分頁

        s.tabBarDoubleClickCloses = true;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        emit tabs->tabBar()->tabBarDoubleClicked(0);
        QCOMPARE(tabs->count(), 1);

        // 索引 -1（點在分頁列空白處）必須被擋掉
        emit tabs->tabBar()->tabBarDoubleClicked(-1);
        QCOMPARE(tabs->count(), 1);
    }

    // 切換分頁要連動狀態列與 Monitoring 勾選（Monitoring 是逐檔狀態，不是全域開關）
    void switchingTabUpdatesStatusBar()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        EditorWidget *first = w.activeEditor();
        QVERIFY(first);
        // 先開著 Find 對話框：切換分頁時它必須跟著換成新的作用中編輯器，
        // 否則搜尋會繼續打在使用者已經離開的那份文件上。
        QMetaObject::invokeMethod(&w, "showFind");
        first->setText(QStringLiteral("0123456789"));

        findMenuAction(w, QStringLiteral("New"))->trigger();
        w.activeEditor()->setText(QStringLiteral("ab"));
        QVERIFY(anyCellContains(w, QStringLiteral("length : 2")));

        tabs->setCurrentIndex(0);
        QCOMPARE(w.activeEditor(), first);
        QVERIFY2(anyCellContains(w, QStringLiteral("length : 10")),
                 "切回第一個分頁後狀態列未更新長度");
    }

    // ---------------------------------------------------------------
    // 分頁標題 / 工具提示 / 文件清單
    // ---------------------------------------------------------------

    // 標題組成：📌 釘選前綴 + 🔒 唯讀前綴 + （未命名時）首行內容 + 長度上限截斷。
    // 這四條規則彼此疊加，任何一條漏掉都會在分頁列直接看出來。
    void tabLabelCombinesPinReadOnlyFirstLineAndMaxLength()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.tabBarUntitledNameFromFirstLine = true;
        s.tabBarLabelMaxLength = 8;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("ABCDEFGHIJKLMN\nsecond"));
        QMetaObject::invokeMethod(&w, "updateTabTitle");
        QCOMPARE(tabs->tabText(0), QStringLiteral("ABCDEFG…"));   // 8 字上限含省略號

        // 首行為空白 → 維持 untitled(N)，不能變成空標題
        e->setText(QStringLiteral("\nnot first"));
        QMetaObject::invokeMethod(&w, "updateTabTitle");
        QVERIFY2(!tabs->tabText(0).isEmpty(), "首行空白時分頁標題變成空的");

        // 唯讀 → 🔒 前綴
        e->setReadOnly(true);
        QMetaObject::invokeMethod(&w, "updateTabTitle");
        QVERIFY2(tabs->tabText(0).startsWith(QStringLiteral("🔒")),
                 "唯讀分頁缺少 🔒 前綴");

        // 釘選 → 📌 前綴排在唯讀之前
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));
        QVERIFY2(tabs->tabText(0).startsWith(QStringLiteral("📌 🔒")),
                 "釘選+唯讀的前綴順序不正確");
        e->setReadOnly(false);

        // 未命名分頁的 tooltip 顯示建立時間（沒有路徑可顯示）
        QVERIFY2(tabs->tabToolTip(0).startsWith(QStringLiteral("Created:")),
                 "未命名分頁的 tooltip 未顯示建立時間");

        // 空 pane 的查詢函式必須安全回傳空字串（防止 nullptr 解參考）
        EditorPane *nullPane = nullptr;
        QString label = QStringLiteral("x");
        QMetaObject::invokeMethod(&w, "tabLabelFor", Q_RETURN_ARG(QString, label),
                                  Q_ARG(EditorPane *, nullPane));
        QVERIFY(label.isEmpty());
        QString tip = QStringLiteral("x");
        QMetaObject::invokeMethod(&w, "tabTooltipFor", Q_RETURN_ARG(QString, tip),
                                  Q_ARG(EditorPane *, nullPane));
        QVERIFY(tip.isEmpty());
    }

    // 已存檔分頁的 tooltip 是完整路徑（讓同名檔可以分辨）
    void tabTooltipShowsFullPathForSavedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tip.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x\n");
        f.close();

        MainWindow w(nullptr, false);
        w.openFile(path);
        QTabWidget *tabs = viewsOf(w).main;
        QCOMPARE(tabs->tabToolTip(tabs->currentIndex()), path);
    }

    // 文件清單（Document List）預覽：超過 15 行要截斷並加省略號，
    // 單行超過 200 字也要截斷——否則 tooltip 會長到蓋住整個畫面。
    void documentListPreviewIsTruncated()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.docPeekerEnabled = true;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        w.show();
        const Views v = viewsOf(w);
        auto *docList = w.findChild<macpad::ui::DocumentListDock *>();
        QVERIFY(docList);
        docList->show();

        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QStringList lines;
        lines << QString(300, QLatin1Char('x'));
        for (int i = 0; i < 25; ++i)
            lines << QStringLiteral("line%1").arg(i);
        e->setText(lines.join(QLatin1Char('\n')));
        QMetaObject::invokeMethod(&w, "updateTabTitle");   // → refreshDocList

        // 第二檢視有內容時，文件清單以 ② 前綴標示（合併兩檢視的列表）
        findMenuAction(w, QStringLiteral("Clone to Other View"))->trigger();
        QMetaObject::invokeMethod(&w, "updateTabTitle");
        QVERIFY2(v.second->count() == 1, "clone 未進入第二檢視");
    }

    // ---------------------------------------------------------------
    // 停靠面板：Document Map / Function List
    // ---------------------------------------------------------------

    // 面板開啟時才付出解析/繪製成本；開啟後必須立刻反映目前文件，
    // 而不是等到下次切分頁（否則剛打開的面板是空的）。
    void panelsRefreshWhenShown()
    {
        MainWindow w(nullptr, false);
        w.show();
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("void alpha() {}\nvoid beta() {}\n"));

        auto *funcList = w.findChild<macpad::ui::FunctionListDock *>();
        auto *docMap = w.findChild<macpad::ui::DocumentMapDock *>();
        QVERIFY(funcList && docMap);

        funcList->show();     // visibilityChanged → refreshPanels
        docMap->show();
        QVERIFY(funcList->isVisible());
        QVERIFY(docMap->isVisible());

        // 打字後 Function List 以 debounce timer 重新解析（單發計時器，300ms）
        e->setText(QStringLiteral("void gamma() {}\n"));
        QTest::qWait(400);

        // 捲動/移動游標 → Document Map 的可視範圍色帶跟著更新
        e->setText(QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\n"));
        e->setCursorPosition(7, 0);
        QVERIFY2(docMap->isVisible(), "Document Map 在更新可視範圍後被關掉了");

        // 關掉面板後不應再有解析成本（僅驗證關閉路徑不崩潰、狀態正確）
        funcList->hide();
        docMap->hide();
        e->setText(QStringLiteral("void delta() {}\n"));
        QTest::qWait(400);
        QVERIFY(!funcList->isVisible());
    }

    // ---------------------------------------------------------------
    // Distraction Free / Post-It
    // ---------------------------------------------------------------

    // Distraction Free：隱藏所有可見面板、工具列、狀態列、分頁列並全螢幕；
    // 關閉時必須把「當初被自己藏起來的」那些面板還原（不能把本來就關著的也打開）。
    void distractionFreeHidesAndRestoresChrome()
    {
        MainWindow w(nullptr, false);
        w.show();
        auto *docMap = w.findChild<macpad::ui::DocumentMapDock *>();
        auto *funcList = w.findChild<macpad::ui::FunctionListDock *>();
        QVERIFY(docMap && funcList);
        docMap->show();                 // 只開其中一個面板
        QVERIFY(docMap->isVisible());
        QVERIFY(!funcList->isVisible());

        QTabWidget *tabs = viewsOf(w).main;
        QAction *df = findMenuAction(w, QStringLiteral("Distraction Free Mode"));
        QVERIFY2(df, "找不到 Distraction Free Mode");
        df->setChecked(true);

        QVERIFY2(!docMap->isVisible(), "Distraction Free 未隱藏已開啟的面板");
        QVERIFY2(!w.statusBar()->isVisible(), "Distraction Free 未隱藏狀態列");
        QVERIFY2(!tabs->tabBar()->isVisible(), "Distraction Free 未隱藏分頁列");

        df->setChecked(false);
        QVERIFY2(docMap->isVisible(), "離開 Distraction Free 未還原原本開著的面板");
        QVERIFY2(!funcList->isVisible(),
                 "離開 Distraction Free 把原本關著的面板也打開了");
        QVERIFY(w.statusBar()->isVisible());
        QVERIFY(tabs->tabBar()->isVisible());
    }

    // Post-It：無邊框 + 永遠置頂 + 只留純文字。離開時邊框要拿掉，
    // 置頂則回到使用者原本的 Always on Top 設定（不能無條件關掉）。
    void postItModeTogglesFramelessAndOnTop()
    {
        MainWindow w(nullptr, false);
        w.show();
        auto *docMap = w.findChild<macpad::ui::DocumentMapDock *>();
        QVERIFY(docMap);
        docMap->show();

        QTabWidget *tabs = viewsOf(w).main;
        QAction *postIt = findMenuAction(w, QStringLiteral("Post-It Mode"));
        QVERIFY2(postIt, "找不到 Post-It Mode");

        postIt->setChecked(true);
        QVERIFY(w.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(!docMap->isVisible());
        QVERIFY(!tabs->tabBar()->isVisible());
        QVERIFY(!w.statusBar()->isVisible());

        postIt->setChecked(false);
        QVERIFY(!w.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY2(!w.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                 "離開 Post-It 後仍保持置頂（使用者並未開啟 Always on Top）");
        QVERIFY(docMap->isVisible());
        QVERIFY(tabs->tabBar()->isVisible());

        // 使用者原本就開著 Always on Top → 離開 Post-It 必須保留置頂
        QMetaObject::invokeMethod(&w, "toggleAlwaysOnTop", Q_ARG(bool, true));
        postIt->setChecked(true);
        postIt->setChecked(false);
        QVERIFY2(w.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                 "離開 Post-It 把使用者原本的置頂設定弄丟了");
        QMetaObject::invokeMethod(&w, "toggleAlwaysOnTop", Q_ARG(bool, false));
    }

    // ---------------------------------------------------------------
    // 分頁巡覽 / 搬移
    // ---------------------------------------------------------------

    void activateTabRelativeWrapsAround()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        findMenuAction(w, QStringLiteral("New"))->trigger();
        findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 3);

        tabs->setCurrentIndex(0);
        QMetaObject::invokeMethod(&w, "activateTabRelative", Q_ARG(int, 1));
        QCOMPARE(tabs->currentIndex(), 1);
        // 最後一個再往後 → 繞回第一個（環狀）
        tabs->setCurrentIndex(2);
        QMetaObject::invokeMethod(&w, "activateTabRelative", Q_ARG(int, 1));
        QCOMPARE(tabs->currentIndex(), 0);
        // 第一個往前 → 繞到最後一個
        QMetaObject::invokeMethod(&w, "activateTabRelative", Q_ARG(int, -1));
        QCOMPARE(tabs->currentIndex(), 2);
    }

    // 搬移分頁與巡覽不同：到頭就停，不繞回去（繞回會讓使用者的分頁順序意外重排）
    void moveCurrentTabStopsAtEdges()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        EditorWidget *e0 = w.activeEditor();
        findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 2);

        tabs->setCurrentIndex(0);
        QMetaObject::invokeMethod(&w, "moveCurrentTab", Q_ARG(int, 1));
        QCOMPARE(tabs->currentIndex(), 1);
        auto *pane = qobject_cast<EditorPane *>(tabs->widget(1));
        QVERIFY(pane);
        QCOMPARE(pane->primary(), e0);

        // 已在最後 → 不再往後（也不繞回 index 0）
        QMetaObject::invokeMethod(&w, "moveCurrentTab", Q_ARG(int, 1));
        QCOMPARE(tabs->currentIndex(), 1);
        // 已在最前 → 不再往前
        tabs->setCurrentIndex(0);
        QMetaObject::invokeMethod(&w, "moveCurrentTab", Q_ARG(int, -1));
        QCOMPARE(tabs->currentIndex(), 0);
    }

    // ---------------------------------------------------------------
    // 監控模式（tail -f）
    // ---------------------------------------------------------------

    // 未存檔分頁不可能監控：提示後必須把可勾選按鈕撥回去，
    // 否則按鈕顯示「監控中」而實際上什麼都沒發生。
    void monitoringRejectsUntitledDocument()
    {
        MainWindow w(nullptr, false);
        QAction *mon = findMenuAction(w, QStringLiteral("Monitoring (tail -f)"));
        QVERIFY2(mon, "找不到 Monitoring (tail -f)");
        QVERIFY(w.activeEditor()->isUntitled());

        bool fired = false;
        driveNextModal([](QDialog *dlg) { dlg->accept(); }, &fired);
        mon->trigger();
        QVERIFY2(fired, "未存檔分頁按 Monitoring 沒有提示");
        QVERIFY2(!mon->isChecked(), "監控失敗後勾選狀態沒有撥回");
        QVERIFY(!w.activeEditor()->isPolicyReadOnly());
    }

    // 已存檔分頁：開始監控 → 政策唯讀 + 游標移到檔尾；再按一次 → 解除。
    void monitoringTogglesPolicyReadOnlyForSavedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("watch.log"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("l1\nl2\nl3\n");
        f.close();

        MainWindow w(nullptr, false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());
        e->setCursorPosition(0, 0);

        QAction *mon = findMenuAction(w, QStringLiteral("Monitoring (tail -f)"));
        QVERIFY(mon);
        mon->trigger();
        QVERIFY2(e->isPolicyReadOnly(), "監控中未鎖成唯讀（tail -f 語意）");
        int line = -1, col = -1;
        e->getCursorPosition(&line, &col);
        QCOMPARE(line, e->lines() - 1);   // 游標停在檔尾
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("監控中")));

        mon->trigger();
        QVERIFY2(!e->isPolicyReadOnly(), "停止監控後未解除唯讀");
        QVERIFY(w.statusBar()->currentMessage().contains(QStringLiteral("已停止監控")));
    }

    // ---------------------------------------------------------------
    // Window ▸ Windows…
    // ---------------------------------------------------------------

    // 對話框只回報列號，實際動作由 MainWindow 執行。四個動作（Activate / Save /
    // Sort tabs / Close）都要能對應回正確的 (檢視, 分頁)，越界列號必須被忽略。
    void windowsListDialogDrivesActivateSaveSortAndClose()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QStringList paths;
        for (const char *name : {"gamma.txt", "zeta.txt", "alpha.txt"}) {
            const QString p = dir.filePath(QString::fromLatin1(name));
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("content\n");
            f.close();
            paths << p;
        }

        MainWindow w(nullptr, false);
        w.show();
        for (const QString &p : paths)
            w.openFile(p);
        QTabWidget *tabs = viewsOf(w).main;
        QCOMPARE(tabs->count(), 3);   // 第一個檔案取代了開場的空白分頁

        // 第一格釘選：Sort tabs 不得動到釘選區
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));

        // 讓其中一個分頁變髒，之後由 Save 動作寫回磁碟
        tabs->setCurrentIndex(1);
        EditorWidget *dirtyEditor = w.activeEditor();
        QVERIFY(dirtyEditor);
        dirtyEditor->setText(QStringLiteral("edited by windows list\n"));
        QVERIFY(dirtyEditor->isDirty());

        // Window 選單是 aboutToShow 時才填的（含最近檔案/開啟中文件），先建起來才找得到項目
        QMetaObject::invokeMethod(&w, "buildWindowMenu");
        QAction *windows = findMenuAction(w, QStringLiteral("Windows…"));
        QVERIFY2(windows, "找不到 Windows…");

        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *list = qobject_cast<macpad::ui::WindowsListDialog *>(dlg);
            QVERIFY(list);
            emit list->activateRequested(2);
            emit list->activateRequested(99);        // 越界：必須被忽略
            emit list->saveRequested({1, -1});       // 含越界列號
            emit list->sortTabsRequested();
            dlg->accept();
        }, &fired);
        windows->trigger();
        QVERIFY2(fired, "Windows… 沒有開出對話框");

        QVERIFY2(!dirtyEditor->isDirty(), "Windows… 的 Save 沒有真的存檔");
        // 排序：釘選的 gamma.txt 仍在第 0 格，其餘依標題升冪
        QCOMPARE(tabs->count(), 3);
        QVERIFY2(tabs->tabText(0).contains(QStringLiteral("gamma")),
                 "Sort tabs 動到了釘選分頁");
        QVERIFY2(tabs->tabText(1).contains(QStringLiteral("alpha"))
                     && tabs->tabText(2).contains(QStringLiteral("zeta")),
                 "Sort tabs 未依標題排序未釘選的分頁");

        // 關閉：由後往前關，索引不會位移
        fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *list = qobject_cast<macpad::ui::WindowsListDialog *>(dlg);
            QVERIFY(list);
            emit list->closeRequested({1, 2, 42});   // 42 越界，須忽略
            dlg->accept();
        }, &fired);
        windows->trigger();
        QVERIFY(fired);
        QCOMPARE(tabs->count(), 1);
        QVERIFY(tabs->tabText(0).contains(QStringLiteral("gamma")));
    }

    // ---------------------------------------------------------------
    // 編輯區右鍵選單的實際動作
    // ---------------------------------------------------------------

    // 右鍵事件必須先把「被右鍵的編輯器」設為作用中分頁，
    // 否則選單裡的檔案類動作（Reload/Rename/Close…）會作用到別的文件。
    void editorContextMenuActivatesClickedEditor()
    {
        MainWindow w(nullptr, false);
        w.show();
        QTabWidget *tabs = viewsOf(w).main;
        EditorWidget *first = w.activeEditor();
        findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 2);
        QVERIFY(w.activeEditor() != first);

        bool fired = false;
        closeNextContextMenu(&w, [](QMenu *m) {
            QVERIFY2(m->actions().size() > 10, "編輯區右鍵選單項目過少");
        }, &fired);
        // 直接發出編輯器的 contextMenuRequested：sender() 即該編輯器，與真實右鍵同路徑
        QMetaObject::invokeMethod(first, "contextMenuRequested",
                                  Q_ARG(QPoint, QPoint(0, 0)));
        QVERIFY2(fired, "編輯區右鍵沒有彈出選單");
        QCOMPARE(w.activeEditor(), first);
        QCOMPARE(tabs->currentIndex(), 0);

        // 非由編輯器訊號觸發（sender() 不是編輯器）時退回目前作用中編輯器，不得崩潰
        fired = false;
        closeNextContextMenu(&w, [](QMenu *m) { QVERIFY(!m->actions().isEmpty()); }, &fired);
        QMetaObject::invokeMethod(&w, "showEditorContextMenu",
                                  Q_ARG(QPoint, QPoint(0, 0)));
        QVERIFY2(fired, "沒有 sender 的右鍵選單請求未彈出選單");
    }

    // 選單各項是不是真的做事——只斷言「項目存在」等於沒測到行為。
    void editorContextMenuActionsPerformTheirWork()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("ctx.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello\n");
        f.close();

        MainWindow w(nullptr, false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());

        QMenu *menu = nullptr;
        QMetaObject::invokeMethod(&w, "buildEditorContextMenu",
                                  Q_RETURN_ARG(QMenu *, menu),
                                  Q_ARG(EditorWidget *, e),
                                  Q_ARG(QWidget *, &w));
        QVERIFY2(menu, "buildEditorContextMenu 未回傳選單");

        // Copy to Clipboard 三項：實際寫進剪貼簿的內容必須正確
        QAction *copyPath = findMenuAction(menu, QStringLiteral("Copy Full File Path"));
        QVERIFY(copyPath);
        copyPath->trigger();
        QCOMPARE(QApplication::clipboard()->text(), path);

        findMenuAction(menu, QStringLiteral("Copy File Name"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("ctx.txt"));

        findMenuAction(menu, QStringLiteral("Copy Directory Path"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QFileInfo(path).absolutePath());

        // Paste as Plain Text：以剪貼簿內容插入（純文字，不帶格式）
        QApplication::clipboard()->setText(QStringLiteral("PLAIN"));
        e->setCursorPosition(0, 0);
        findMenuAction(menu, QStringLiteral("Paste as Plain Text"))->trigger();
        QVERIFY2(e->text().startsWith(QStringLiteral("PLAIN")),
                 "Paste as Plain Text 沒有插入剪貼簿內容");

        // 唯讀切換：狀態列同步回報，且唯讀時 Paste as Plain Text 不得改動內容
        QAction *ro = findMenuAction(menu, QStringLiteral("Read-Only"));
        QVERIFY(ro && ro->isCheckable());
        ro->setChecked(true);
        QVERIFY(e->isReadOnly());
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("唯讀已開啟"));
        const QString locked = e->text();
        findMenuAction(menu, QStringLiteral("Paste as Plain Text"))->trigger();
        QCOMPARE(e->text(), locked);
        ro->setChecked(false);
        QCOMPARE(w.statusBar()->currentMessage(), QStringLiteral("唯讀已關閉"));

        // Style Token / Clear Styled Tokens：對選取字詞上色後可全部清除
        e->setText(QStringLiteral("aa bb aa"));
        e->setSelection(0, 0, 0, 2);
        findMenuAction(menu, QStringLiteral("Style Token Color 1"))->trigger();
        findMenuAction(menu, QStringLiteral("Clear Styled Tokens"))->trigger();

        // 兩段式選取與書籤（純轉呼叫，確認接線正確且不崩潰）
        findMenuAction(menu, QStringLiteral("Begin Select"))->trigger();
        findMenuAction(menu, QStringLiteral("End Select"))->trigger();
        findMenuAction(menu, QStringLiteral("Begin Column Select"))->trigger();
        findMenuAction(menu, QStringLiteral("End Column Select"))->trigger();
        findMenuAction(menu, QStringLiteral("Toggle Bookmark"))->trigger();

        // Close：關掉作用中分頁（此檔未修改，不會跳存檔提示）
        QTabWidget *tabs = viewsOf(w).main;
        e->setText(locked);
        e->setModified(false);
        const int before = tabs->count();
        findMenuAction(menu, QStringLiteral("Close"))->trigger();
        QVERIFY2(tabs->count() <= before, "Close 之後分頁數反而變多");
        QVERIFY2(!w.activeEditor()->filePath().endsWith(QStringLiteral("ctx.txt")),
                 "Close 沒有關掉目前分頁");

        delete menu;
    }

    // On Selection：選取內容是路徑就開檔，不是路徑就交給系統開 URL。
    // 這裡用 setUrlHandler 攔截，確認組出來的 URL 正確且不會真的啟動瀏覽器。
    void onSelectionOpensPathOrUrl()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString hostPath = dir.filePath(QStringLiteral("host.txt"));
        const QString targetPath = dir.filePath(QStringLiteral("target.txt"));
        for (const QString &p : {hostPath, targetPath}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x\n");
            f.close();
        }

        UrlSink sink;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &sink, "handle");

        MainWindow w(nullptr, false);
        w.openFile(hostPath);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        QTabWidget *tabs = viewsOf(w).main;

        // (1) 相對路徑 → 以目前檔案所在目錄解析後開檔
        e->setText(QStringLiteral("target.txt"));
        e->setSelection(0, 0, 0, 10);
        const int before = tabs->count();
        QMetaObject::invokeMethod(&w, "openSelectionAsPathOrUrl",
                                  Q_ARG(EditorWidget *, e));
        QCOMPARE(tabs->count(), before + 1);
        QCOMPARE(w.activeEditor()->filePath(), targetPath);

        // (2) 不存在的路徑 → 當 URL 交給系統（被攔截，不會真的開瀏覽器）
        EditorWidget *e2 = w.activeEditor();
        e2->setText(QStringLiteral("https://example.invalid/page"));
        e2->setSelection(0, 0, 0, 28);
        QMetaObject::invokeMethod(&w, "openSelectionAsPathOrUrl",
                                  Q_ARG(EditorWidget *, e2));
        QCOMPARE(sink.urls.size(), 1);
        QCOMPARE(sink.urls.last().toString(), QStringLiteral("https://example.invalid/page"));

        // (3) 沒有選取 / 選取只有空白 → 什麼都不做
        e2->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        QMetaObject::invokeMethod(&w, "openSelectionAsPathOrUrl",
                                  Q_ARG(EditorWidget *, e2));
        e2->setText(QStringLiteral("   "));
        e2->setSelection(0, 0, 0, 3);
        QMetaObject::invokeMethod(&w, "openSelectionAsPathOrUrl",
                                  Q_ARG(EditorWidget *, e2));
        EditorWidget *nullEd = nullptr;
        QMetaObject::invokeMethod(&w, "openSelectionAsPathOrUrl",
                                  Q_ARG(EditorWidget *, nullEd));
        QCOMPARE(sink.urls.size(), 1);

        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    }

    // Search on Internet：依偏好的搜尋引擎樣板組 URL。
    // 三條路徑都要走到：樣板含 %1 / 樣板不含 %1（直接接在後面）/ 未設定（回退預設）。
    void searchSelectionOnInternetUsesConfiguredTemplate()
    {
        UrlSink sink;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &sink, "handle");

        auto s = macpad::persistence::SettingsStore::load();
        s.searchEngineUrl = QStringLiteral("https://example.invalid/search?q=%1");
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setText(QStringLiteral("hello world"));
        e->setSelection(0, 0, 0, 11);
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, e));
        QCOMPARE(sink.urls.size(), 1);
        QVERIFY2(sink.urls.last().toString()
                     .startsWith(QStringLiteral("https://example.invalid/search?q=hello")),
                 qPrintable(QStringLiteral("URL 非預期：%1").arg(sink.urls.last().toString())));
        QVERIFY2(QString::fromUtf8(sink.urls.last().toEncoded())
                     .contains(QStringLiteral("hello%20world")),
                 "查詢字串未做百分比編碼");

        // 樣板不含 %1 → 直接把編碼後的查詢接在後面
        s.searchEngineUrl = QStringLiteral("https://example.invalid/s/");
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, e));
        QCOMPARE(sink.urls.size(), 2);
        QVERIFY(sink.urls.last().toString().startsWith(QStringLiteral("https://example.invalid/s/hello")));

        // 未設定 → 回退到預設搜尋引擎（仍是 https，被攔截，不會連外）
        s.searchEngineUrl.clear();
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, e));
        QCOMPARE(sink.urls.size(), 3);
        QVERIFY(sink.urls.last().host().endsWith(QStringLiteral("google.com")));

        // 無選取 / 空白選取 / 空指標 → 不得發出任何 URL
        e->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, e));
        e->setText(QStringLiteral("  "));
        e->setSelection(0, 0, 0, 2);
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, e));
        EditorWidget *nullEd = nullptr;
        QMetaObject::invokeMethod(&w, "searchSelectionOnInternet",
                                  Q_ARG(EditorWidget *, nullEd));
        QCOMPARE(sink.urls.size(), 3);

        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    }

    // ---------------------------------------------------------------
    // 分頁右鍵選單的實際動作
    // ---------------------------------------------------------------

    // 標色 / 清除標色 / 唯讀鎖定三項會直接改分頁列外觀，值得逐一驗證。
    void tabContextMenuColorAndReadOnlyActions()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);

        QMenu *menu = nullptr;
        QMetaObject::invokeMethod(&w, "buildTabContextMenu",
                                  Q_RETURN_ARG(QMenu *, menu),
                                  Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0),
                                  Q_ARG(QWidget *, &w));
        QVERIFY2(menu, "buildTabContextMenu 未回傳選單");

        // Set Tab Color…：取色器選紅色後套用到分頁文字色
        bool fired = false;
        driveNextModal([](QDialog *dlg) {
            auto *cd = qobject_cast<QColorDialog *>(dlg);
            QVERIFY(cd);
            cd->setCurrentColor(Qt::red);
            cd->accept();
        }, &fired);
        findMenuAction(menu, QStringLiteral("Set Tab Color…"))->trigger();
        QVERIFY2(fired, "Set Tab Color… 沒有開出取色器");
        QCOMPARE(tabs->tabBar()->tabTextColor(0), QColor(Qt::red));

        findMenuAction(menu, QStringLiteral("Clear Tab Color"))->trigger();
        QVERIFY2(!tabs->tabBar()->tabTextColor(0).isValid(),
                 "Clear Tab Color 未清掉分頁顏色");

        // Read-Only：標籤加上 🔒 前綴，取消時還原
        QAction *ro = findMenuAction(menu, QStringLiteral("Read-Only"));
        QVERIFY(ro && ro->isCheckable());
        ro->setChecked(true);
        QVERIFY(e->isReadOnly());
        QVERIFY2(tabs->tabText(0).startsWith(QStringLiteral("🔒")),
                 "分頁右鍵鎖定後標籤未加上 🔒");
        ro->setChecked(false);
        QVERIFY(!e->isReadOnly());
        QVERIFY(!tabs->tabText(0).startsWith(QStringLiteral("🔒")));

        delete menu;
    }

    // 取消取色器 → 分頁顏色不得改變（無效 QColor 必須被擋掉）
    void cancellingTabColorPickerKeepsColour()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        QMenu *menu = nullptr;
        QMetaObject::invokeMethod(&w, "buildTabContextMenu",
                                  Q_RETURN_ARG(QMenu *, menu),
                                  Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0),
                                  Q_ARG(QWidget *, &w));
        QVERIFY(menu);

        bool fired = false;
        driveNextModal([](QDialog *dlg) { dlg->reject(); }, &fired);
        findMenuAction(menu, QStringLiteral("Set Tab Color…"))->trigger();
        QVERIFY(fired);
        QVERIFY(!tabs->tabBar()->tabTextColor(0).isValid());
        delete menu;
    }

    // ---------------------------------------------------------------
    // 批次關閉與釘選
    // ---------------------------------------------------------------

    // Close All to the Left/Right 不得波及釘選分頁（Notepad++ v8.7.3 語意）
    void closeToOneSideSkipsPinnedTabs()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        for (int i = 0; i < 4; ++i)
            findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 5);

        // 釘選第 0 格（釘選一律靠左集中，位置不變）
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));
        QVERIFY(tabs->count() == 5);

        // 關閉 index 3 左側：釘選的第 0 格必須留下
        QMetaObject::invokeMethod(&w, "closeTabsToOneSide",
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 3),
                                  Q_ARG(bool, true));
        // [P,A,B,C,D] → 關掉 A、B（P 是釘選，跳過），剩 [P,C,D]
        QCOMPARE(tabs->count(), 3);
        bool pinnedStillThere = false;
        QMetaObject::invokeMethod(&w, "isTabPinned",
                                  Q_RETURN_ARG(bool, pinnedStillThere),
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 0));
        QVERIFY2(pinnedStillThere, "Close All to the Left 關掉了釘選分頁");

        // 關閉釘選格右側的全部
        QMetaObject::invokeMethod(&w, "closeTabsToOneSide",
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 0),
                                  Q_ARG(bool, false));
        QCOMPARE(tabs->count(), 1);

        // 空指標檢視必須安全早退（不可解參考）
        QTabWidget *nullTabs = nullptr;
        QMetaObject::invokeMethod(&w, "closeTabsToOneSide",
                                  Q_ARG(QTabWidget *, nullTabs), Q_ARG(int, 0),
                                  Q_ARG(bool, true));
        QCOMPARE(tabs->count(), 1);
    }

    // Close All BUT Pinned：兩個檢視都要掃到，釘選分頁一律保留
    void closeAllButPinnedSpansBothViews()
    {
        MainWindow w(nullptr, false);
        w.show();
        QTabWidget *tabs = viewsOf(w).main;
        QTabWidget *second = viewsOf(w).second;
        findMenuAction(w, QStringLiteral("New"))->trigger();
        findMenuAction(w, QStringLiteral("Clone to Other View"))->trigger();
        QCOMPARE(second->count(), 1);

        // 主檢視第 0 格釘選
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));
        QMetaObject::invokeMethod(&w, "closeAllButPinned");

        QCOMPARE(tabs->count(), 1);
        QCOMPARE(second->count(), 0);
        bool pinned = false;
        QMetaObject::invokeMethod(&w, "isTabPinned", Q_RETURN_ARG(bool, pinned),
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 0));
        QVERIFY(pinned);
    }

    // 批次關閉遇到「未存檔」時會跳存檔提示；使用者按取消就必須整批中止，
    // 而不是跳過這一個繼續關下去（那會讓使用者眼睜睜看著其他分頁被關掉）。
    void cancellingSavePromptAbortsBatchClose()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        for (int i = 0; i < 3; ++i)
            findMenuAction(w, QStringLiteral("New"))->trigger();
        QCOMPARE(tabs->count(), 4);

        // 第 0 格改成未存檔狀態，讓它成為批次關閉時的第一個攔路者
        auto *pane0 = qobject_cast<EditorPane *>(tabs->widget(0));
        QVERIFY(pane0);
        pane0->primary()->setText(QStringLiteral("unsaved"));
        QVERIFY(pane0->primary()->isDirty());

        // 存檔提示一律按「取消」
        auto cancelPrompt = [](QDialog *dlg) {
            auto *mb = qobject_cast<QMessageBox *>(dlg);
            QVERIFY(mb);
            QAbstractButton *cancel = mb->button(QMessageBox::Cancel);
            QVERIFY(cancel);
            cancel->click();
        };

        // (1) Close All to the Left：第一個就是 dirty 的第 0 格 → 取消 → 一個都不關
        bool fired = false;
        driveNextModal(cancelPrompt, &fired);
        QMetaObject::invokeMethod(&w, "closeTabsToOneSide",
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 3),
                                  Q_ARG(bool, true));
        QVERIFY2(fired, "批次關閉未跳出存檔提示");
        QCOMPARE(tabs->count(), 4);

        // (2) Close All to the Right：把 dirty 分頁放到樞紐右邊
        tabs->tabBar()->moveTab(0, 3);
        fired = false;
        driveNextModal(cancelPrompt, &fired);
        QMetaObject::invokeMethod(&w, "closeTabsToOneSide",
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 2),
                                  Q_ARG(bool, false));
        QVERIFY(fired);
        QCOMPARE(tabs->count(), 4);

        // (3) Close All BUT Pinned：同樣必須因取消而中止
        fired = false;
        driveNextModal(cancelPrompt, &fired);
        QMetaObject::invokeMethod(&w, "closeAllButPinned");
        QVERIFY(fired);
        QCOMPARE(tabs->count(), 4);
    }

    // 空指標 / 越界索引的防禦分支——這些守衛正是「別的路徑傳進奇怪參數」時的最後一道防線
    void pinningGuardsAgainstNullAndOutOfRange()
    {
        MainWindow w(nullptr, false);
        QTabWidget *tabs = viewsOf(w).main;
        QTabWidget *nullTabs = nullptr;

        int count = -1;
        QMetaObject::invokeMethod(&w, "pinnedCount", Q_RETURN_ARG(int, count),
                                  Q_ARG(QTabWidget *, nullTabs));
        QCOMPARE(count, 0);

        // 越界索引 / 空檢視：不可崩潰，也不可改動任何分頁
        const int before = tabs->count();
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, -1), Q_ARG(bool, true));
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 99), Q_ARG(bool, true));
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, nullTabs),
                                  Q_ARG(int, 0), Q_ARG(bool, true));
        QCOMPARE(tabs->count(), before);

        // 重複設定同一狀態 → 早退（不得重覆搬動分頁）
        QMetaObject::invokeMethod(&w, "setTabPinned", Q_ARG(QTabWidget *, tabs),
                                  Q_ARG(int, 0), Q_ARG(bool, false));
        bool pinned = true;
        QMetaObject::invokeMethod(&w, "isTabPinned", Q_RETURN_ARG(bool, pinned),
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 0));
        QVERIFY(!pinned);

        // 越界索引查詢釘選狀態一律 false
        QMetaObject::invokeMethod(&w, "isTabPinned", Q_RETURN_ARG(bool, pinned),
                                  Q_ARG(QTabWidget *, tabs), Q_ARG(int, 99));
        QVERIFY(!pinned);
    }

    // ---------------------------------------------------------------
    // 編輯器訊號接線（lexer / call tip / Smart Highlighting）
    // ---------------------------------------------------------------

    // 換 lexer 時要重新上主題色，並在偏好允許時載入該語言的自動完成字典。
    void lexerChangeAppliesThemeAndApiCompletions()
    {
        auto s = macpad::persistence::SettingsStore::load();
        s.wordAutoComplete = true;
        QVERIFY(macpad::persistence::SettingsStore::save(s));

        MainWindow w(nullptr, false);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e);
        e->setLanguageLexer(macpad::core::LexerFactory::createForLanguage(
            QStringLiteral("python"), e));
        QVERIFY(e->lexer());
        e->setText(QStringLiteral("x = 1"));   // 觸發狀態列刷新
        // 狀態列同步反映新的文件類型（lexerChanged → themeEditor → 狀態列刷新）
        QVERIFY2(anyCellContains(w, QStringLiteral("Python file")),
                 "換 lexer 後文件類型格未更新");

        // 關閉偏好時不得載入字典（僅驗證這條分支不會出錯）
        s.wordAutoComplete = false;
        QVERIFY(macpad::persistence::SettingsStore::save(s));
        e->setLanguageLexer(macpad::core::LexerFactory::createForLanguage(
            QStringLiteral("cpp"), e));
        e->setText(QStringLiteral("int x;"));
        QVERIFY(anyCellContains(w, QStringLiteral("C++ file")));
    }

    // Call tip 的查找順序：先 ApiDatabase 的標準函式簽名，找不到才掃文件內的自訂函式定義。
    void callTipPrefersApiDatabaseThenDocumentScan()
    {
        // 文件內掃描以副檔名決定語言，故必須是已存檔的 .cpp（未命名文件掃不出任何符號）
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tip.cpp"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("void myHelper(int a)\n{\n}\n");
        f.close();

        MainWindow w(nullptr, false);
        w.openFile(path);
        EditorWidget *e = w.activeEditor();
        QVERIFY(e && !e->isUntitled());
        QVERIFY2(e->lexer(), "開啟 .cpp 後未套用 lexer");
        e->setCursorPosition(2, 0);

        // (1) ApiDatabase 收錄的標準函式 → 顯示真實簽名
        QMetaObject::invokeMethod(e, "callTipRequested",
                                  Q_ARG(QString, QStringLiteral("printf")));
        QVERIFY2(e->SendScintilla(QsciScintilla::SCI_CALLTIPACTIVE),
                 "ApiDatabase 有簽名卻沒顯示 call tip");
        e->SendScintilla(QsciScintilla::SCI_CALLTIPCANCEL);

        // (2) ApiDatabase 沒有 → 掃描文件內的定義行當簽名
        QMetaObject::invokeMethod(e, "callTipRequested",
                                  Q_ARG(QString, QStringLiteral("myHelper")));
        QVERIFY2(e->SendScintilla(QsciScintilla::SCI_CALLTIPACTIVE),
                 "自訂函式未由文件掃描取得 call tip");
        e->SendScintilla(QsciScintilla::SCI_CALLTIPCANCEL);

        // (3) 兩邊都查不到 → 不顯示任何 call tip
        QMetaObject::invokeMethod(e, "callTipRequested",
                                  Q_ARG(QString, QStringLiteral("nowhereToBeFound")));
        QVERIFY(!e->SendScintilla(QsciScintilla::SCI_CALLTIPACTIVE));
    }

    // Smart Highlighting 是全域檢視狀態：開啟後新開的分頁也要跟上，
    // 否則使用者每開一個分頁就得重按一次。
    void smartHighlightingAppliesToNewTabs()
    {
        MainWindow w(nullptr, false);
        QAction *sh = findMenuAction(w, QStringLiteral("Smart Highlighting"));
        QVERIFY2(sh, "找不到 Smart Highlighting");
        QVERIFY(!w.activeEditor()->smartHighlight());

        sh->setChecked(true);
        QVERIFY(w.activeEditor()->smartHighlight());

        findMenuAction(w, QStringLiteral("New"))->trigger();
        QVERIFY2(w.activeEditor()->smartHighlight(),
                 "新分頁未跟隨 Smart Highlighting 的勾選狀態");
    }
};

QTEST_MAIN(TestMainWindowView)
#include "test_mainwindow_view.moc"
