// 單元測試：UdlEditorDialog（Language ▸ User-Defined Language ▸ Define your language）
//
// 這個對話框沒有公開的 getter/setter，它的「行為」全都發生在私有的 collectDefinition()
// 與 loadDefinitionIntoUi() 裡。因此測試一律從**使用者看得到的介面**下手：
// 抓出真正的輸入元件、填字、按下按鈕，再檢查 UdlManager 裡多了什麼、內容對不對。
// 這樣驗到的是真實的「表單 ↔ UdlDefinition」轉換，而不是實作細節。
//
// 元件的定位方式刻意用 placeholder / 按鈕文字，而不是 findChildren 的走訪順序——
// 順序會隨版面重排而改變，placeholder 是這些欄位真正的識別特徵，
// 一旦有人把欄位改掉，測試會直接找不到而失敗，這正是我們要的訊號。
//
// 限制：exportDefinition()/importFromNppXml()/exportToNppXml() 會開 QFileDialog，
// pickColor() 會開 QColorDialog，名稱為空的儲存路徑會開 QMessageBox——
// 這些在 offscreen 下都是會卡住的 modal API，因此不在此檔覆蓋範圍內。
#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlLexer.h"
#include "features/udl/UdlManager.h"
#include "ui/UdlEditorDialog.h"

using macpad::features::kUdlMaxKeywordGroups;
using macpad::features::UdlDefinition;
using macpad::features::UdlDelimiter;
using macpad::features::UdlLexer;
using macpad::features::UdlManager;
using macpad::features::UdlStyle;
using macpad::ui::UdlEditorDialog;

namespace {

// 對話框內所有可編輯元件的把手集合。測試透過它操作表單，
// 等同於使用者在畫面上打字/勾選，不繞過任何 UI 邏輯。
struct EditorUi {
    QComboBox *picker = nullptr;
    QTabWidget *tabs = nullptr;
    QDialogButtonBox *box = nullptr;
    QSlider *transparency = nullptr;
    QPushButton *dockBtn = nullptr;
    QPushButton *renameBtn = nullptr;
    QPushButton *removeBtn = nullptr;

    QLineEdit *name = nullptr;
    QLineEdit *extensions = nullptr;
    QLineEdit *lineComment = nullptr;
    QLineEdit *blockStart = nullptr;
    QLineEdit *blockEnd = nullptr;
    QLineEdit *blockNesting = nullptr;
    QLineEdit *lineNesting = nullptr;
    QLineEdit *stringNesting = nullptr;
    QLineEdit *folderOpen = nullptr;
    QLineEdit *folderMiddle = nullptr;
    QLineEdit *folderClose = nullptr;

    QVector<QPlainTextEdit *> keywordGroups;
    QVector<QCheckBox *> keywordPrefix;
    QPlainTextEdit *operators = nullptr;
    QPlainTextEdit *delimiters = nullptr;
    QCheckBox *caseSensitive = nullptr;

    struct StyleRow {
        QPushButton *fg = nullptr;
        QPushButton *bg = nullptr;
        QCheckBox *bold = nullptr;
        QCheckBox *italic = nullptr;
        QCheckBox *underline = nullptr;
    };
    QVector<StyleRow> styleRows;
};

// 樣式列在畫面上的排列順序（對應 UdlEditorDialog::buildStylesPage 的 kStyles 表）。
// 測試要能指名「第幾列 = 哪個 styleId」，才能驗證勾選後寫回的是正確的樣式編號。
const int kStyleRowIds[] = {
    UdlLexer::Default,  UdlLexer::Keyword,  UdlLexer::Keyword2, UdlLexer::Keyword3,
    UdlLexer::Keyword4, UdlLexer::Keyword5, UdlLexer::Keyword6, UdlLexer::Keyword7,
    UdlLexer::Keyword8, UdlLexer::Comment,  UdlLexer::String,   UdlLexer::Number,
    UdlLexer::Operator, UdlLexer::Delimiter,
};
constexpr int kStyleRowCount = int(sizeof(kStyleRowIds) / sizeof(kStyleRowIds[0]));

QPushButton *buttonWithText(QDialog *dlg, const QString &text)
{
    const auto buttons = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->text() == text)
            return b;
    return nullptr;
}

// 依名稱取出 manager 中的定義；回傳值（而非指標）以免後續 save() 讓 QVector 重配置後懸空。
bool takeDefinition(const UdlManager &mgr, const QString &name, UdlDefinition &out)
{
    for (const UdlDefinition &d : mgr.definitions()) {
        if (d.name == name) {
            out = d;
            return true;
        }
    }
    return false;
}

}  // namespace

class TestUdlEditorDialog : public QObject {
    Q_OBJECT

    // 把對話框裡的元件抓成 EditorUi。用 void + 輸出參數是因為 QVERIFY 只能用在 void 函式。
    void collectUi(UdlEditorDialog *dlg, EditorUi &ui)
    {
        ui.picker = dlg->findChild<QComboBox *>();
        QVERIFY(ui.picker);
        ui.tabs = dlg->findChild<QTabWidget *>();
        QVERIFY(ui.tabs);
        ui.box = dlg->findChild<QDialogButtonBox *>();
        QVERIFY(ui.box);
        ui.transparency = dlg->findChild<QSlider *>();
        QVERIFY(ui.transparency);

        ui.dockBtn = buttonWithText(dlg, QStringLiteral("Dock"));
        QVERIFY(ui.dockBtn);
        ui.renameBtn = buttonWithText(dlg, QString::fromUtf8("Rename…"));
        QVERIFY(ui.renameBtn);
        ui.removeBtn = buttonWithText(dlg, QStringLiteral("Remove"));
        QVERIFY(ui.removeBtn);

        // 單行欄位：placeholder 唯一者直接對號入座；三個 nesting 欄位共用同一段提示，
        // 以「同一個 parent 下的建立順序」區分（block / line / string）。
        QVector<QLineEdit *> nesting;
        const auto edits = dlg->findChildren<QLineEdit *>();
        for (QLineEdit *e : edits) {
            const QString ph = e->placeholderText();
            if (ph == QStringLiteral("My Language")) ui.name = e;
            else if (ph.startsWith(QString::fromUtf8("以空白或逗號分隔"))) ui.extensions = e;
            else if (ph == QStringLiteral("//")) ui.lineComment = e;
            else if (ph == QStringLiteral("/*")) ui.blockStart = e;
            else if (ph == QStringLiteral("*/")) ui.blockEnd = e;
            else if (ph == QStringLiteral("{")) ui.folderOpen = e;
            else if (ph == QStringLiteral("}")) ui.folderClose = e;
            else if (ph.startsWith(QString::fromUtf8("逗號分隔"))) nesting.push_back(e);
            else if (ph.isEmpty()) ui.folderMiddle = e;
        }
        QCOMPARE(int(nesting.size()), 3);
        ui.blockNesting = nesting.at(0);
        ui.lineNesting = nesting.at(1);
        ui.stringNesting = nesting.at(2);
        QVERIFY(ui.name && ui.extensions && ui.lineComment && ui.blockStart && ui.blockEnd);
        QVERIFY(ui.folderOpen && ui.folderMiddle && ui.folderClose);

        const auto areas = dlg->findChildren<QPlainTextEdit *>();
        for (QPlainTextEdit *e : areas) {
            const QString ph = e->placeholderText();
            if (ph.startsWith(QString::fromUtf8("關鍵字"))) ui.keywordGroups.push_back(e);
            else if (ph.startsWith(QString::fromUtf8("運算子"))) ui.operators = e;
            else if (ph.startsWith(QString::fromUtf8("每行一組"))) ui.delimiters = e;
        }
        QCOMPARE(int(ui.keywordGroups.size()), kUdlMaxKeywordGroups);
        QVERIFY(ui.operators && ui.delimiters);

        // 勾選框：Case sensitive 唯一；Prefix Mode 有 8 個；樣式列以 "B" 為錨點，
        // 同一列（同一個 rowWidget）內的兩顆按鈕即 FG/BG，三個勾選框即 B/I/U。
        const auto checks = dlg->findChildren<QCheckBox *>();
        for (QCheckBox *c : checks) {
            if (c->text() == QStringLiteral("Case sensitive")) {
                ui.caseSensitive = c;
            } else if (c->text() == QStringLiteral("Prefix Mode")) {
                ui.keywordPrefix.push_back(c);
            } else if (c->text() == QStringLiteral("B")) {
                QWidget *rowWidget = c->parentWidget();
                QVERIFY(rowWidget);
                const auto rowButtons = rowWidget->findChildren<QPushButton *>();
                const auto rowChecks = rowWidget->findChildren<QCheckBox *>();
                QCOMPARE(int(rowButtons.size()), 2);
                QCOMPARE(int(rowChecks.size()), 3);
                EditorUi::StyleRow row;
                row.fg = rowButtons.at(0);
                row.bg = rowButtons.at(1);
                row.bold = rowChecks.at(0);
                row.italic = rowChecks.at(1);
                row.underline = rowChecks.at(2);
                QCOMPARE(row.italic->text(), QStringLiteral("I"));
                QCOMPARE(row.underline->text(), QStringLiteral("U"));
                ui.styleRows.push_back(row);
            }
        }
        QVERIFY(ui.caseSensitive);
        QCOMPARE(int(ui.keywordPrefix.size()), kUdlMaxKeywordGroups);
        QCOMPARE(int(ui.styleRows.size()), kStyleRowCount);
    }

    // 一份欄位盡量填滿的定義，用來驗證「載入 → 顯示 → 存回」不會掉資料。
    static UdlDefinition richDefinition(const QString &name)
    {
        UdlDefinition d;
        d.name = name;
        d.extensions = {"aa", "bb"};
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        d.keywordGroups[0] = {"alpha"};
        d.keywordGroups[2] = {"gamma"};
        d.keywordGroups[7] = {"omega"};
        d.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
        d.keywordGroupPrefixMode[2] = true;
        d.keywords = d.keywordGroups[0];
        d.operators = {"+", "=="};
        UdlDelimiter plain;
        plain.open = "\"";
        plain.escape = "\\";
        plain.close = "\"";
        d.delimiters.push_back(plain);
        UdlDelimiter nested;
        nested.open = "<<";
        nested.escape = QString();
        nested.close = ">>";
        nested.nesting = macpad::features::UdlNest::Number
                       | macpad::features::UdlNest::keywordBit(0);
        d.delimiters.push_back(nested);
        d.blockCommentNesting = macpad::features::UdlNest::Number;
        d.lineCommentNesting = macpad::features::UdlNest::Operator;
        d.stringNesting = macpad::features::UdlNest::keywordBit(1);
        d.folderTokens.open = "begin";
        d.folderTokens.middle = "elsif";
        d.folderTokens.close = "end";
        d.lineComment = "#";
        d.blockCommentStart = "{-";
        d.blockCommentEnd = "-}";
        d.caseSensitive = false;
        UdlStyle st;
        st.fg = "#ff0000";
        st.bg = "#00ff00";
        st.bold = true;
        st.underline = true;
        d.styles.insert(UdlLexer::Comment, st);
        return d;
    }

private slots:
    // 所有 UdlManager 都會寫入設定目錄，開測試模式避免污染使用者資料
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // 建構後應具備完整的四個分頁與全部欄位；缺任何一個都代表版面被改壞了
    void constructionBuildsCompleteForm()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        QCOMPARE(ui.tabs->count(), 4);
        QCOMPARE(ui.tabs->tabText(0), QStringLiteral("General"));
        QCOMPARE(ui.tabs->tabText(1), QStringLiteral("Keywords"));
        QCOMPARE(ui.tabs->tabText(2), QStringLiteral("Operators / Delimiters / Folding"));
        QCOMPARE(ui.tabs->tabText(3), QStringLiteral("Styles"));

        // 預設值：大小寫敏感（與 Notepad++ 新建 UDL 一致）、色彩皆為「未設定」
        QVERIFY(ui.caseSensitive->isChecked());
        for (const EditorUi::StyleRow &row : ui.styleRows) {
            QCOMPARE(row.fg->text(), QStringLiteral("(default)"));
            QCOMPARE(row.bg->text(), QStringLiteral("(default)"));
            QVERIFY(row.fg->styleSheet().isEmpty());
            QVERIFY(!row.bold->isChecked());
        }

        // 空的 manager：選單只有「新語言」一項
        QCOMPARE(ui.picker->count(), 1);
        QCOMPARE(ui.picker->itemText(0), QStringLiteral("(New Language)"));
        QCOMPARE(ui.picker->currentIndex(), 0);
    }

    // 語言選單應列出 manager 中既有的 UDL，且「(New Language)」永遠在第 0 項
    void languagePickerListsExistingDefinitions()
    {
        UdlManager mgr;
        UdlDefinition a;
        a.name = "PickerOne";
        a.extensions = {"p1"};
        QVERIFY(mgr.save(a));
        UdlDefinition b;
        b.name = "PickerTwo";
        b.extensions = {"p2"};
        QVERIFY(mgr.save(b));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        QCOMPARE(ui.picker->count(), 3);
        QCOMPARE(ui.picker->itemText(0), QStringLiteral("(New Language)"));
        QCOMPARE(ui.picker->itemText(1), QStringLiteral("PickerOne"));
        QCOMPARE(ui.picker->itemText(2), QStringLiteral("PickerTwo"));
        // 建構完成時不應自動載入任何語言（表單維持空白）
        QVERIFY(ui.name->text().isEmpty());
    }

    // 從選單挑一個既有語言，表單各欄位必須忠實呈現該定義的內容
    void selectingLanguageFillsFormFields()
    {
        UdlManager mgr;
        const UdlDefinition d = richDefinition(QStringLiteral("LoadIntoUi"));
        QVERIFY(mgr.save(d));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        ui.picker->setCurrentIndex(1);  // 觸發 loadSelectedLanguage

        QCOMPARE(ui.name->text(), QStringLiteral("LoadIntoUi"));
        QCOMPARE(ui.extensions->text(), QStringLiteral("aa bb"));
        QCOMPARE(ui.lineComment->text(), QStringLiteral("#"));
        QCOMPARE(ui.blockStart->text(), QStringLiteral("{-"));
        QCOMPARE(ui.blockEnd->text(), QStringLiteral("-}"));
        QVERIFY(!ui.caseSensitive->isChecked());

        QCOMPARE(ui.keywordGroups.at(0)->toPlainText(), QStringLiteral("alpha"));
        QCOMPARE(ui.keywordGroups.at(2)->toPlainText(), QStringLiteral("gamma"));
        QCOMPARE(ui.keywordGroups.at(7)->toPlainText(), QStringLiteral("omega"));
        QVERIFY(ui.keywordGroups.at(1)->toPlainText().isEmpty());
        QVERIFY(ui.keywordPrefix.at(2)->isChecked());
        QVERIFY(!ui.keywordPrefix.at(0)->isChecked());

        // 運算子是 QSet，顯示順序不保證，因此比對集合而非字串
        const QStringList shownOps =
            ui.operators->toPlainText().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QCOMPARE(QSet<QString>(shownOps.begin(), shownOps.end()), d.operators);

        // 分隔符：nesting 為 0 的維持三欄外觀，有 nesting 的才補上第四欄
        const QStringList delimLines =
            ui.delimiters->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(int(delimLines.size()), 2);
        QCOMPARE(delimLines.at(0), QStringLiteral("\"|\\|\""));
        QCOMPARE(delimLines.at(1), QStringLiteral("<<||>>|number,kw1"));

        QCOMPARE(ui.blockNesting->text(), QStringLiteral("number"));
        QCOMPARE(ui.lineNesting->text(), QStringLiteral("operator"));
        QCOMPARE(ui.stringNesting->text(), QStringLiteral("kw2"));
        QCOMPARE(ui.folderOpen->text(), QStringLiteral("begin"));
        QCOMPARE(ui.folderMiddle->text(), QStringLiteral("elsif"));
        QCOMPARE(ui.folderClose->text(), QStringLiteral("end"));

        // 樣式列：Comment 那列應顯示載入的色碼，其餘維持「未設定」
        const int commentRow = 9;  // kStyleRowIds 中 UdlLexer::Comment 的位置
        QCOMPARE(kStyleRowIds[commentRow], int(UdlLexer::Comment));
        QCOMPARE(ui.styleRows.at(commentRow).fg->text(), QStringLiteral("#ff0000"));
        QCOMPARE(ui.styleRows.at(commentRow).bg->text(), QStringLiteral("#00ff00"));
        QVERIFY(ui.styleRows.at(commentRow).fg->styleSheet().contains(QStringLiteral("#ff0000")));
        QVERIFY(ui.styleRows.at(commentRow).bold->isChecked());
        QVERIFY(!ui.styleRows.at(commentRow).italic->isChecked());
        QVERIFY(ui.styleRows.at(commentRow).underline->isChecked());
        QCOMPARE(ui.styleRows.at(0).fg->text(), QStringLiteral("(default)"));
        QVERIFY(!ui.styleRows.at(0).bold->isChecked());
    }

    // 切回「(New Language)」是 no-op：不應把空定義倒灌進表單，使用者正在編的內容要留著
    void selectingNewLanguageLeavesFormUntouched()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "KeepFormLang";
        d.extensions = {"kfl"};
        QVERIFY(mgr.save(d));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        ui.picker->setCurrentIndex(1);
        QCOMPARE(ui.name->text(), QStringLiteral("KeepFormLang"));
        ui.picker->setCurrentIndex(0);
        QCOMPARE(ui.name->text(), QStringLiteral("KeepFormLang"));
    }

    // 手動填表 → 按 Save：manager 應收到一份完全對應表單內容的定義，且對話框被接受
    void savingFormWritesParsedDefinition()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        ui.name->setText(QStringLiteral("  TypedLang  "));   // 前後空白應被 trim
        ui.extensions->setText(QStringLiteral("TL, Tl2  tl3"));  // 逗號/空白混用、大寫應轉小寫
        ui.lineComment->setText(QStringLiteral("--"));
        ui.blockStart->setText(QStringLiteral("(*"));
        ui.blockEnd->setText(QStringLiteral("*)"));
        ui.caseSensitive->setChecked(false);
        ui.keywordGroups.at(0)->setPlainText(QStringLiteral("if\nelse then"));
        ui.keywordGroups.at(4)->setPlainText(QStringLiteral("g5kw"));
        ui.keywordPrefix.at(4)->setChecked(true);
        ui.operators->setPlainText(QStringLiteral("+ - =="));
        ui.delimiters->setPlainText(QStringLiteral("\"|\\|\"\n<<||>>|number,kw1"));
        ui.blockNesting->setText(QStringLiteral("number, kw3"));
        ui.lineNesting->setText(QStringLiteral("string"));
        ui.stringNesting->setText(QStringLiteral("delim2"));
        ui.folderOpen->setText(QStringLiteral("do"));
        ui.folderMiddle->setText(QStringLiteral("elif"));
        ui.folderClose->setText(QStringLiteral("done"));
        ui.styleRows.at(1).italic->setChecked(true);  // Keyword group 1 = styleId 1

        QPushButton *save = ui.box->button(QDialogButtonBox::Save);
        QVERIFY(save);
        save->click();

        UdlDefinition got;
        QVERIFY(takeDefinition(mgr, QStringLiteral("TypedLang"), got));
        QCOMPARE(got.extensions, QStringList({"tl", "tl2", "tl3"}));
        QCOMPARE(got.lineComment, QStringLiteral("--"));
        QCOMPARE(got.blockCommentStart, QStringLiteral("(*"));
        QCOMPARE(got.blockCommentEnd, QStringLiteral("*)"));
        QCOMPARE(got.caseSensitive, false);

        QCOMPARE(got.keywordGroup(0), QSet<QString>({"if", "else", "then"}));
        QCOMPARE(got.keywordGroup(4), QSet<QString>({"g5kw"}));
        QVERIFY(got.keywordGroup(1).isEmpty());
        QVERIFY(got.keywordGroupPrefix(4));
        QVERIFY(!got.keywordGroupPrefix(0));
        // keywords 為向後相容欄位，必須等同第 0 組
        QCOMPARE(got.keywords, got.keywordGroup(0));

        QCOMPARE(got.operators, QSet<QString>({"+", "-", "=="}));

        QCOMPARE(int(got.delimiters.size()), 2);
        QCOMPARE(got.delimiters.at(0).open, QStringLiteral("\""));
        QCOMPARE(got.delimiters.at(0).escape, QStringLiteral("\\"));
        QCOMPARE(got.delimiters.at(0).close, QStringLiteral("\""));
        QCOMPARE(got.delimiters.at(0).nesting, 0);
        QCOMPARE(got.delimiters.at(1).open, QStringLiteral("<<"));
        QVERIFY(got.delimiters.at(1).escape.isEmpty());
        QCOMPARE(got.delimiters.at(1).close, QStringLiteral(">>"));
        QCOMPARE(got.delimiters.at(1).nesting,
                 macpad::features::UdlNest::Number | macpad::features::UdlNest::keywordBit(0));

        QCOMPARE(got.blockCommentNesting,
                 macpad::features::UdlNest::Number | macpad::features::UdlNest::keywordBit(2));
        QCOMPARE(got.lineCommentNesting, int(macpad::features::UdlNest::String));
        QCOMPARE(got.stringNesting, macpad::features::UdlNest::delimiterBit(1));

        QCOMPARE(got.folderTokens.open, QStringLiteral("do"));
        QCOMPARE(got.folderTokens.middle, QStringLiteral("elif"));
        QCOMPARE(got.folderTokens.close, QStringLiteral("done"));

        // 只有被動過的那一列才寫進 styles，其餘維持預設（不硬塞空樣式）
        QCOMPARE(int(got.styles.size()), 1);
        QVERIFY(got.styles.contains(UdlLexer::Keyword));
        QVERIFY(got.styles.value(UdlLexer::Keyword).italic);
        QVERIFY(!got.styles.value(UdlLexer::Keyword).bold);
        QVERIFY(got.styles.value(UdlLexer::Keyword).fg.isEmpty());

        // 儲存成功即關閉對話框（accept）
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QVERIFY(!dlg.isVisible());
    }

    // 載入既有語言後直接存回，內容必須原封不動——這是編輯既有 UDL 最常見的路徑，
    // 任何欄位在 loadDefinitionIntoUi/collectDefinition 之間掉字都會在這裡現形
    void loadThenSaveRoundTripsDefinition()
    {
        UdlManager mgr;
        const UdlDefinition original = richDefinition(QStringLiteral("RoundTripLang"));
        QVERIFY(mgr.save(original));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        ui.picker->setCurrentIndex(1);
        ui.box->button(QDialogButtonBox::Save)->click();

        // save() 對同名是取代語意，因此仍應只有一筆
        QCOMPARE(int(mgr.definitions().size()), 1);
        UdlDefinition got;
        QVERIFY(takeDefinition(mgr, QStringLiteral("RoundTripLang"), got));

        QCOMPARE(got.extensions, original.extensions);
        for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
            QCOMPARE(got.keywordGroup(i), original.keywordGroup(i));
            QCOMPARE(got.keywordGroupPrefix(i), original.keywordGroupPrefix(i));
        }
        QCOMPARE(got.operators, original.operators);
        QCOMPARE(int(got.delimiters.size()), int(original.delimiters.size()));
        for (int i = 0; i < int(got.delimiters.size()); ++i) {
            QCOMPARE(got.delimiters.at(i).open, original.delimiters.at(i).open);
            QCOMPARE(got.delimiters.at(i).escape, original.delimiters.at(i).escape);
            QCOMPARE(got.delimiters.at(i).close, original.delimiters.at(i).close);
            QCOMPARE(got.delimiters.at(i).nesting, original.delimiters.at(i).nesting);
        }
        QCOMPARE(got.blockCommentNesting, original.blockCommentNesting);
        QCOMPARE(got.lineCommentNesting, original.lineCommentNesting);
        QCOMPARE(got.stringNesting, original.stringNesting);
        QCOMPARE(got.folderTokens.open, original.folderTokens.open);
        QCOMPARE(got.folderTokens.middle, original.folderTokens.middle);
        QCOMPARE(got.folderTokens.close, original.folderTokens.close);
        QCOMPARE(got.lineComment, original.lineComment);
        QCOMPARE(got.blockCommentStart, original.blockCommentStart);
        QCOMPARE(got.blockCommentEnd, original.blockCommentEnd);
        QCOMPARE(got.caseSensitive, original.caseSensitive);
        QCOMPARE(int(got.styles.size()), 1);
        QCOMPARE(got.styles.value(UdlLexer::Comment).fg, QStringLiteral("#ff0000"));
        QCOMPARE(got.styles.value(UdlLexer::Comment).bg, QStringLiteral("#00ff00"));
        QVERIFY(got.styles.value(UdlLexer::Comment).bold);
        QVERIFY(got.styles.value(UdlLexer::Comment).underline);
        QVERIFY(!got.styles.value(UdlLexer::Comment).italic);
    }

    // 載入 B 語言後再載入 A 語言，A 沒設定的欄位必須被清乾淨，
    // 不能殘留 B 的關鍵字/樣式（表單重用最容易出的 bug）
    void loadingAnotherLanguageClearsPreviousValues()
    {
        UdlManager mgr;
        QVERIFY(mgr.save(richDefinition(QStringLiteral("AaaPlain"))));  // 先存，排在選單前面
        UdlDefinition plain;
        plain.name = QStringLiteral("ZzzPlain");
        plain.extensions = {"zzz"};
        plain.keywords = {"only"};
        QVERIFY(mgr.save(plain));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        ui.picker->setCurrentIndex(1);  // AaaPlain（欄位豐富）
        QVERIFY(!ui.keywordGroups.at(2)->toPlainText().isEmpty());
        QVERIFY(ui.styleRows.at(9).bold->isChecked());

        ui.picker->setCurrentIndex(2);  // ZzzPlain（幾乎空白）
        QCOMPARE(ui.name->text(), QStringLiteral("ZzzPlain"));
        QCOMPARE(ui.keywordGroups.at(0)->toPlainText(), QStringLiteral("only"));
        QVERIFY(ui.keywordGroups.at(2)->toPlainText().isEmpty());
        QVERIFY(!ui.keywordPrefix.at(2)->isChecked());
        QVERIFY(ui.operators->toPlainText().isEmpty());
        QVERIFY(ui.delimiters->toPlainText().isEmpty());
        QVERIFY(ui.blockNesting->text().isEmpty());
        QVERIFY(ui.folderOpen->text().isEmpty());
        QVERIFY(ui.caseSensitive->isChecked());   // plain 用預設值 true
        QVERIFY(!ui.styleRows.at(9).bold->isChecked());
        QCOMPARE(ui.styleRows.at(9).fg->text(), QStringLiteral("(default)"));
        QVERIFY(ui.styleRows.at(9).fg->styleSheet().isEmpty());
    }

    // 分隔符欄位的容錯：開頭符號為空的行要整行忽略，只填一欄時 escape/close 視為空
    void delimiterLinesWithoutOpenTokenAreIgnored()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        ui.name->setText(QStringLiteral("DelimEdgeLang"));
        ui.delimiters->setPlainText(QStringLiteral("|esc|close\n@\n$|^|$"));
        ui.box->button(QDialogButtonBox::Save)->click();

        UdlDefinition got;
        QVERIFY(takeDefinition(mgr, QStringLiteral("DelimEdgeLang"), got));
        QCOMPARE(int(got.delimiters.size()), 2);
        QCOMPARE(got.delimiters.at(0).open, QStringLiteral("@"));
        QVERIFY(got.delimiters.at(0).escape.isEmpty());
        QVERIFY(got.delimiters.at(0).close.isEmpty());
        QCOMPARE(got.delimiters.at(1).open, QStringLiteral("$"));
        QCOMPARE(got.delimiters.at(1).escape, QStringLiteral("^"));
        QCOMPARE(got.delimiters.at(1).close, QStringLiteral("$"));
    }

    // 全部 14 列樣式都勾一個屬性 → 全部都要被寫回，且 styleId 對應正確
    void everyStyleRowMapsToItsStyleId()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        ui.name->setText(QStringLiteral("AllStylesLang"));
        for (int i = 0; i < kStyleRowCount; ++i)
            ui.styleRows.at(i).underline->setChecked(true);
        ui.box->button(QDialogButtonBox::Save)->click();

        UdlDefinition got;
        QVERIFY(takeDefinition(mgr, QStringLiteral("AllStylesLang"), got));
        QCOMPARE(int(got.styles.size()), kStyleRowCount);
        for (int id : kStyleRowIds) {
            QVERIFY2(got.styles.contains(id), qPrintable(QStringLiteral("缺少 styleId %1").arg(id)));
            QVERIFY(got.styles.value(id).underline);
        }
    }

    // 新語言存檔後，選單要即時多出這一項（否則使用者得重開對話框才看得到）
    void savedLanguageIsPersistedForNextDialog()
    {
        UdlManager mgr;
        {
            UdlEditorDialog dlg(&mgr);
            EditorUi ui;
            collectUi(&dlg, ui);
            ui.name->setText(QStringLiteral("SecondDialogLang"));
            ui.extensions->setText(QStringLiteral("sdl"));
            ui.box->button(QDialogButtonBox::Save)->click();
        }

        UdlEditorDialog dlg2(&mgr);
        EditorUi ui2;
        collectUi(&dlg2, ui2);
        QCOMPARE(ui2.picker->count(), 2);
        QCOMPARE(ui2.picker->itemText(1), QStringLiteral("SecondDialogLang"));
        ui2.picker->setCurrentIndex(1);
        QCOMPARE(ui2.extensions->text(), QStringLiteral("sdl"));
    }

    // 選單停在「(New Language)」時，Rename/Remove 必須是 no-op：
    // 沒有選中任何語言就不該跳出輸入框或確認框，更不該動到 manager
    void renameAndRemoveDoNothingWithoutSelection()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "UntouchedLang";
        d.extensions = {"utl"};
        QVERIFY(mgr.save(d));

        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);
        QCOMPARE(ui.picker->currentIndex(), 0);
        ui.renameBtn->click();
        ui.removeBtn->click();

        QCOMPARE(int(mgr.definitions().size()), 1);
        QCOMPARE(mgr.definitions().at(0).name, QStringLiteral("UntouchedLang"));
    }

    // Dock/Undock：切換視窗旗標與按鈕文字/勾選狀態，且必須可以來回切
    void dockToggleFlipsWindowFlagAndLabel()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        // Qt::SubWindow 是「視窗型別」欄位（多個位元），因此用 testFlag 檢查整組位元，
        // 不能用單一 & 判斷——Qt::Dialog 與 Qt::SubWindow 有共用位元，會誤判為已停駐。
        QVERIFY(!dlg.windowFlags().testFlag(Qt::SubWindow));
        ui.dockBtn->click();
        QCOMPARE(ui.dockBtn->text(), QStringLiteral("Undock"));
        QVERIFY(ui.dockBtn->isChecked());
        QVERIFY(dlg.windowFlags().testFlag(Qt::SubWindow));

        ui.dockBtn->click();
        QCOMPARE(ui.dockBtn->text(), QStringLiteral("Dock"));
        QVERIFY(!ui.dockBtn->isChecked());
        QVERIFY(!dlg.windowFlags().testFlag(Qt::SubWindow));
        QVERIFY(dlg.windowFlags().testFlag(Qt::Dialog));
    }

    // 已顯示的對話框切換 dock 後不能消失——setWindowFlags 會隱藏視窗，
    // 程式必須重新 show()（這是實作裡特別處理過的一段）
    void dockToggleKeepsVisibleDialogVisible()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        dlg.show();
        QVERIFY(dlg.isVisible());
        ui.dockBtn->click();
        QVERIFY(dlg.isVisible());
        ui.dockBtn->click();
        QVERIFY(dlg.isVisible());
        dlg.close();
    }

    // 透明度滑桿：範圍限制在 20~100%，避免使用者把視窗調到完全看不見
    void transparencySliderHasSafeRange()
    {
        UdlManager mgr;
        UdlEditorDialog dlg(&mgr);
        EditorUi ui;
        collectUi(&dlg, ui);

        QCOMPARE(ui.transparency->minimum(), 20);
        QCOMPARE(ui.transparency->maximum(), 100);
        QCOMPARE(ui.transparency->value(), 100);
        ui.transparency->setValue(0);           // 低於下限應被夾住
        QCOMPARE(ui.transparency->value(), 20);
        ui.transparency->setValue(50);
        QCOMPARE(ui.transparency->value(), 50);
    }

    // manager 為 nullptr（尚未初始化）時對話框仍要能建構與操作，不得崩潰
    void nullManagerStillBuildsUsableDialog()
    {
        UdlEditorDialog dlg(nullptr);
        EditorUi ui;
        collectUi(&dlg, ui);

        QCOMPARE(ui.picker->count(), 1);
        QCOMPARE(ui.picker->itemText(0), QStringLiteral("(New Language)"));
        ui.renameBtn->click();   // 無 manager：直接 return
        ui.removeBtn->click();
        ui.dockBtn->click();
        QCOMPARE(ui.dockBtn->text(), QStringLiteral("Undock"));
    }
};

QTEST_MAIN(TestUdlEditorDialog)
#include "test_udleditordialog.moc"
