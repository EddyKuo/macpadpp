// 單元測試：Find / Replace 對話框（features/search/FindReplaceDialog）
//
// 這個對話框先前完全沒有測試——它的邏輯幾乎全落在 private slots 與 private helper 裡，
// 表面上「看不見」，但它是使用者每天最常按的功能，任何回歸都會直接被踩到。
//
// 三個測試策略上的取捨，先講清楚：
//  1) private slots 一律用 QMetaObject::invokeMethod 觸發。QtTest 走 meta-object，
//     存取權限不影響 invoke，因此不需要為了測試把 slot 改成 public（不動 src/ 是前提）。
//  2) 子 widget 一律用「建立順序」而非 objectName 取得（原始碼沒有設 objectName）。
//     順序若被改動，widgetInventory() 會第一個爆掉並指出是哪裡變了——這正是我們要的。
//  3) 絕對不碰 modal API。replaceAll() 內含 QMessageBox::question，在 offscreen 下會
//     卡死整個測試程序，故 initTestCase() 先把 confirmReplaceAll 寫成 false，
//     讓 replaceAll 走「不確認」路徑。這也是唯一能自動化測到取代結果的方式。
//
// 另外：對話框會把勾選狀態與搜尋歷史寫進 QSettings、把偏好讀自 settings.json，
// 因此測試把兩者都導向暫存位置，不污染使用者的真實設定。
//
// 已知且刻意未涵蓋的區塊（不是漏測，是測不到或不該測）：
//  - replaceAll() 內 confirmReplaceAll=true 的 QMessageBox::question 分支：modal，
//    在 offscreen 下無人可按會直接卡死測試程序。本檔一律以 confirmReplaceAll=false 執行。
//  - doFind() 的 fromStart=true 分支：目前沒有任何呼叫端傳 true（findNext 與兩個
//    volatile 版本都傳 false），是尚無使用者的參數。
//  - pushHistory() 的截斷迴圈：combo 自身已設 setMaxCount(kHistoryMax)，
//    count 永遠不會超過上限，迴圈體實際上到不了（historyIsCappedAtTwenty 驗的是結果）。
//  - rememberMatch()/selectionIsRememberedMatch() 的 !m_editor 早退與空選取早退：
//    兩者唯一的呼叫端 replaceOne() 已先檢查過 m_editor 與 hasSelectedText()，屬防禦性程式碼。
#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/EditorWidget.h"
#include "features/search/FindReplaceDialog.h"
#include "persistence/AppPaths.h"
#include "persistence/SettingsStore.h"

using macpad::core::EditorWidget;
using macpad::features::FindReplaceDialog;
using macpad::persistence::AppPaths;
using macpad::persistence::Settings;
using macpad::persistence::SettingsStore;

namespace {

// 勾選框在對話框中的建立順序（= FindReplaceDialog 建構子的順序）。
// 用 enum 取代「魔術數字 2」，讓測試讀起來還是講人話。
enum CheckIndex {
    CbMatchCase = 0,
    CbWholeWord,
    CbRegex,
    CbWrap,
    CbInSelection,
    CbDotAll,
    CbExtended,
};

// 只取直系子物件：可編輯 QComboBox 內部也有一個 QLineEdit，
// 遞迴搜尋會把它一起撈出來，破壞「第 0 個是碼點下限欄」的假設。
template <typename T>
QList<T *> directChildren(const QObject *parent)
{
    QList<T *> out;
    const auto kids = parent->children();
    for (QObject *o : kids)
        if (auto *t = qobject_cast<T *>(o))
            out << t;
    return out;
}

QCheckBox *box(const FindReplaceDialog *dlg, CheckIndex i)
{
    return directChildren<QCheckBox>(dlg).value(static_cast<int>(i));
}

QComboBox *findCombo(const FindReplaceDialog *dlg)
{
    return directChildren<QComboBox>(dlg).value(0);
}

QComboBox *replaceCombo(const FindReplaceDialog *dlg)
{
    return directChildren<QComboBox>(dlg).value(1);
}

QLineEdit *findEdit(const FindReplaceDialog *dlg) { return findCombo(dlg)->lineEdit(); }
QLineEdit *replaceEdit(const FindReplaceDialog *dlg) { return replaceCombo(dlg)->lineEdit(); }

// 碼點範圍的兩個欄位是對話框的直系 QLineEdit（combo 的內部欄位不是）
QLineEdit *cpLoEdit(const FindReplaceDialog *dlg) { return directChildren<QLineEdit>(dlg).value(0); }
QLineEdit *cpHiEdit(const FindReplaceDialog *dlg) { return directChildren<QLineEdit>(dlg).value(1); }

// 狀態列是唯一一個「不是固定標題」的 QLabel。用排除法而非索引，
// 因為 report() 會改它的文字，用文字比對反而更不穩。
QLabel *statusLabel(const FindReplaceDialog *dlg)
{
    static const QStringList captions = {QStringLiteral("Find:"), QStringLiteral("Replace:"),
                                         QStringLiteral("Opacity:"),
                                         QStringLiteral("Codepoint range:")};
    const auto labels = directChildren<QLabel>(dlg);
    for (QLabel *l : labels)
        if (!captions.contains(l->text()))
            return l;
    return nullptr;
}

QString status(const FindReplaceDialog *dlg) { return statusLabel(dlg)->text(); }

}  // namespace

class TestFindReplaceDialog : public QObject {
    Q_OBJECT

    // private slot 觸發器。invokeMethod 失敗代表 slot 被改名/移除，
    // 那是我們要當場擋下的破壞性變更，所以這裡直接斷言而非靜默略過。
    static void call(QObject *o, const char *slot)
    {
        QVERIFY2(QMetaObject::invokeMethod(o, slot, Qt::DirectConnection), slot);
    }

    // 設定尋找欄。setText 會觸發 textChanged → incrementalFind，游標可能被搬走，
    // 故所有測試在填完欄位後都要自己重設游標，不能沿用填字前的位置。
    static void setFind(FindReplaceDialog *dlg, const QString &text)
    {
        findEdit(dlg)->setText(text);
    }

    QTemporaryDir *m_configDir = nullptr;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // QSettings（勾選狀態＋歷史）導向測試 scope，不動使用者的真實設定
        QCoreApplication::setOrganizationName(QStringLiteral("macpadpp-test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_findreplacedialog"));

        // settings.json（keepFindDialogOpen / confirmReplaceAll / findInSelectionThreshold）
        // 導向暫存目錄。confirmReplaceAll 必須為 false——否則 replaceAll 會彈出 modal
        // QMessageBox，在 offscreen 下無人可按，測試會直接掛住。
        m_configDir = new QTemporaryDir;
        QVERIFY(m_configDir->isValid());
        AppPaths::setConfigDirOverride(m_configDir->path());
        Settings s;
        s.confirmReplaceAll = false;
        s.keepFindDialogOpen = true;
        s.findInSelectionThreshold = 0;
        QVERIFY(SettingsStore::save(s));
    }

    void cleanupTestCase()
    {
        AppPaths::setConfigDirOverride(QString());
        delete m_configDir;
    }

    // 每個測試都從乾淨的 QSettings 開始：勾選狀態與歷史會跨 dialog 實例保留，
    // 前一個測試的殘留會讓後面的測試莫名其妙地紅或綠。
    void init()
    {
        QSettings settings;
        settings.remove(QStringLiteral("FindReplaceDialog"));
        settings.sync();
    }

    // ── 組成與預設值 ─────────────────────────────────────────────────────────

    // 其餘測試全都依賴「第 n 個 checkbox 是哪一個」，故先把這份對應釘死。
    // 一旦有人在中間插入新選項，這裡會第一個失敗並指出真正的原因，
    // 而不是讓十幾個測試各自以難懂的方式壞掉。
    void widgetInventory()
    {
        FindReplaceDialog dlg;
        const auto boxes = directChildren<QCheckBox>(&dlg);
        const QStringList texts = {
            QStringLiteral("Match case"),
            QStringLiteral("Whole word"),
            QStringLiteral("Regex"),
            QStringLiteral("Wrap around"),
            QStringLiteral("In selection"),
            QStringLiteral("'.' matches newline"),
            QStringLiteral("Extended (\\n \\r \\t \\0 \\xNN)"),
        };
        QCOMPARE(boxes.size(), texts.size());
        for (int i = 0; i < texts.size(); ++i)
            QCOMPARE(boxes.at(i)->text(), texts.at(i));

        // 「Wrap around」是唯一預設開啟的選項——關掉它會讓搜尋在檔尾停住，
        // 與 Notepad++ 的預設行為不符。
        QVERIFY(box(&dlg, CbWrap)->isChecked());
        for (int i = 0; i < boxes.size(); ++i)
            if (i != CbWrap)
                QVERIFY2(!boxes.at(i)->isChecked(), qPrintable(texts.at(i)));

        // 按鈕清單（文字即功能，缺一個代表功能被拿掉了）
        QStringList buttons;
        const auto btns = directChildren<QPushButton>(&dlg);
        for (const QPushButton *b : btns)
            buttons << b->text();
        for (const QString &want : {QStringLiteral("Find Next"), QStringLiteral("Replace"),
                                    QStringLiteral("Replace All"), QStringLiteral("Mark All"),
                                    QStringLiteral("Count"), QStringLiteral("↕"),
                                    QStringLiteral("Find (Volatile) Next"),
                                    QStringLiteral("Find (Volatile) Previous"),
                                    QStringLiteral("Find Codepoint")})
            QVERIFY2(buttons.contains(want), qPrintable(want));

        // 透明度滑桿對應 setWindowOpacity 的 0.3..1.0
        auto *slider = directChildren<QSlider>(&dlg).value(0);
        QVERIFY(slider);
        QCOMPARE(slider->minimum(), 30);
        QCOMPARE(slider->maximum(), 100);
        QCOMPARE(slider->value(), 100);

        QVERIFY(statusLabel(&dlg));
        QVERIFY(status(&dlg).isEmpty());
        QVERIFY(!dlg.isModal());   // 非模態：使用者要能邊搜尋邊編輯
    }

    // 勾選狀態必須跨 dialog 實例（實際上是跨程式重啟）保留，
    // 否則每次開對話框都要重勾 Regex——這是原始碼特地寫 saveSearchOption 的理由。
    void searchOptionsPersistAcrossDialogs()
    {
        {
            FindReplaceDialog dlg;
            box(&dlg, CbMatchCase)->setChecked(true);
            box(&dlg, CbRegex)->setChecked(true);
            box(&dlg, CbExtended)->setChecked(true);
            box(&dlg, CbWrap)->setChecked(false);   // 預設 true 的那個也要能被記成 false
        }
        FindReplaceDialog fresh;
        QVERIFY(box(&fresh, CbMatchCase)->isChecked());
        QVERIFY(box(&fresh, CbRegex)->isChecked());
        QVERIFY(box(&fresh, CbExtended)->isChecked());
        QVERIFY(!box(&fresh, CbWrap)->isChecked());
        QVERIFY(!box(&fresh, CbWholeWord)->isChecked());
    }

    // ── 搜尋歷史 ─────────────────────────────────────────────────────────────

    // 歷史只在「使用者真的按下搜尋」時才記錄。若在 textChanged 就記，
    // 增量搜尋會把 f / fo / foo 每個中間狀態都塞進歷史（原始碼註解特別說明了這點）。
    void historyOnlyRecordedOnAction()
    {
        EditorWidget editor;
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        setFind(&dlg, QStringLiteral("foo"));
        QCOMPARE(findCombo(&dlg)->count(), 0);   // 打字階段不入歷史

        call(&dlg, "findNext");
        QCOMPARE(findCombo(&dlg)->count(), 1);
        QCOMPARE(findCombo(&dlg)->itemText(0), QStringLiteral("foo"));
    }

    // 最近使用者優先＋去重：重複搜尋同一個字不該讓歷史長出兩筆。
    void historyDeduplicatesAndOrdersByRecency()
    {
        EditorWidget editor;
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        for (const QString &term : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("a")}) {
            setFind(&dlg, term);
            call(&dlg, "findNext");
        }
        QCOMPARE(findCombo(&dlg)->count(), 2);
        QCOMPARE(findCombo(&dlg)->itemText(0), QStringLiteral("a"));   // 最近用過的置頂
        QCOMPARE(findCombo(&dlg)->itemText(1), QStringLiteral("b"));
    }

    // 歷史有上限（kHistoryMax = 20）。沒有上限的話 QSettings 會無限膨脹，
    // 且下拉選單會長到蓋掉整個畫面。
    void historyIsCappedAtTwenty()
    {
        EditorWidget editor;
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        for (int i = 0; i < 25; ++i) {
            setFind(&dlg, QStringLiteral("term%1").arg(i));
            call(&dlg, "findNext");
        }
        QCOMPARE(findCombo(&dlg)->count(), 20);
        QCOMPARE(findCombo(&dlg)->itemText(0), QStringLiteral("term24"));
        QCOMPARE(findCombo(&dlg)->itemText(19), QStringLiteral("term5"));
    }

    // 歷史跨 session 保留，但欄位開啟時必須是空的——否則會蓋掉
    // 「以目前選取內容預填」的行為（原始碼 setCurrentIndex(-1) 的用意）。
    void historyRestoresButLeavesFieldEmpty()
    {
        {
            EditorWidget editor;
            FindReplaceDialog dlg;
            dlg.setEditor(&editor);
            setFind(&dlg, QStringLiteral("needle"));
            replaceEdit(&dlg)->setText(QStringLiteral("thread"));
            call(&dlg, "findNext");
        }
        FindReplaceDialog fresh;
        QCOMPARE(findCombo(&fresh)->count(), 1);
        QCOMPARE(findCombo(&fresh)->itemText(0), QStringLiteral("needle"));
        QCOMPARE(replaceCombo(&fresh)->itemText(0), QStringLiteral("thread"));
        QCOMPARE(findCombo(&fresh)->currentIndex(), -1);
        QVERIFY(findEdit(&fresh)->text().isEmpty());
        QVERIFY(replaceEdit(&fresh)->text().isEmpty());
    }

    // ── 基本尋找 ─────────────────────────────────────────────────────────────

    // 沒有綁定編輯器時所有動作都必須安靜地什麼都不做（MainWindow 尚未指派分頁的狀態）。
    // 特別是不能報「找不到」——那會誤導使用者以為檔案裡真的沒有這個字。
    void actionsWithoutEditorAreNoOps()
    {
        FindReplaceDialog dlg;
        setFind(&dlg, QStringLiteral("x"));
        replaceEdit(&dlg)->setText(QStringLiteral("y"));
        for (const char *slot : {"findNext", "findNextVolatile", "findPreviousVolatile",
                                 "replaceOne", "replaceAll", "markAll", "countAll",
                                 "findCodepointRange"})
            call(&dlg, slot);
        QVERIFY2(status(&dlg).isEmpty(), qPrintable(status(&dlg)));
    }

    void findNextSelectsMatchAndClearsStatus()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("alpha beta alpha"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        setFind(&dlg, QStringLiteral("beta"));
        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");

        QVERIFY(editor.hasSelectedText());
        QCOMPARE(editor.selectedText(), QStringLiteral("beta"));
        QVERIFY(status(&dlg).isEmpty());
    }

    void findNextReportsMiss()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("alpha"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        setFind(&dlg, QStringLiteral("zzz"));
        call(&dlg, "findNext");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「zzz」"));
    }

    // 空的尋找字串等同「沒有要找的東西」：doFind 直接回 false，
    // findNext 於是報出一則沒有意義的訊息——但至少不可以真的去搜尋或移動游標。
    void emptyFindTextDoesNotMoveCursor()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("alpha"));
        editor.setCursorPosition(0, 2);
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        call(&dlg, "findNext");
        int line = -1, index = -1;
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 0);
        QCOMPARE(index, 2);
        QVERIFY(!editor.hasSelectedText());
    }

    // Wrap around 是可關的：關掉之後從檔尾往後搜尋必須「找不到」，
    // 而不是偷偷繞回檔頭——這是使用者用來確認「後面沒有了」的方式。
    void wrapAroundControlsSearchFromEnd()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("target\nfiller\nfiller"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("target"));

        box(&dlg, CbWrap)->setChecked(false);
        editor.setCursorPosition(2, 6);
        call(&dlg, "findNext");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「target」"));

        box(&dlg, CbWrap)->setChecked(true);
        editor.setCursorPosition(2, 6);
        call(&dlg, "findNext");
        QVERIFY(status(&dlg).isEmpty());
        QCOMPARE(editor.selectedText(), QStringLiteral("target"));
    }

    void matchCaseIsHonoured()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("Alpha alpha"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("alpha"));
        box(&dlg, CbMatchCase)->setChecked(true);

        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");
        int lf = -1, if_ = -1, lt = -1, it = -1;
        editor.getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 6);   // 命中的是後面那個小寫 alpha，不是句首的 Alpha
    }

    void regexSearchUsesPattern()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("id 42 end"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("\\d+"));

        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");
        QCOMPARE(editor.selectedText(), QStringLiteral("42"));
    }

    // ── Volatile 尋找 ───────────────────────────────────────────────────────

    // Volatile 尋找的存在理由只有一個：它「不」更新最近命中記錄。
    // 因此 volatile 移動過之後按 Replace，不該取代目前這個（非正式命中的）選取。
    // 這正是原始碼 remember=false 參數要保護的不變式。
    //
    // 這裡用增量搜尋（打字）而非 findNext 來建立「正式命中」，單純是為了讓本測試
    // 只驗 volatile 這一件事，不牽扯搜尋歷史。
    // （pushHistory 曾因「先移除再插回」而在中途清空 lineEdit、觸發 incrementalFind
    //   把命中記錄指到別處；該問題已修，見 findNextTwiceDoesNotSkipAMatch。）
    void volatileFindDoesNotArmReplace()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("x1 x2 x3"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        replaceEdit(&dlg)->setText(QStringLiteral("Z"));

        editor.setCursorPosition(0, 0);
        setFind(&dlg, QStringLiteral("x"));   // 增量搜尋：命中並記下第一個 x
        QCOMPARE(editor.selectedText(), QStringLiteral("x"));

        call(&dlg, "findNextVolatile");       // 移到第二個 x，但不更新記錄
        int lf = -1, if_ = -1, lt = -1, it = -1;
        editor.getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 3);

        call(&dlg, "replaceOne");
        // 目前選取不是「最近一次正式命中」，故不得取代
        QCOMPARE(editor.text(), QStringLiteral("x1 x2 x3"));
    }

    // 迴歸測試：連按兩次 Find Next 不可多跳一個匹配。
    //
    // findNext 結束時會把搜尋詞收進歷史。第二次按下時該詞已存在，pushHistory 走的是
    // 「先 removeItem 再 insertItem」——移除的瞬間 combo 變空，連帶清掉 lineEdit，
    // 插回時又填了回來。這一來一往發出的 textChanged 連到 incrementalFind，於是在
    // 使用者毫無察覺的情況下又搜尋了一次、把選取移到別處。
    // 症狀：文件裡三個 x，連按兩次 Find Next 不會停在第二個而是第三個（或更後面）。
    void findNextTwiceDoesNotSkipAMatch()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("x1 x2 x3"));   // x 位於 0、3、6
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        editor.setCursorPosition(0, 0);
        setFind(&dlg, QStringLiteral("x"));   // 增量搜尋先命中第 0 個
        QCOMPARE(editor.selectedText(), QStringLiteral("x"));

        int lf = -1, idx = -1, lt = -1, it = -1;

        call(&dlg, "findNext");               // 第一次：此時歷史尚無此詞，不觸發移除
        editor.getSelection(&lf, &idx, &lt, &it);
        QCOMPARE(idx, 3);

        call(&dlg, "findNext");               // 第二次：詞已在歷史中，正是出問題的路徑
        editor.getSelection(&lf, &idx, &lt, &it);
        QCOMPARE(idx, 6);
    }

    // Volatile 版本連搜尋歷史都不留——這是它與 findNext 最直接可觀察的差異，
    // 也是「暫時看一眼下一個匹配」這個使用情境該有的行為。
    void volatileFindDoesNotRecordHistory()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("x1 x2"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("x2"));

        editor.setCursorPosition(0, 0);
        call(&dlg, "findNextVolatile");
        QCOMPARE(editor.selectedText(), QStringLiteral("x2"));
        QCOMPARE(findCombo(&dlg)->count(), 0);
    }

    void volatilePreviousSearchesBackwards()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("hit filler hit"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("hit"));

        editor.setCursorPosition(0, 14);   // 檔尾
        call(&dlg, "findPreviousVolatile");
        int lf = -1, if_ = -1, lt = -1, it = -1;
        editor.getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(if_, 11);                 // 往回找到的是後面那個 hit
        QVERIFY(status(&dlg).isEmpty());
    }

    void volatileFindReportsMiss()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("nothing here"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("qqq"));

        call(&dlg, "findNextVolatile");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「qqq」"));
        statusLabel(&dlg)->clear();
        call(&dlg, "findPreviousVolatile");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「qqq」"));
    }

    // ── In selection ────────────────────────────────────────────────────────

    // 「In selection」的邊界是在勾選當下記錄的，不是搜尋當下才問編輯器——
    // 因為搜尋本身就會改變選取。這裡驗證：範圍內的命中算數，範圍外的不算。
    void inSelectionRestrictsFind()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aaa\nbbb\naaa"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        editor.setSelection(1, 0, 1, 3);            // 只選中間那行 bbb
        box(&dlg, CbInSelection)->setChecked(true); // 勾選當下記下 [1,0]..[1,3]

        setFind(&dlg, QStringLiteral("b"));
        // 限制分支刻意不環繞（wrap=false），故必須從範圍起點開始找，
        // 否則游標停在範圍尾端時會直接判定「範圍內沒有」。
        editor.setCursorPosition(1, 0);
        call(&dlg, "findNext");
        QVERIFY2(status(&dlg).isEmpty(), qPrintable(status(&dlg)));
        QCOMPARE(editor.selectedText(), QStringLiteral("b"));

        // 'a' 只存在於限制範圍之外，必須被判為「找不到」
        setFind(&dlg, QStringLiteral("a"));
        editor.setCursorPosition(1, 3);
        call(&dlg, "findNext");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「a」"));

        // 全篇都不存在的字串同樣要如實回報，而不是被限制邏輯吃掉錯誤
        setFind(&dlg, QStringLiteral("zzz"));
        editor.setCursorPosition(1, 0);
        call(&dlg, "findNext");
        QCOMPARE(status(&dlg), QStringLiteral("找不到「zzz」"));
    }

    // 取消勾選必須清掉記錄的邊界，否則之後的搜尋會被一個看不見的舊範圍綁住。
    void unheckingInSelectionClearsRestriction()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aaa\nbbb\naaa"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        editor.setSelection(1, 0, 1, 3);
        box(&dlg, CbInSelection)->setChecked(true);
        box(&dlg, CbInSelection)->setChecked(false);

        setFind(&dlg, QStringLiteral("a"));
        editor.setCursorPosition(1, 3);
        call(&dlg, "findNext");
        QVERIFY2(status(&dlg).isEmpty(), qPrintable(status(&dlg)));
        QCOMPARE(editor.selectedText(), QStringLiteral("a"));
    }

    // 勾選時若根本沒有選取，也不該留下半個邊界（-1 表示未記錄）；
    // 否則後續搜尋會走進限制分支卻沒有合理的範圍。
    void inSelectionWithoutSelectionKeepsSearchGlobal()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aaa\nbbb"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        editor.setCursorPosition(0, 0);
        box(&dlg, CbInSelection)->setChecked(true);   // 無選取 → 不記錄邊界

        setFind(&dlg, QStringLiteral("b"));
        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");
        QVERIFY2(status(&dlg).isEmpty(), qPrintable(status(&dlg)));
        QCOMPARE(editor.selectedText(), QStringLiteral("b"));
    }

    // ── 取代 ────────────────────────────────────────────────────────────────

    // Replace 只在「目前選取正是最近一次尋找的命中」時才動手，
    // 避免使用者手動圈了一段文字後按 Replace 就被吃掉。
    void replaceOneReplacesRememberedMatchThenAdvances()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat cat"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        replaceEdit(&dlg)->setText(QStringLiteral("dog"));

        editor.setCursorPosition(0, 0);
        setFind(&dlg, QStringLiteral("cat"));   // 增量搜尋命中第一個 cat（見 volatile 測試的說明）
        call(&dlg, "replaceOne");               // 取代它，並自動找下一個
        QCOMPARE(editor.text(), QStringLiteral("dog cat"));
        QCOMPARE(editor.selectedText(), QStringLiteral("cat"));   // 已前進到下一個
    }

    void replaceOneIgnoresManualSelection()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat cat"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("cat"));
        replaceEdit(&dlg)->setText(QStringLiteral("dog"));

        editor.setSelection(0, 4, 0, 7);   // 使用者自己圈的，不是搜尋結果
        call(&dlg, "replaceOne");
        QCOMPARE(editor.text(), QStringLiteral("cat cat"));
    }

    void replaceAllCountsAndReports()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("a a a a"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("a"));
        replaceEdit(&dlg)->setText(QStringLiteral("b"));

        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("b b b b"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 4 處"));
    }

    void replaceAllWithEmptyFindIsNoOp()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("untouched"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        replaceEdit(&dlg)->setText(QStringLiteral("boom"));

        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("untouched"));
        QVERIFY(status(&dlg).isEmpty());   // 連「已取代 0 處」都不該報
    }

    void replaceAllWithRegexBackreference()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("2024-01 2025-02"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("(\\d+)-(\\d+)"));
        replaceEdit(&dlg)->setText(QStringLiteral("\\2-\\1"));

        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("01-2024 02-2025"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 2 處"));
    }

    // In selection 的全部取代走的是完全不同的實作（記憶體內 QString::replace 後整段寫回），
    // 因此必須另外驗證：範圍外的同名文字不能被動到。
    void replaceAllInSelectionLeavesOutsideIntact()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat\ncat\ncat"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("cat"));
        replaceEdit(&dlg)->setText(QStringLiteral("dog"));

        editor.setSelection(1, 0, 1, 3);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "replaceAll");

        QCOMPARE(editor.text(), QStringLiteral("cat\ndog\ncat"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 1 處"));
    }

    // In selection + regex 又是另一條分支（QRegularExpression 而非編輯核心）。
    // 順便驗 Whole word 會被包成 \b...\b、未勾 Match case 時大小寫不敏感。
    void replaceAllInSelectionWithRegexWholeWord()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat category CAT"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        box(&dlg, CbWholeWord)->setChecked(true);
        setFind(&dlg, QStringLiteral("cat"));
        replaceEdit(&dlg)->setText(QStringLiteral("dog"));

        editor.setSelection(0, 0, 0, 16);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "replaceAll");

        // category 是較長的字，\b 邊界保護它不被改；CAT 因未勾 Match case 而中
        QCOMPARE(editor.text(), QStringLiteral("dog category dog"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 2 處"));
    }

    // 「'.' matches newline」只在 regex 下有意義：關掉時 . 不跨行，
    // 開啟時才能用 a.b 匹配跨越換行的內容。這個選項若沒接上，
    // 使用者會以為自己的正則寫錯了。
    void replaceAllInSelectionHonoursDotMatchesNewline()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("a\nb"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("a.b"));
        replaceEdit(&dlg)->setText(QStringLiteral("X"));

        editor.setSelection(0, 0, 1, 1);
        box(&dlg, CbInSelection)->setChecked(true);

        call(&dlg, "replaceAll");            // 未開 dotAll：. 不跨行 → 不匹配
        QCOMPARE(editor.text(), QStringLiteral("a\nb"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 0 處"));

        editor.setSelection(0, 0, 1, 1);
        box(&dlg, CbDotAll)->setChecked(true);
        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("X"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 1 處"));
    }

    // 無效的正則不可以讓對話框當掉或做出半套取代——原始碼以 re.isValid() 擋下，
    // 結果應該是「取代 0 處、內容完全沒動」。
    void replaceAllInSelectionWithInvalidRegexIsSafe()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("abc"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("("));      // 未閉合的群組
        replaceEdit(&dlg)->setText(QStringLiteral("X"));

        editor.setSelection(0, 0, 0, 3);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "replaceAll");

        QCOMPARE(editor.text(), QStringLiteral("abc"));
        QCOMPARE(status(&dlg), QStringLiteral("已取代 0 處"));
    }

    // In selection 勾了但沒有選取時，必須退回全域取代路徑而不是什麼都不做，
    // 否則使用者會以為功能壞了（勾了一個不影響現況的選項卻導致取代失效）。
    void replaceAllInSelectionWithoutSelectionFallsBackToGlobal()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("a a"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("a"));
        replaceEdit(&dlg)->setText(QStringLiteral("b"));

        editor.setCursorPosition(0, 0);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("b b"));
    }

    // ── Mark All / Count ────────────────────────────────────────────────────

    void markAllReportsCount()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat dog cat bird cat"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("cat"));

        call(&dlg, "markAll");
        QCOMPARE(status(&dlg), QStringLiteral("已標記 3 處"));
    }

    // Count 的重點是「只計數、不移動」：按下去之後游標與選取都要原封不動。
    void countAllDoesNotMoveCursor()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aa aa aa"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("aa"));
        editor.setCursorPosition(0, 1);

        call(&dlg, "countAll");
        QCOMPARE(status(&dlg), QStringLiteral("共 3 處匹配"));
        int line = -1, index = -1;
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 0);
        QCOMPARE(index, 1);
        QVERIFY(!editor.hasSelectedText());
    }

    void countAllWithEmptyFindIsNoOp()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aa"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        call(&dlg, "countAll");
        QVERIFY(status(&dlg).isEmpty());
    }

    // In selection 的計數不能用編輯核心的全文 countMatches，
    // 否則勾了「只在選取內」卻回報整份文件的數量——這是最容易誤導人的那種錯。
    void countAllInSelectionCountsOnlyWithinSelection()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("aa\naa\naa"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("aa"));

        editor.setSelection(0, 0, 1, 2);   // 只涵蓋前兩行
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "countAll");
        QCOMPARE(status(&dlg), QStringLiteral("共 2 處匹配"));
    }

    void countAllInSelectionWithRegex()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("a1 b22 c333"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("\\d+"));

        editor.setSelection(0, 0, 0, 11);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "countAll");
        QCOMPARE(status(&dlg), QStringLiteral("共 3 處匹配"));
    }

    // 選取內計數同樣要套用 Whole word（實作是把 pattern 包成 \b...\b）；
    // 漏掉的話 category 也會被算成一筆 cat。
    void countAllInSelectionWithRegexWholeWord()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("cat category cat"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        box(&dlg, CbWholeWord)->setChecked(true);
        setFind(&dlg, QStringLiteral("cat"));

        editor.setSelection(0, 0, 0, 16);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "countAll");
        QCOMPARE(status(&dlg), QStringLiteral("共 2 處匹配"));
    }

    void countAllInSelectionWithInvalidRegexReportsZero()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("abc"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("("));

        editor.setSelection(0, 0, 0, 3);
        box(&dlg, CbInSelection)->setChecked(true);
        call(&dlg, "countAll");
        QCOMPARE(status(&dlg), QStringLiteral("共 0 處匹配"));
    }

    // ── 其他控制項 ──────────────────────────────────────────────────────────

    void swapExchangesFindAndReplace()
    {
        FindReplaceDialog dlg;
        setFind(&dlg, QStringLiteral("from"));
        replaceEdit(&dlg)->setText(QStringLiteral("to"));

        call(&dlg, "swapFindReplace");
        QCOMPARE(findEdit(&dlg)->text(), QStringLiteral("to"));
        QCOMPARE(replaceEdit(&dlg)->text(), QStringLiteral("from"));
    }

    // 滑桿數值是百分比，setWindowOpacity 要的是 0..1；少除一次 100 會讓視窗直接消失。
    void opacitySliderMapsToWindowOpacity()
    {
        FindReplaceDialog dlg;
        auto *slider = directChildren<QSlider>(&dlg).value(0);
        QVERIFY(slider);

        // QWidget 內部把透明度存成 0..255 的整數，回讀會有 1/255 的量化誤差，
        // 故用容差比較而非 qFuzzyCompare。
        slider->setValue(50);
        QVERIFY2(qAbs(dlg.windowOpacity() - 0.5) < 0.01,
                 qPrintable(QString::number(dlg.windowOpacity())));
        slider->setValue(30);
        QVERIFY2(qAbs(dlg.windowOpacity() - 0.3) < 0.01,
                 qPrintable(QString::number(dlg.windowOpacity())));
        slider->setValue(100);
        QVERIFY2(qAbs(dlg.windowOpacity() - 1.0) < 0.01,
                 qPrintable(QString::number(dlg.windowOpacity())));
    }

    // 增量搜尋（打字即定位）：輸入時就該選到第一個匹配，不必按 Find Next。
    void incrementalFindSelectsWhileTyping()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("hello world"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        editor.setCursorPosition(0, 0);
        setFind(&dlg, QStringLiteral("world"));   // textChanged → incrementalFind
        QCOMPARE(editor.selectedText(), QStringLiteral("world"));
    }

    // 清空欄位不可以把游標亂丟——空字串在增量搜尋裡是「沒有查詢」而非「查詢空字串」。
    void incrementalFindIgnoresEmptyText()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("hello"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        setFind(&dlg, QStringLiteral("ell"));
        editor.setCursorPosition(0, 4);
        setFind(&dlg, QString());
        int line = -1, index = -1;
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 0);
        QCOMPARE(index, 4);
    }

    // 增量搜尋的命中要算「正式命中」，接著按 Replace 應該直接取代它。
    void incrementalFindArmsReplace()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("hello world"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        replaceEdit(&dlg)->setText(QStringLiteral("there"));

        editor.setCursorPosition(0, 0);
        setFind(&dlg, QStringLiteral("world"));
        call(&dlg, "replaceOne");
        QVERIFY(editor.text().startsWith(QStringLiteral("hello there")));
    }

    // ── 碼點範圍尋找 ────────────────────────────────────────────────────────

    void codepointRangeRejectsInvalidInput_data()
    {
        QTest::addColumn<QString>("lo");
        QTest::addColumn<QString>("hi");
        QTest::newRow("both empty") << QString() << QString();
        QTest::newRow("lo only") << QStringLiteral("0x41") << QString();
        QTest::newRow("not a number") << QStringLiteral("zz") << QStringLiteral("0x41");
        QTest::newRow("lo greater than hi") << QStringLiteral("0x5A") << QStringLiteral("0x41");
    }

    // 無效輸入必須明講原因，而不是靜靜地報「找不到」——後者會讓使用者
    // 以為檔案裡沒有這種字元，實際上是他打錯了範圍。
    void codepointRangeRejectsInvalidInput()
    {
        QFETCH(QString, lo);
        QFETCH(QString, hi);

        EditorWidget editor;
        editor.setText(QStringLiteral("abc"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(lo);
        cpHiEdit(&dlg)->setText(hi);

        call(&dlg, "findCodepointRange");
        QCOMPARE(status(&dlg), QStringLiteral("碼點範圍無效（請輸入 lo <= hi，可用 0x 前綴）"));
    }

    void codepointRangeSelectsFirstMatchAfterCursor()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("  abc"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(QStringLiteral("0x61"));   // 'a'
        cpHiEdit(&dlg)->setText(QStringLiteral("0x7A"));   // 'z'

        editor.setCursorPosition(0, 0);
        call(&dlg, "findCodepointRange");
        QCOMPARE(editor.selectedText(), QStringLiteral("a"));
        QVERIFY2(status(&dlg).startsWith(QStringLiteral("找到碼點範圍")), qPrintable(status(&dlg)));
        QVERIFY(!status(&dlg).contains(QStringLiteral("已環繞")));
    }

    // 十進位輸入（無 0x 前綴）也要收——原始碼用 toUInt(base=0) 就是為了兩種都吃。
    void codepointRangeAcceptsDecimalInput()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("--A--"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(QStringLiteral("65"));    // 'A'
        cpHiEdit(&dlg)->setText(QStringLiteral("65"));

        editor.setCursorPosition(0, 0);
        call(&dlg, "findCodepointRange");
        QCOMPARE(editor.selectedText(), QStringLiteral("A"));
    }

    // 游標之後沒有了就要繞回檔頭再找一次，並在訊息中說明「已環繞」，
    // 否則使用者無從得知游標剛剛跳到了自己身後。
    void codepointRangeWrapsAround()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("A----\n-----"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(QStringLiteral("0x41"));
        cpHiEdit(&dlg)->setText(QStringLiteral("0x41"));

        editor.setCursorPosition(1, 5);
        call(&dlg, "findCodepointRange");
        QVERIFY2(status(&dlg).contains(QStringLiteral("已環繞")), qPrintable(status(&dlg)));
        QCOMPARE(editor.selectedText(), QStringLiteral("A"));
    }

    // 關掉 Wrap around 時不環繞，並如實回報找不到。
    void codepointRangeWithoutWrapReportsMiss()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("A----\n-----"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbWrap)->setChecked(false);
        cpLoEdit(&dlg)->setText(QStringLiteral("0x41"));
        cpHiEdit(&dlg)->setText(QStringLiteral("0x41"));

        editor.setCursorPosition(1, 5);
        call(&dlg, "findCodepointRange");
        QVERIFY2(status(&dlg).startsWith(QStringLiteral("找不到碼點範圍")),
                 qPrintable(status(&dlg)));
    }

    // BMP 外的字元（emoji）在 QString 裡是一組代理對，逐 QChar 掃描會看到
    // 兩個「半個字元」的碼點，永遠對不上使用者輸入的真實碼點。
    // 原始碼特地合併代理對，這裡驗證它確實有效。
    void codepointRangeHandlesSurrogatePairs()
    {
        const QString emoji = QString(QChar::highSurrogate(0x1F600))
                            + QString(QChar::lowSurrogate(0x1F600));
        EditorWidget editor;
        // emoji 後面必須還有內容：若把它擺在行尾，越界的索引會被 Scintilla 夾限成
        // 行長，剛好等於正確答案，這個測試就分辨不出對錯了。
        editor.setText(QStringLiteral("..") + emoji + QStringLiteral(".."));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(QStringLiteral("0x1F600"));
        cpHiEdit(&dlg)->setText(QStringLiteral("0x1F600"));

        editor.setCursorPosition(0, 0);
        call(&dlg, "findCodepointRange");
        QVERIFY2(status(&dlg).startsWith(QStringLiteral("找到碼點範圍")), qPrintable(status(&dlg)));

        // 選取範圍必須正確：QScintilla 的欄位索引是「Unicode 字元」數，而掃描用的是
        // QString 的 UTF-16 code unit 索引。emoji 在 QString 佔兩格、在 Scintilla 只
        // 算一格，故正確的選取是字元 [2,3)；直接把 QString 的 len=2 交出去會變成
        // [2,4)，多吃掉後面一個字元。
        int lf = -1, idx = -1, lt = -1, it = -1;
        editor.getSelection(&lf, &idx, &lt, &it);
        QCOMPARE(idx, 2);
        QCOMPARE(it, 3);
        QCOMPARE(editor.selectedText(), emoji);
    }

    // 起始游標位置同樣是 Unicode 字元索引。若不換算就拿去索引 QString，
    // 只要游標前方出現過 BMP 外字元，掃描起點就會整體偏移。
    void codepointRangeConvertsCursorCharIndex()
    {
        const QString emoji = QString(QChar::highSurrogate(0x1F600))
                            + QString(QChar::lowSurrogate(0x1F600));
        EditorWidget editor;
        // 😀中文😀：字元索引 0/1/2/3，但 QString 索引 0/2/3/4——前方那個 emoji 造成偏差
        editor.setText(emoji + QStringLiteral("中文") + emoji);
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        cpLoEdit(&dlg)->setText(QStringLiteral("0x1F600"));
        cpHiEdit(&dlg)->setText(QStringLiteral("0x1F600"));

        editor.setCursorPosition(0, 3);   // 字元索引 3 ＝ 第二個 emoji 之前
        call(&dlg, "findCodepointRange");
        QVERIFY2(status(&dlg).startsWith(QStringLiteral("找到碼點範圍")), qPrintable(status(&dlg)));

        int lf = -1, idx = -1, lt = -1, it = -1;
        editor.getSelection(&lf, &idx, &lt, &it);
        QCOMPARE(idx, 3);
        QCOMPARE(it, 4);
        QCOMPARE(editor.selectedText(), emoji);
    }

    // ── Extended（跳脫序列）────────────────────────────────────────────────

    void extendedEscapes_data()
    {
        QTest::addColumn<QString>("input");     // 使用者在取代欄輸入的字面文字
        QTest::addColumn<QString>("expected");  // 轉換後應寫入文件的內容

        QTest::newRow("newline") << QStringLiteral("a\\nb") << QStringLiteral("a\nb");
        QTest::newRow("carriage return") << QStringLiteral("a\\rb") << QStringLiteral("a\rb");
        QTest::newRow("tab") << QStringLiteral("a\\tb") << QStringLiteral("a\tb");
        QTest::newRow("nul") << QStringLiteral("a\\0b")
                             << (QStringLiteral("a") + QChar(QChar::Null) + QStringLiteral("b"));
        QTest::newRow("backslash") << QStringLiteral("a\\\\b") << QStringLiteral("a\\b");
        QTest::newRow("hex") << QStringLiteral("\\x41") << QStringLiteral("A");
        QTest::newRow("backspace") << QStringLiteral("a\\bb") << (QStringLiteral("a")
                                                                 + QChar(0x08)
                                                                 + QStringLiteral("b"));
        QTest::newRow("bmp unicode") << QStringLiteral("\\u4E2D") << QStringLiteral("中");
        QTest::newRow("braced unicode") << QStringLiteral("\\u{4E2D}") << QStringLiteral("中");
        // BMP 之外的碼點必須被輸出成代理對，否則寫進文件的會是一個無效的 QChar
        QTest::newRow("astral unicode") << QStringLiteral("\\u{1F600}")
                                        << (QString(QChar::highSurrogate(0x1F600))
                                            + QString(QChar::lowSurrogate(0x1F600)));
        QTest::newRow("octal") << QStringLiteral("\\o101") << QStringLiteral("A");
        QTest::newRow("decimal") << QStringLiteral("\\d65") << QStringLiteral("A");
        // 未知或不完整的跳脫序列一律原樣保留——擅自吞掉反斜線會讓使用者
        // 無法搜尋/寫入字面上的反斜線組合。
        QTest::newRow("unknown escape") << QStringLiteral("a\\qb") << QStringLiteral("a\\qb");
        QTest::newRow("truncated hex") << QStringLiteral("\\x4") << QStringLiteral("\\x4");
        QTest::newRow("bad hex digits") << QStringLiteral("\\xZZ") << QStringLiteral("\\xZZ");
        QTest::newRow("bad unicode digits") << QStringLiteral("\\uZZZZ") << QStringLiteral("\\uZZZZ");
        QTest::newRow("out of range braced") << QStringLiteral("\\u{110000}")
                                             << QStringLiteral("\\u{110000}");
        QTest::newRow("octal without digits") << QStringLiteral("\\o8") << QStringLiteral("\\o8");
        QTest::newRow("decimal without digits") << QStringLiteral("\\dz") << QStringLiteral("\\dz");
        QTest::newRow("trailing backslash") << QStringLiteral("ab\\") << QStringLiteral("ab\\");
    }

    // Extended 的轉換只在「非 Regex」時生效，且同時套用於尋找與取代欄。
    // 這裡從取代欄下手，因為結果可以直接在文件內容上斷言。
    void extendedEscapes()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);

        EditorWidget editor;
        editor.setText(QStringLiteral("@"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbExtended)->setChecked(true);
        setFind(&dlg, QStringLiteral("@"));
        replaceEdit(&dlg)->setText(input);

        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), expected);
    }

    // 尋找欄同樣要轉換：文件裡是一個真的 tab，使用者輸入 "\t" 應該找得到。
    void extendedAppliesToFindField()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("a\tb"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbExtended)->setChecked(true);
        setFind(&dlg, QStringLiteral("a\\tb"));

        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");
        QVERIFY2(status(&dlg).isEmpty(), qPrintable(status(&dlg)));
        QCOMPARE(editor.selectedText(), QStringLiteral("a\tb"));
    }

    // 勾了 Regex 時 Extended 必須讓路：兩套跳脫規則同時作用會互相打架
    //（\d 在 regex 是「數字」，在 extended 是「十進位碼點」）。
    void regexDisablesExtendedUnescaping()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("x1x"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        box(&dlg, CbExtended)->setChecked(true);
        box(&dlg, CbRegex)->setChecked(true);
        setFind(&dlg, QStringLiteral("\\d"));   // regex 語意：一個數字
        replaceEdit(&dlg)->setText(QStringLiteral("Y"));

        call(&dlg, "replaceAll");
        QCOMPARE(editor.text(), QStringLiteral("xYx"));
    }

    // ── showFind 與偏好設定 ─────────────────────────────────────────────────

    // 開啟對話框時以目前選取預填尋找欄，是最常被依賴的小便利。
    void showFindPrefillsFromSelection()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("alpha beta"));
        editor.setSelection(0, 6, 0, 10);
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        dlg.showFind(/*replaceMode=*/false);
        QCOMPARE(findEdit(&dlg)->text(), QStringLiteral("beta"));
        QVERIFY(dlg.isVisible());
        QVERIFY(status(&dlg).isEmpty());
        dlg.close();
    }

    // replaceMode 只影響焦點落在哪一欄（v1 一律顯示取代列）。
    void showFindFocusesReplaceFieldInReplaceMode()
    {
        EditorWidget editor;
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);

        dlg.showFind(/*replaceMode=*/true);
        QVERIFY(replaceEdit(&dlg)->isVisible());
        dlg.close();

        dlg.showFind(/*replaceMode=*/false);
        QVERIFY(findEdit(&dlg)->isVisible());
        dlg.close();
    }

    // 沒有選取時不可以清掉使用者上次輸入的搜尋字串。
    void showFindKeepsExistingTextWithoutSelection()
    {
        EditorWidget editor;
        editor.setText(QStringLiteral("plain"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        setFind(&dlg, QStringLiteral("kept"));

        dlg.showFind(false);
        QCOMPARE(findEdit(&dlg)->text(), QStringLiteral("kept"));
        dlg.close();
    }

    // findInSelectionThreshold：選取跨越的行數達到門檻時自動勾「In selection」，
    // 這是為了避免在大段選取上誤觸全域取代。
    void largeSelectionAutoEnablesInSelection()
    {
        Settings s = SettingsStore::load();
        const int saved = s.findInSelectionThreshold;
        s.findInSelectionThreshold = 3;
        QVERIFY(SettingsStore::save(s));

        EditorWidget editor;
        editor.setText(QStringLiteral("l0\nl1\nl2\nl3"));
        editor.setSelection(0, 0, 2, 2);   // 跨 3 行，達到門檻
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        dlg.showFind(false);
        QVERIFY2(box(&dlg, CbInSelection)->isChecked(), "跨 3 行的選取未自動啟用 In selection");
        dlg.close();

        // 上一個對話框把 inSelection=true 寫進了 QSettings；不清掉的話下一個對話框
        // 一開場就是勾選狀態，這個測試就變成什麼都沒驗到。
        {
            QSettings settings;
            settings.remove(QStringLiteral("FindReplaceDialog"));
            settings.sync();
        }

        EditorWidget small;
        small.setText(QStringLiteral("l0\nl1"));
        small.setSelection(0, 0, 0, 2);    // 只有 1 行，未達門檻
        FindReplaceDialog dlg2;
        dlg2.setEditor(&small);
        dlg2.showFind(false);
        QVERIFY(!box(&dlg2, CbInSelection)->isChecked());
        dlg2.close();

        s.findInSelectionThreshold = saved;
        QVERIFY(SettingsStore::save(s));
    }

    // keepFindDialogOpen=false 時，搜尋/取代成功後對話框自動關閉；
    // 失敗時則必須留著，否則使用者看不到「找不到」的訊息。
    void keepFindDialogOpenPreference()
    {
        Settings s = SettingsStore::load();
        s.keepFindDialogOpen = false;
        QVERIFY(SettingsStore::save(s));

        EditorWidget editor;
        editor.setText(QStringLiteral("needle"));
        FindReplaceDialog dlg;
        dlg.setEditor(&editor);
        dlg.showFind(false);               // showFind 會重新載入偏好
        setFind(&dlg, QStringLiteral("zzz"));
        call(&dlg, "findNext");
        QVERIFY2(dlg.isVisible(), "找不到時不該關閉對話框");

        setFind(&dlg, QStringLiteral("needle"));
        editor.setCursorPosition(0, 0);
        call(&dlg, "findNext");
        QVERIFY2(!dlg.isVisible(), "keepFindDialogOpen=false 時，成功尋找後應自動關閉");

        s.keepFindDialogOpen = true;
        QVERIFY(SettingsStore::save(s));
    }
};

QTEST_MAIN(TestFindReplaceDialog)
#include "test_findreplacedialog.moc"
