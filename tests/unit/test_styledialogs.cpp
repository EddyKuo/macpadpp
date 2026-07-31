// 單元測試：樣式相關對話框 —— StyleConfiguratorDialog / ThemePickerDialog / ColorPicker
//
// 這三個檔都是「UI 殼」，但殼裡包著真正的行為：
//   * StyleConfiguratorDialog 是 styles.json 的唯一編輯入口，
//     使用者在上面的每一次點選都會落到 StyleSettings 的某個欄位，按下 OK 才寫檔。
//   * ThemePickerDialog 是 ThemeStore 的管理介面（列出/套用/刪除）。
//   * ColorPicker 負責把 QColorDialog 的 16 格自訂色跨 session 保存。
// 所以這裡測的不是「widget 有沒有被建出來」，而是「操作 → 狀態 → 落檔」這條鏈。
//
// 兩個刻意的限制（測試在 QT_QPA_PLATFORM=offscreen 下無人值守執行）：
//   1. 不讓任何 modal 對話框停在畫面上等人。取色器（QColorDialog）與錯誤訊息
//      （QMessageBox）一律不觸發，只驗證那些路徑的「提早返回」守衛（無選取、
//      Global Styles 模式…）。唯一的例外是主題 Import/Export 的檔案對話框——
//      不驅動它就完全測不到那兩條路徑，故以計時器在它出現的瞬間按下取消，
//      並加上「原生對話框關閉 + 逾時強制收尾」兩道保險（見 armFileDialogCanceller）。
//   2. 私有 slot 一律用 QMetaObject::invokeMethod 以名稱呼叫，而不是把按鈕點下去——
//      按鈕在停用狀態下 click() 不會有反應，那樣就測不到守衛條件了。
#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>

#include <Qsci/qscilexer.h>

#include <memory>
#include <utility>

#include "core/LexerFactory.h"
#include "persistence/AppPaths.h"
#include "persistence/SettingsStore.h"
#include "persistence/StyleStore.h"
#include "persistence/ThemeStore.h"
#include "ui/ColorPicker.h"
#include "ui/StyleConfiguratorDialog.h"
#include "ui/ThemePickerDialog.h"

using namespace macpad::persistence;
using macpad::ui::ColorPicker;
using macpad::ui::StyleConfiguratorDialog;
using macpad::ui::ThemePickerDialog;

namespace {

// 只取直屬子 widget：QSpinBox/QFontComboBox/QComboBox 內部也有 QLineEdit 與 QComboBox，
// 遞迴的 findChild 會撈到那些內部元件而測錯對象。
// 另外注意 Qt 會在 addWidget 時把 widget 重新指派給 layout 所屬的 widget，
// 所以放在 QGroupBox 內的控制項並不是對話框的直屬子物件——下面以群組框為單位定位。
template <typename T>
QList<T *> directChildren(const QWidget *w)
{
    QList<T *> out;
    const QList<T *> all = w->findChildren<T *>();
    for (T *c : all)
        if (c->parent() == w)
            out << c;
    return out;
}

QGroupBox *groupBox(const QWidget *dlg, const QString &title)
{
    const QList<QGroupBox *> boxes = dlg->findChildren<QGroupBox *>();
    for (QGroupBox *b : boxes)
        if (b->title() == title)
            return b;
    return nullptr;
}

// 色塊是唯一開了 autoFillBackground 的 QLabel（paintSwatch() 設的），
// 以此把「色塊」與「說明文字」兩種 QLabel 分開。
QList<QLabel *> swatchesIn(const QWidget *w)
{
    QList<QLabel *> out;
    const QList<QLabel *> labels = directChildren<QLabel>(w);
    for (QLabel *l : labels)
        if (l->autoFillBackground())
            out << l;
    return out;
}

QColor swatchColor(const QLabel *l)
{
    return l->palette().color(QPalette::Window);
}

// --- Global Override 群組框（單一 fg/bg 套用到所有語言）---
QCheckBox *globalEnable(const QWidget *dlg, int i)   // 0=前景 1=背景
{
    return directChildren<QCheckBox>(groupBox(dlg, QStringLiteral("Global Override"))).value(i);
}
QPushButton *globalPickBtn(const QWidget *dlg, int i)
{
    return directChildren<QPushButton>(groupBox(dlg, QStringLiteral("Global Override"))).value(i);
}
QLabel *globalSwatch(const QWidget *dlg, int i)
{
    return swatchesIn(groupBox(dlg, QStringLiteral("Global Override"))).value(i);
}

// --- Select Theme 群組框 ---
QComboBox *themeCombo(const QWidget *dlg)
{
    return directChildren<QComboBox>(groupBox(dlg, QStringLiteral("Select Theme"))).value(0);
}
QPushButton *applyThemeBtn(const QWidget *dlg)
{
    return directChildren<QPushButton>(groupBox(dlg, QStringLiteral("Select Theme"))).value(0);
}

// --- 對話框主體（不在任何群組框內）---
// 直屬子物件中：QComboBox 只有語言下拉、QLineEdit 只有 User ext.、
// QPushButton 只有前景/背景取色鈕（OK/Cancel 的 parent 是 QDialogButtonBox）。
QComboBox *languageCombo(const QWidget *dlg) { return directChildren<QComboBox>(dlg).value(0); }
QLineEdit *userExtEdit(const QWidget *dlg) { return directChildren<QLineEdit>(dlg).value(0); }

enum StyleButton { FgBtn = 0, BgBtn = 1 };
QPushButton *styleButton(const QWidget *dlg, StyleButton which)
{
    return directChildren<QPushButton>(dlg).value(int(which));
}

enum StyleCheck { BoldBox = 0, ItalicBox = 1, UnderlineBox = 2 };
QCheckBox *styleCheck(const QWidget *dlg, StyleCheck which)
{
    return directChildren<QCheckBox>(dlg).value(int(which));
}

// 樣式前景/背景色塊（Global Override 的兩個色塊在群組框內，不會混進來）
QLabel *styleSwatch(const QWidget *dlg, int i) { return swatchesIn(dlg).value(i); }

// styles.json 中 C/C++ 的鍵是 lexer 的 language()（"C++"），不是選單鍵 "cpp"。
// 硬寫字串會讓測試綁死 QScintilla 的實作，這裡改為向 LexerFactory 現場問。
QString cppLanguageName()
{
    QsciLexer *lex = macpad::core::LexerFactory::createForLanguage(QStringLiteral("cpp"), nullptr);
    const QString name = lex ? QString::fromLatin1(lex->language()) : QString();
    delete lex;
    return name;
}

void selectLanguage(QWidget *dlg, const QString &key)
{
    QComboBox *lang = languageCombo(dlg);
    const int idx = lang->findData(key);
    QVERIFY2(idx >= 0, qPrintable(QStringLiteral("語言下拉找不到 %1").arg(key)));
    lang->setCurrentIndex(idx);
}

// Import/Export 會開啟 QFileDialog。測試不能停在那裡等人操作，但這兩條路徑的
// 「使用者按取消」分支本身是有意義的（取消後不得動到任何主題檔），值得測。
// 作法：在呼叫前掛一支計時器，一偵測到檔案對話框就直接 Rejected 關掉——
// 只做「取消」，不做選檔，因此不會觸發匯入/匯出，也不會有 QMessageBox。
// 兩道保險確保絕不卡住：
//   1. initTestCase 設 AA_DontUseNativeDialogs，保證出現的是 Qt 自己的 widget 對話框
//      （原生 macOS 面板不是 QWidget，關不掉）；
//   2. 等待逾時（2 秒）就 closeAllWindows() 強制收尾，並讓 sawDialog 維持 false 使測試失敗。
QTimer *armFileDialogCanceller(QObject *ctx, bool *sawDialog)
{
    auto *timer = new QTimer(ctx);
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, ctx, [timer, sawDialog, ticks]() {
        if (auto *fd = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            *sawDialog = true;
            timer->stop();
            fd->reject();   // QDialog::reject 是公開 slot；done() 在 QFileDialog 是 protected
            return;
        }
        if (++(*ticks) > 200) {
            timer->stop();
            QApplication::closeAllWindows();
        }
    });
    timer->start(10);
    return timer;
}

Theme makeTheme(const QString &name, bool dark)
{
    Theme t;
    t.name = name;
    t.dark = dark;
    t.styles.global.editorBg = dark ? QStringLiteral("#202020") : QStringLiteral("#FFFFFF");
    return t;
}

}  // namespace

class TestStyleDialogs : public QObject {
    Q_OBJECT

    static void clearThemes()
    {
        QDir dir(AppPaths::configDir() + QStringLiteral("/themes"));
        if (!dir.exists())
            return;
        const QStringList files = dir.entryList(QDir::Files);
        for (const QString &f : files)
            dir.remove(f);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // 檔案對話框一律走 Qt 自繪版本：原生面板在無人值守的測試環境中關不掉
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    // 每個測試都從乾淨的設定檔開始：這三個類別全都以檔案為單一真相來源，
    // 殘留檔案會讓「載入時反映既有設定」這類斷言變成偽陽性。
    void init()
    {
        QFile::remove(AppPaths::filePath(QStringLiteral("styles.json")));
        QFile::remove(AppPaths::filePath(QStringLiteral("settings.json")));
        clearThemes();
    }

    void cleanup() { clearThemes(); }

    // ---------------------------------------------------------------- ColorPicker

    // 自訂色格的內容要能寫進 settings.json，且寫的是全部 16 格（QColorDialog 的固定格數），
    // 否則重啟後只會回來一半。
    void colorPickerPersistsAllCustomSlots()
    {
        const int n = QColorDialog::customCount();
        QVERIFY(n > 0);
        QColorDialog::setCustomColor(0, QColor(QStringLiteral("#123456")));
        QColorDialog::setCustomColor(1, QColor(QStringLiteral("#abcdef")));

        ColorPicker::persistCustomColors();

        const QStringList saved = SettingsStore::load().customColors;
        QCOMPARE(saved.size(), n);
        QCOMPARE(QColor(saved.at(0)), QColor(QStringLiteral("#123456")));
        QCOMPARE(QColor(saved.at(1)), QColor(QStringLiteral("#abcdef")));
        // 以 #RRGGBB（HexRgb）格式儲存，不帶 alpha——設定檔要能被人讀懂也能被 QColor 吃回去
        QCOMPARE(saved.at(0).size(), 7);
        QVERIFY(saved.at(0).startsWith(QLatin1Char('#')));
    }

    // 沒變就不該回寫設定檔：每開一次取色器就重寫 settings.json，會把使用者其他設定
    // 的寫入時機弄得無法預期。以「檔案裡的未知鍵是否倖存」判定——SettingsStore::save()
    // 是整份重建 JSON，一旦真的存檔，未知鍵必然消失。
    void colorPickerSkipsWriteWhenUnchanged()
    {
        QColorDialog::setCustomColor(0, QColor(QStringLiteral("#0f0f0f")));

        QStringList current;
        for (int i = 0; i < QColorDialog::customCount(); ++i)
            current << QColorDialog::customColor(i).name(QColor::HexRgb);

        QJsonArray colors;
        for (const QString &c : std::as_const(current))
            colors.append(c);
        QJsonObject o;
        o.insert(QStringLiteral("custom_colors"), colors);
        o.insert(QStringLiteral("zz_unknown_sentinel"), 42);
        QFile f(AppPaths::filePath(QStringLiteral("settings.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(o).toJson());
        f.close();

        ColorPicker::persistCustomColors();   // 內容相同 → 應提早返回，不寫檔

        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QVERIFY2(after.contains(QStringLiteral("zz_unknown_sentinel")),
                 "自訂色未變更時仍重寫了 settings.json");

        // 反向確認：真的改了顏色就必須寫檔（sentinel 隨整份重建而消失）
        QColorDialog::setCustomColor(0, QColor(QStringLiteral("#010203")));
        ColorPicker::persistCustomColors();
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject after2 = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QVERIFY(!after2.contains(QStringLiteral("zz_unknown_sentinel")));
        QCOMPARE(QColor(SettingsStore::load().customColors.value(0)),
                 QColor(QStringLiteral("#010203")));
    }

    // 啟動時把設定檔的自訂色填回色格；壞掉的字串要被跳過而不是塞成黑色，
    // 也不能因為清單比 16 短就越界。
    void colorPickerRestoresSavedColorsAndSkipsInvalid()
    {
        for (int i = 0; i < QColorDialog::customCount(); ++i)
            QColorDialog::setCustomColor(i, QColor(Qt::white));
        const QColor slot2Before = QColorDialog::customColor(2);

        Settings s;
        s.customColors = QStringList{QStringLiteral("#ff0000"), QStringLiteral("#00ff00"),
                                     QStringLiteral("definitely-not-a-color")};
        QVERIFY(SettingsStore::save(s));

        ColorPicker::restoreCustomColors();

        QCOMPARE(QColorDialog::customColor(0), QColor(QStringLiteral("#ff0000")));
        QCOMPARE(QColorDialog::customColor(1), QColor(QStringLiteral("#00ff00")));
        QCOMPARE(QColorDialog::customColor(2), slot2Before);   // 無效值 → 保持原樣
    }

    // 清單比色格多時只取前 16 格（qMin），不得越界寫入
    void colorPickerRestoreClampsToSlotCount()
    {
        Settings s;
        for (int i = 0; i < QColorDialog::customCount() + 5; ++i)
            s.customColors << QStringLiteral("#0000ff");
        QVERIFY(SettingsStore::save(s));

        ColorPicker::restoreCustomColors();   // 不得崩潰

        QCOMPARE(QColorDialog::customColor(QColorDialog::customCount() - 1),
                 QColor(QStringLiteral("#0000ff")));
    }

    // ------------------------------------------------------ StyleConfiguratorDialog

    // 開窗即載入 styles.json：字型大小、Global override 開關與色塊都要反映既有設定，
    // 而不是每次都從空白開始（那等於使用者的設定沒被讀取）。
    void styleDialogLoadsExistingSettings()
    {
        StyleSettings seed;
        seed.fontSize = 13;
        seed.global.enableGlobalFg = true;
        seed.global.globalFg = QStringLiteral("#ff8800");
        seed.global.globalBg = QStringLiteral("#112233");
        QVERIFY(StyleStore::save(seed));

        StyleConfiguratorDialog dlg;
        QCOMPARE(dlg.findChild<QSpinBox *>()->value(), 13);

        QCOMPARE(globalEnable(&dlg, 0)->isChecked(), true);
        QCOMPARE(globalEnable(&dlg, 1)->isChecked(), false);
        // 啟用者的取色鈕才可按——沒啟用還能改色，會讓使用者以為改了卻無效
        QCOMPARE(globalPickBtn(&dlg, 0)->isEnabled(), true);
        QCOMPARE(globalPickBtn(&dlg, 1)->isEnabled(), false);

        QCOMPARE(swatchesIn(groupBox(&dlg, QStringLiteral("Global Override"))).size(), 2);
        QCOMPARE(swatchColor(globalSwatch(&dlg, 0)), QColor(QStringLiteral("#ff8800")));
        QCOMPARE(swatchColor(globalSwatch(&dlg, 1)), QColor(QStringLiteral("#112233")));
        QCOMPARE(globalSwatch(&dlg, 0)->text(), QStringLiteral("#FF8800"));
    }

    // 預設狀態（無 styles.json）：色塊無色時顯示破折號而非假的黑色
    void styleDialogShowsDashForUnsetGlobalOverride()
    {
        StyleConfiguratorDialog dlg;
        QCOMPARE(globalSwatch(&dlg, 0)->text(), QStringLiteral("—"));
        QCOMPARE(globalSwatch(&dlg, 1)->text(), QStringLiteral("—"));
        QVERIFY(!globalPickBtn(&dlg, 0)->isEnabled());
        QVERIFY(!globalPickBtn(&dlg, 1)->isEnabled());
    }

    // 預設選中的是 Global Styles：該模式一列只有一個顏色，
    // 背景色/粗體/斜體/底線/副檔名/關鍵字都必須停用，否則使用者會以為能設而其實不會被寫入。
    void globalStylesModeDisablesPerStyleControls()
    {
        StyleConfiguratorDialog dlg;
        QCOMPARE(languageCombo(&dlg)->currentIndex(), 0);
        QCOMPARE(languageCombo(&dlg)->currentData().toString(),
                 QStringLiteral("__global_styles__"));

        auto *list = dlg.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 18);          // Global Styles 的 18 個項目
        QCOMPARE(list->currentRow(), 0);      // 自動選第一列

        QVERIFY(!styleButton(&dlg, BgBtn)->isEnabled());
        QVERIFY(!styleCheck(&dlg, BoldBox)->isEnabled());
        QVERIFY(!styleCheck(&dlg, ItalicBox)->isEnabled());
        QVERIFY(!styleCheck(&dlg, UnderlineBox)->isEnabled());
        QVERIFY(!userExtEdit(&dlg)->isEnabled());
        QVERIFY(!dlg.findChild<QPlainTextEdit *>()->isEnabled());
        // 前景取色鈕仍可用——Global Styles 就是靠它改色
        QVERIFY(styleButton(&dlg, FgBtn)->isEnabled());
    }

    // 切換 Global Styles 的列，前景色塊要換成該欄位的值（列 ↔ GlobalStyles 欄位的對應是
    // 寫死在 cpp 的 member-pointer 表，順序錯掉就會改到別的欄位）。背景色塊恆為無色。
    void globalStylesRowSelectionShowsMatchingField()
    {
        StyleSettings seed;
        seed.global.indentGuide = QStringLiteral("#404040");     // 第 0 列
        seed.global.caretLineBg = QStringLiteral("#2a2c2f");     // 第 1 列
        seed.global.urlHovered = QStringLiteral("#00aaff");      // 最後一列
        QVERIFY(StyleStore::save(seed));

        StyleConfiguratorDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        QCOMPARE(swatchesIn(&dlg).size(), 2);
        QLabel *fg = styleSwatch(&dlg, 0);
        QLabel *bg = styleSwatch(&dlg, 1);

        QCOMPARE(swatchColor(fg), QColor(QStringLiteral("#404040")));
        QCOMPARE(bg->text(), QStringLiteral("—"));   // 背景色塊在此模式恆為空

        list->setCurrentRow(1);
        QCOMPARE(swatchColor(fg), QColor(QStringLiteral("#2a2c2f")));

        list->setCurrentRow(list->count() - 1);
        QCOMPARE(swatchColor(fg), QColor(QStringLiteral("#00aaff")));

        // 未設定的欄位（例如第 2 列 selectionBg）顯示破折號
        list->setCurrentRow(2);
        QCOMPARE(fg->text(), QStringLiteral("—"));

        // 此模式下 bold/italic/underline 一律顯示為未勾選
        QVERIFY(!styleCheck(&dlg, BoldBox)->isChecked());
        QVERIFY(!styleCheck(&dlg, ItalicBox)->isChecked());
        QVERIFY(!styleCheck(&dlg, UnderlineBox)->isChecked());
    }

    // 選到具體語言：style 清單改由 lexer 的 description() 填出，
    // 每列都帶得回 style id（UserRole），且各控制項重新啟用。
    void selectingLanguagePopulatesStyleListFromLexer()
    {
        const QString lang = cppLanguageName();
        QVERIFY(!lang.isEmpty());

        StyleSettings seed;
        seed.userExtensions.insert(lang, QStringLiteral("foo bar"));
        QVERIFY(StyleStore::save(seed));

        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));

        auto *list = dlg.findChild<QListWidget *>();
        QVERIFY2(list->count() > 5, "C++ lexer 應提供多個具名 style");
        QCOMPARE(list->currentRow(), 0);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("—")));
        QVERIFY(list->item(0)->data(Qt::UserRole).isValid());

        QVERIFY(styleButton(&dlg, BgBtn)->isEnabled());
        QVERIFY(styleCheck(&dlg, BoldBox)->isEnabled());
        // 該語言既有的 User ext. 要被帶出來
        QCOMPARE(userExtEdit(&dlg)->text(), QStringLiteral("foo bar"));
    }

    // 已存在的覆寫要蓋掉 lexer 預設值顯示出來——否則使用者看到的是「改之前」的顏色。
    void existingOverrideDrivesSwatchesAndCheckboxes()
    {
        const QString lang = cppLanguageName();
        StyleConfiguratorDialog probe;              // 先取得第一列的 style id
        selectLanguage(&probe, QStringLiteral("cpp"));
        auto *probeList = probe.findChild<QListWidget *>();
        const int styleId = probeList->item(0)->data(Qt::UserRole).toInt();

        StyleOverride ov;
        ov.style = styleId;
        ov.fg = QStringLiteral("#ff0000");
        ov.bg = QStringLiteral("#00ff00");
        ov.bold = true;
        ov.italic = true;
        ov.underline = true;
        ov.keywords = QStringLiteral("alpha beta");
        StyleSettings seed;
        seed.byLang.insert(lang, {ov});
        QVERIFY(StyleStore::save(seed));

        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        QCOMPARE(swatchColor(styleSwatch(&dlg, 0)), QColor(QStringLiteral("#ff0000")));
        QCOMPARE(swatchColor(styleSwatch(&dlg, 1)), QColor(QStringLiteral("#00ff00")));
        QVERIFY(styleCheck(&dlg, BoldBox)->isChecked());
        QVERIFY(styleCheck(&dlg, ItalicBox)->isChecked());
        QVERIFY(styleCheck(&dlg, UnderlineBox)->isChecked());
        QCOMPARE(dlg.findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("alpha beta"));

        // 換到沒有覆寫的第二列，粗體等應回到 lexer 預設而非沿用上一列
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(1);
        QCOMPARE(dlg.findChild<QPlainTextEdit *>()->toPlainText(), QString());
    }

    // 勾選 Bold/Italic/Underline → 建立覆寫；按下 OK 才寫入 styles.json。
    void togglingFontFlagsCreatesOverrideAndApplySaves()
    {
        const QString lang = cppLanguageName();
        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        auto *list = dlg.findChild<QListWidget *>();
        const int styleId = list->item(0)->data(Qt::UserRole).toInt();

        styleCheck(&dlg, BoldBox)->setChecked(true);
        styleCheck(&dlg, UnderlineBox)->setChecked(true);

        // 尚未按 OK → 檔案不應有任何內容
        QVERIFY(StyleStore::load().byLang.isEmpty());

        QSpinBox *size = dlg.findChild<QSpinBox *>();
        size->setValue(16);
        const QString family = dlg.findChild<QFontComboBox *>()->currentFont().family();

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        QCOMPARE(dlg.result(), int(QDialog::Accepted));

        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.fontSize, 16);
        QCOMPARE(out.fontFamily, family);
        QVERIFY(out.byLang.contains(lang));
        const QVector<StyleOverride> list2 = out.byLang.value(lang);
        QCOMPARE(list2.size(), 1);
        QCOMPARE(list2[0].style, styleId);
        QCOMPARE(list2[0].bold, true);
        QCOMPARE(list2[0].italic, false);
        QCOMPARE(list2[0].underline, true);
    }

    // 同一個 style 反覆修改只能有一筆覆寫（currentOverride 應找到既有節點而非一直 append），
    // 不同 style 則各自一筆。
    void repeatedEditsReuseSameOverrideEntry()
    {
        const QString lang = cppLanguageName();
        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        auto *list = dlg.findChild<QListWidget *>();
        QCheckBox *bold = styleCheck(&dlg, BoldBox);

        bold->setChecked(true);
        bold->setChecked(false);
        bold->setChecked(true);
        list->setCurrentRow(1);
        styleCheck(&dlg, ItalicBox)->setChecked(true);

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        const QVector<StyleOverride> out = StyleStore::load().byLang.value(lang);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].style, list->item(0)->data(Qt::UserRole).toInt());
        QCOMPARE(out[0].bold, true);
        QCOMPARE(out[1].style, list->item(1)->data(Qt::UserRole).toInt());
        QCOMPARE(out[1].italic, true);
    }

    // 關鍵字覆寫：輸入即記錄（textChanged），清空也要寫回空字串（代表取消覆寫）。
    void editingKeywordsStoresPerStyleOverride()
    {
        const QString lang = cppLanguageName();
        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        auto *kw = dlg.findChild<QPlainTextEdit *>();
        kw->setPlainText(QStringLiteral("  foo bar baz  "));   // 前後空白應被 trim

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        const QVector<StyleOverride> out = StyleStore::load().byLang.value(lang);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].keywords, QStringLiteral("foo bar baz"));
    }

    // User ext.：填值寫入、清空移除（留一個空字串鍵會讓 LexerFactory 誤判有自訂副檔名）。
    void userExtensionsAreStoredAndClearedOnEmpty()
    {
        const QString lang = cppLanguageName();
        {
            StyleConfiguratorDialog dlg;
            selectLanguage(&dlg, QStringLiteral("cpp"));
            QLineEdit *ext = userExtEdit(&dlg);
            QVERIFY(ext);
            ext->setText(QStringLiteral("  myext other  "));
            QTest::keyClick(ext, Qt::Key_Return);      // 觸發 editingFinished
            QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        }
        QCOMPARE(StyleStore::load().userExtensions.value(lang), QStringLiteral("myext other"));

        {
            StyleConfiguratorDialog dlg;
            selectLanguage(&dlg, QStringLiteral("cpp"));
            QLineEdit *ext = userExtEdit(&dlg);
            QCOMPARE(ext->text(), QStringLiteral("myext other"));
            ext->clear();
            QTest::keyClick(ext, Qt::Key_Return);
            QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        }
        QVERIFY(!StyleStore::load().userExtensions.contains(lang));
    }

    // 切回 Global Styles 時，副檔名/關鍵字欄位要被清掉，而且清掉的動作
    // 不可反過來污染剛才那個語言的設定（QSignalBlocker 的用意）。
    void switchingBackToGlobalStylesClearsPerLanguageFields()
    {
        const QString lang = cppLanguageName();
        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        QLineEdit *ext = userExtEdit(&dlg);
        ext->setText(QStringLiteral("keepme"));
        QTest::keyClick(ext, Qt::Key_Return);
        dlg.findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("kw1 kw2"));

        languageCombo(&dlg)->setCurrentIndex(0);       // 回到 Global Styles
        QCOMPARE(ext->text(), QString());
        QCOMPARE(dlg.findChild<QPlainTextEdit *>()->toPlainText(), QString());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.userExtensions.value(lang), QStringLiteral("keepme"));
        QCOMPARE(out.byLang.value(lang).value(0).keywords, QStringLiteral("kw1 kw2"));
    }

    // Global override 的兩個勾選框控制取色鈕的可用性，且狀態要能存檔。
    void globalOverrideToggleUpdatesButtonsAndPersists()
    {
        StyleConfiguratorDialog dlg;
        QVERIFY(!globalPickBtn(&dlg, 0)->isEnabled());
        QVERIFY(!globalPickBtn(&dlg, 1)->isEnabled());

        globalEnable(&dlg, 0)->setChecked(true);
        QVERIFY(globalPickBtn(&dlg, 0)->isEnabled());
        QVERIFY(!globalPickBtn(&dlg, 1)->isEnabled());

        globalEnable(&dlg, 1)->setChecked(true);
        QVERIFY(globalPickBtn(&dlg, 1)->isEnabled());

        globalEnable(&dlg, 0)->setChecked(false);
        QVERIFY(!globalPickBtn(&dlg, 0)->isEnabled());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        const GlobalStyles g = StyleStore::load().global;
        QCOMPARE(g.enableGlobalFg, false);
        QCOMPARE(g.enableGlobalBg, true);
    }

    // Select Theme：下拉內容來自 ThemeStore，按下 Apply Theme 只發訊號（實際套用在 MainWindow）。
    void applyThemeEmitsSelectedThemeName()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Alpha"), true)));
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Beta"), false)));

        StyleConfiguratorDialog dlg;
        QComboBox *themes = themeCombo(&dlg);
        QCOMPARE(themes->count(), 2);
        QVERIFY(applyThemeBtn(&dlg)->isEnabled());

        QSignalSpy spy(&dlg, &StyleConfiguratorDialog::themeSelected);
        themes->setCurrentIndex(themes->findText(QStringLiteral("Beta")));
        applyThemeBtn(&dlg)->click();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Beta"));
        // 只是發訊號：對話框不該被 accept/reject，也不該動到 styles.json
        QCOMPARE(dlg.result(), 0);
        QVERIFY(!QFile::exists(AppPaths::filePath(QStringLiteral("styles.json"))));
    }

    // 沒有任何主題時 Apply Theme 要停用；即使強行呼叫也必須安全地什麼都不做。
    void applyThemeIsNoOpWithoutThemes()
    {
        StyleConfiguratorDialog dlg;
        QCOMPARE(themeCombo(&dlg)->count(), 0);
        QVERIFY(!applyThemeBtn(&dlg)->isEnabled());

        QSignalSpy spy(&dlg, &StyleConfiguratorDialog::themeSelected);
        applyThemeBtn(&dlg)->click();                       // 停用 → 不會觸發
        QVERIFY(QMetaObject::invokeMethod(&dlg, "applyThemeClicked"));  // 直接呼叫也要守住
        QCOMPARE(spy.count(), 0);
    }

    // 沒有選取任何 style 時，取色/關鍵字等操作必須提早返回。
    // 這裡刻意用 invokeMethod 而非點按鈕：若守衛失效就會彈出 modal 取色器把測試卡死，
    // 所以這條測試同時也是「不會誤開對話框」的保險。
    void noStyleSelectedGuardsAgainstColorPicking()
    {
        StyleConfiguratorDialog dlg;
        selectLanguage(&dlg, QStringLiteral("cpp"));
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(-1);
        QVERIFY(!list->currentItem());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "pickForeground"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "pickBackground"));
        // 沒有選取列時輸入關鍵字也不應建立任何覆寫
        dlg.findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("ignored"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onBoldItalicChanged"));

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        // 注意：查詢覆寫時會為該語言建立一個空的容器（QHash::operator[] 的副作用），
        // 所以「沒有任何修改」的判準是覆寫清單為空，而不是語言鍵不存在。
        QVERIFY(StyleStore::load().byLang.value(cppLanguageName()).isEmpty());
    }

    // Global Styles 模式下沒有「背景色」概念，pickBackground 必須直接返回
    // （按鈕雖已停用，但 slot 本身也要自我防衛）。粗體等亦不得產生覆寫。
    void globalStylesModeIgnoresBackgroundAndFontFlags()
    {
        StyleConfiguratorDialog dlg;
        QVERIFY(QMetaObject::invokeMethod(&dlg, "pickBackground"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onBoldItalicChanged"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onExtensionsEdited"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onKeywordsEdited"));

        QVERIFY(QMetaObject::invokeMethod(&dlg, "apply"));
        const StyleSettings out = StyleStore::load();
        QVERIFY(out.byLang.isEmpty());
        QVERIFY(out.userExtensions.isEmpty());
    }

    // 語言下拉的組成：第一項固定是 Global Styles，其餘來自 LexerFactory（Plain Text 除外）
    void languageComboContainsGlobalStylesPlusLexerLanguages()
    {
        StyleConfiguratorDialog dlg;
        QComboBox *lang = languageCombo(&dlg);
        QVERIFY(lang->count() > 1);
        QCOMPARE(lang->itemData(0).toString(), QStringLiteral("__global_styles__"));
        for (int i = 1; i < lang->count(); ++i)
            QVERIFY2(!lang->itemData(i).toString().isEmpty(),
                     "Plain Text（空鍵）不應出現在樣式設定的語言清單中");
        QVERIFY(lang->findData(QStringLiteral("cpp")) > 0);
    }

    // -------------------------------------------------------------- ThemePickerDialog

    // 清單來自 ThemeStore；未選取前 Apply/Export/Delete 一律停用（Import 永遠可用）。
    void themePickerListsThemesAndDisablesActionsWithoutSelection()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Aaa"), true)));
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Bbb"), false)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        QVERIFY(dlg.selectedTheme().isEmpty());

        const QList<QPushButton *> btns = directChildren<QPushButton>(&dlg);
        QCOMPARE(btns.size(), 4);   // Apply / Import / Export / Delete
        QVERIFY(!btns[0]->isEnabled());
        QVERIFY(btns[1]->isEnabled());   // Import 不需選取
        QVERIFY(!btns[2]->isEnabled());
        QVERIFY(!btns[3]->isEnabled());

        list->setCurrentRow(0);
        QVERIFY(btns[0]->isEnabled());
        QVERIFY(btns[2]->isEnabled());
        QVERIFY(btns[3]->isEnabled());
    }

    // Apply：記下選取名稱並 accept()，呼叫端才有東西可讀。
    void themePickerApplyRecordsSelectionAndAccepts()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Aaa"), true)));
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Bbb"), false)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(1);
        const QString expected = list->item(1)->text();

        directChildren<QPushButton>(&dlg).value(0)->click();

        QCOMPARE(dlg.selectedTheme(), expected);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QVERIFY(!dlg.isVisible());
    }

    // Delete：主題檔真的被刪掉，且清單即時刷新（不是只把列從畫面上拿掉）。
    void themePickerDeleteRemovesThemeAndRefreshes()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Keep"), true)));
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Doomed"), false)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        const QList<QListWidgetItem *> found = list->findItems(QStringLiteral("Doomed"),
                                                              Qt::MatchExactly);
        QCOMPARE(found.size(), 1);
        list->setCurrentItem(found.first());

        directChildren<QPushButton>(&dlg).value(3)->click();   // Delete

        QVERIFY(!ThemeStore::listThemes().contains(QStringLiteral("Doomed")));
        QVERIFY(ThemeStore::listThemes().contains(QStringLiteral("Keep")));
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Keep"));
        // 被刪掉的那筆不再是選取項 → 動作鈕重新停用
        QVERIFY(!directChildren<QPushButton>(&dlg).value(3)->isEnabled());
    }

    // 無選取時 Apply/Export/Delete 都必須提早返回。Export/Import 會開檔案對話框，
    // 因此只驗證守衛路徑——守衛若失效，這條測試會因彈出 modal 而卡住（即失敗訊號）。
    void themePickerActionsAreNoOpWithoutSelection()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Only"), true)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(0);
        list->setCurrentRow(-1);
        QVERIFY(!list->currentItem());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onExport"));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onDelete"));

        QVERIFY(dlg.selectedTheme().isEmpty());
        QCOMPARE(dlg.result(), int(QDialog::Rejected));
        QVERIFY(ThemeStore::listThemes().contains(QStringLiteral("Only")));
        QCOMPARE(list->count(), 1);
    }

    // Import：使用者在檔案對話框按取消 → 什麼都不該發生（清單不變、不跳錯誤訊息）。
    void themePickerImportCancelledChangesNothing()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Only"), true)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        bool saw = false;
        armFileDialogCanceller(&dlg, &saw);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onImport"));

        QVERIFY2(saw, "沒有等到檔案對話框出現（或它不是 Qt 自繪的 QFileDialog）");
        QCOMPARE(list->count(), 1);
        QCOMPARE(ThemeStore::listThemes(), QStringList{QStringLiteral("Only")});
    }

    // Export：同樣地，取消就不該產生任何檔案。
    void themePickerExportCancelledWritesNothing()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Only"), true)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(0);
        bool saw = false;
        armFileDialogCanceller(&dlg, &saw);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onExport"));

        QVERIFY2(saw, "沒有等到檔案對話框出現（或它不是 Qt 自繪的 QFileDialog）");
        QCOMPARE(ThemeStore::listThemes(), QStringList{QStringLiteral("Only")});
        QCOMPARE(dlg.result(), int(QDialog::Rejected));   // 取消匯出不應把主對話框關掉
    }

    // 空清單也要能開窗（首次啟動、使用者刪光主題），且不得誤判成有選取。
    void themePickerHandlesEmptyThemeList()
    {
        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        QCOMPARE(list->count(), 0);
        const QList<QPushButton *> btns = directChildren<QPushButton>(&dlg);
        QVERIFY(!btns[0]->isEnabled());
        QVERIFY(!btns[2]->isEnabled());
        QVERIFY(!btns[3]->isEnabled());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "refreshList"));
        QCOMPARE(list->count(), 0);
    }

    // refreshList 會盡量保留原本選取的項目：手動加入一筆主題後刷新，
    // 原本選中的主題仍要是選中狀態（否則使用者每次匯入後都得重新找回自己的主題）。
    void themePickerRefreshKeepsCurrentSelection()
    {
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Aaa"), true)));
        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Bbb"), false)));

        ThemePickerDialog dlg;
        auto *list = dlg.findChild<QListWidget *>();
        list->setCurrentRow(1);
        const QString before = list->currentItem()->text();

        QVERIFY(ThemeStore::save(makeTheme(QStringLiteral("Ccc"), true)));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "refreshList"));

        QCOMPARE(list->count(), 3);
        QVERIFY(list->currentItem());
        QCOMPARE(list->currentItem()->text(), before);
        QVERIFY(directChildren<QPushButton>(&dlg).value(0)->isEnabled());
    }
};

QTEST_MAIN(TestStyleDialogs)
#include "test_styledialogs.moc"
