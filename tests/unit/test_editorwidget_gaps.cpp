// 單元測試：EditorWidget 覆蓋率缺口補強
//
// 本檔專攻 test_editor.cpp / test_editor_ops.cpp / test_textops.cpp 尚未觸及的路徑：
// 事件處理（鍵盤／滑鼠／滾輪／拖放／雙擊）、選取歷史 Undo/Redo、進階自動縮排、
// 呼叫提示多載切換、路徑自動完成、Paste Special 的 HTML/RTF 剝除、標籤配對邊界、
// 以及各式錯誤／早退分支。不重複既有測試已驗證的行為。
#include <QtTest>

#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QWheelEvent>

#include <Qsci/qsciscintillabase.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexerpython.h>

#include "core/EditorWidget.h"

using namespace macpad::core;

// EditorWidget 的事件處理器（keyPressEvent/wheelEvent/mousePressEvent/eventFilter）為 protected。
// Qt6 的 QApplication::notify 對拖放事件有特殊派送路徑，合成事件送不進 event filter
// （見 EditorWidget.h 的註解），故改以子類別把這些入口提升為 public 直接驅動，
// 讓測試不依賴平台的事件派送細節。
class ProbeEditor : public EditorWidget {
public:
    using EditorWidget::eventFilter;
    using EditorWidget::keyPressEvent;
    using EditorWidget::mousePressEvent;
    using EditorWidget::wheelEvent;
};

namespace {

// 合成一次「鍵入某字元」：QsciScintilla 依 event->text() 插入字元。
void typeChar(ProbeEditor &e, int key, const QString &text,
              Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    e.keyPressEvent(&ev);
}

}  // namespace

class TestEditorWidgetGaps : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // 測試模式：避免任何路徑寫入使用者真實的設定/快取目錄
        QStandardPaths::setTestModeEnabled(true);
    }

    // ── 純函式邊界 ─────────────────────────────────────────────────────────

    // 路徑片段限定 ASCII：遇到非 ASCII 字元必須停止向前掃描
    // （字元數＝UTF-8 位元組數的不變式，onUserListActivated 靠它決定刪除長度）
    void pathFragmentStopsAtNonAscii()
    {
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("中文abc"), 5),
                 QStringLiteral("abc"));
        // 全非 ASCII → 空片段
        QVERIFY(EditorWidget::pathFragmentBefore(QStringLiteral("中文"), 2).isEmpty());
        // pos 超出範圍會被夾限，不得越界
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("/tmp/x"), 999),
                 QStringLiteral("/tmp/x"));
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("/tmp/x"), -5),
                 QString());
    }

    // 自動閉合標籤的拒絕條件（既有測試只驗證正向補標籤）
    void closingTagRejectsIncompleteOrNonTags()
    {
        // '>' 不是游標前最後一字元 → 不補（標籤已經打完並繼續輸入內文）
        QVERIFY(EditorWidget::closingTagFor(QStringLiteral("<div>x")).isEmpty());
        // 完全沒有 '>' → 不補
        QVERIFY(EditorWidget::closingTagFor(QStringLiteral("<div")).isEmpty());
        // 空標籤 <> → 不補
        QVERIFY(EditorWidget::closingTagFor(QStringLiteral("<>")).isEmpty());
        // 名稱非以字母開頭 → 不補
        QVERIFY(EditorWidget::closingTagFor(QStringLiteral("<1st>")).isEmpty());
        // 沒有 '<' → 不補
        QVERIFY(EditorWidget::closingTagFor(QStringLiteral("plain text")).isEmpty());
        // 對照組：合法開啟標籤（含屬性與命名空間）才補
        QCOMPARE(EditorWidget::closingTagFor(QStringLiteral("<ns:tag a=\"1\">")),
                 QStringLiteral("</ns:tag>"));
    }

    // 標籤配對的掃描邊界：註解區塊、未閉合的 '<'、宣告節點、孤立閉合標籤
    void matchingTagScannerEdgeCases()
    {
        int os = 0, oe = 0, cs = 0, ce = 0;

        // <!-- --> 內部的 '<' 不得被視為標籤：註解裡的 <b> 不能與外面的 </b> 配對
        const QString withComment = QStringLiteral("<b>x<!--<b>-->y</b>");
        QVERIFY(EditorWidget::matchingTagRanges(withComment, 1, &os, &oe, &cs, &ce));
        QCOMPARE(os, 0);
        QCOMPARE(ce, withComment.size());   // 配對到最後的 </b>，而非註解內那個

        // 未閉合的 '<'（無 '>'）→ 掃描中止，其後不再產生標籤
        const QString unterminated = QStringLiteral("<a>text<b");
        QVERIFY(!EditorWidget::matchingTagRanges(unterminated, 1, &os, &oe, &cs, &ce));

        // 宣告節點 <!DOCTYPE …> / <?xml …?> 名稱為空 → 不配對
        QVERIFY(!EditorWidget::matchingTagRanges(QStringLiteral("<!DOCTYPE html><p>x</p>"),
                                                 2, &os, &oe, &cs, &ce));
        QVERIFY(!EditorWidget::matchingTagRanges(QStringLiteral("<?xml version=\"1.0\"?><p/>"),
                                                 2, &os, &oe, &cs, &ce));

        // 孤立閉合標籤（前面沒有對應開啟）→ 不配對
        QVERIFY(!EditorWidget::matchingTagRanges(QStringLiteral("text</p>more"),
                                                 6, &os, &oe, &cs, &ce));

        // 開啟標籤往後配對時需略過不同名稱的標籤（<a> 要跳過中間的 <b>/</b>）
        const QString nestedDiff = QStringLiteral("<a><b>t</b></a>");
        QVERIFY(EditorWidget::matchingTagRanges(nestedDiff, 1, &os, &oe, &cs, &ce));
        QCOMPARE(nestedDiff.mid(os, oe - os), QStringLiteral("<a>"));
        QCOMPARE(nestedDiff.mid(cs, ce - cs), QStringLiteral("</a>"));

        // 閉合標籤名稱前允許空白（</ b>），仍應正確配對回 <b>
        const QString spaced = QStringLiteral("<b>t</ b>");
        QVERIFY(EditorWidget::matchingTagRanges(spaced, spaced.indexOf(QStringLiteral("</ b>")) + 1,
                                                &os, &oe, &cs, &ce));
        QCOMPARE(os, 0);
        QCOMPARE(oe, 3);
    }

    // ── 檔案 I/O 的失敗與早退分支 ─────────────────────────────────────────

    void loadAndSaveFailurePaths()
    {
        EditorWidget e;
        QString err;
        // 開不了的來源 → 明確失敗並帶訊息（IL-4：不得靜默吞噬）
        QVERIFY(!e.loadFile(QStringLiteral("/no/such/dir/nope.txt"), &err));
        QVERIFY(!err.isEmpty());
        // 失敗不得污染既有狀態
        QVERIFY(e.isUntitled());

        err.clear();
        QVERIFY(!e.saveFile(QStringLiteral("/no/such/dir/nope.txt"), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(e.isUntitled());
    }

    // 換 lexer 時舊 lexer 必須被刪除（否則會掛在 this 下累積）——以連續載入不同語言驗證流程
    void reloadDifferentLanguageSwapsLexer()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString cpp = dir.filePath(QStringLiteral("a.cpp"));
        const QString py = dir.filePath(QStringLiteral("b.py"));
        {
            QFile f(cpp);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int main() { return 0; }\n");
        }
        {
            QFile f(py);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("def main():\n    pass\n");
        }

        EditorWidget e;
        QSignalSpy spy(&e, &EditorWidget::lexerChanged);
        QVERIFY(spy.isValid());

        QVERIFY(e.loadFile(cpp));
        QVERIFY(e.lexer() != nullptr);
        const QString first = QString::fromLatin1(e.lexer()->language());

        QVERIFY(e.loadFile(py));
        QVERIFY(e.lexer() != nullptr);
        const QString second = QString::fromLatin1(e.lexer()->language());
        QVERIFY2(first != second, qPrintable(first + QLatin1Char('/') + second));
        QVERIFY(spy.count() >= 2);
    }

    // 手動指定 lexer：連續指定時舊的必須換掉，並每次發出 lexerChanged
    void setLanguageLexerReplacesPrevious()
    {
        EditorWidget e;
        QSignalSpy spy(&e, &EditorWidget::lexerChanged);
        e.setLanguageLexer(new QsciLexerPython(&e));
        QCOMPARE(QString::fromLatin1(e.lexer()->language()), QStringLiteral("Python"));
        e.setLanguageLexer(new QsciLexerCSS(&e));
        QCOMPARE(QString::fromLatin1(e.lexer()->language()), QStringLiteral("CSS"));
        e.setLanguageLexer(nullptr);   // nullptr = 純文字
        QVERIFY(e.lexer() == nullptr);
        QCOMPARE(spy.count(), 3);
    }

    // 純中繼資料（編碼）變更後儲存 → dirtyChanged(false) 必須補發
    // （文字未曾修改，QScintilla 的 modificationChanged 不會自動發）
    void metaOnlyDirtyClearsOnSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        EditorWidget e;
        QSignalSpy spy(&e, &EditorWidget::dirtyChanged);
        QVERIFY(spy.isValid());

        e.setEncoding(Encoding::Utf16LE);
        QVERIFY(e.isDirty());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        // 同一編碼重複設定 → 無變化、不重複發訊號
        e.setEncoding(Encoding::Utf16LE);
        QCOMPARE(spy.count(), 0);

        QVERIFY(e.saveFile(dir.filePath(QStringLiteral("m.txt"))));
        QVERIFY(!e.isDirty());
        // 存檔後必須有 dirtyChanged 通知，且最後一次為 false（分頁的 ● 標記須消失）
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.takeLast().at(0).toBool(), false);
    }

    // 舊 Mac EOL（CR）模式（既有測試只涵蓋 LF/CRLF）
    void convertEolToClassicMacCr()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a\nb"));
        e.convertEol(Eol::Cr);
        QCOMPARE(e.eol(), Eol::Cr);
        QCOMPARE(e.eolMode(), QsciScintilla::EolMac);
        QVERIFY(e.text().contains(QLatin1Char('\r')));
        QVERIFY(!e.text().contains(QLatin1Char('\n')));
    }

    // 重新解讀編碼：未存檔僅設定目標編碼；檔案消失時必須明確失敗
    void reinterpretUntitledAndMissingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // 未存檔 → 僅設為存檔用 codec，不重讀
        EditorWidget untitled;
        untitled.setText(QStringLiteral("hello"));
        QVERIFY(untitled.reinterpretWithCodec(QStringLiteral("Big5")));
        QCOMPARE(untitled.saveCodec(), QStringLiteral("Big5"));
        QCOMPARE(untitled.text(), QStringLiteral("hello"));   // 內容不動

        // 不支援的 codec → 明確失敗（不得靜默回退 UTF-8）
        QString err;
        QVERIFY(!untitled.reinterpretWithCodec(QStringLiteral("no-such-codec-xyz"), &err));
        QVERIFY(!err.isEmpty());

        // 未存檔 + 內建 enum → 僅設為目標編碼
        EditorWidget untitled2;
        QVERIFY(untitled2.reinterpretAsEncoding(Encoding::Utf16BE));
        QCOMPARE(untitled2.encoding(), Encoding::Utf16BE);

        // 已存檔但檔案在背後被刪除 → 兩條重讀路徑都必須失敗且帶訊息
        const QString path = dir.filePath(QStringLiteral("gone.txt"));
        EditorWidget e;
        e.setText(QStringLiteral("bye"));
        QVERIFY(e.saveFile(path));
        QVERIFY(QFile::remove(path));

        err.clear();
        QVERIFY(!e.reinterpretWithCodec(QStringLiteral("Big5"), &err));
        QVERIFY(!err.isEmpty());
        err.clear();
        QVERIFY(!e.reinterpretAsEncoding(Encoding::Utf8, &err));
        QVERIFY(!err.isEmpty());
    }

    // ── 搜尋／取代的早退與旗標分支 ──────────────────────────────────────

    void replaceAllEdgeFlags()
    {
        EditorWidget e;
        e.setText(QStringLiteral("cat catalog CAT"));

        // 空樣式 → 不做事
        QCOMPARE(e.replaceAll(QString(), QStringLiteral("x"), false, true, false), 0);
        QCOMPARE(e.markAll(QString(), false, true, false), 0);
        QCOMPARE(e.countMatches(QString(), false, true, false), 0);

        // 整詞（非 regex）：catalog 不算
        QCOMPARE(e.replaceAll(QStringLiteral("cat"), QStringLiteral("dog"),
                              /*regex=*/false, /*caseSensitive=*/true, /*wholeWord=*/true), 1);
        QCOMPARE(e.text(), QStringLiteral("dog catalog CAT"));

        // 非法 regex → 回 0 且不改內容
        QCOMPARE(e.replaceAll(QStringLiteral("[unclosed"), QStringLiteral("x"),
                              true, true, false), 0);
        QCOMPARE(e.text(), QStringLiteral("dog catalog CAT"));
    }

    // dotAll：'.' 是否匹配換行（FR-047），需用 6 參數多載
    void replaceAllDotAllSpansNewline()
    {
        EditorWidget e;
        e.setText(QStringLiteral("<a>\nmid\n</a>"));

        // dotAll=false：'.' 不跨行 → 無匹配
        QCOMPARE(e.replaceAll(QStringLiteral("<a>.*</a>"), QStringLiteral("X"),
                              true, true, false, /*dotAll=*/false), 0);
        QCOMPARE(e.text(), QStringLiteral("<a>\nmid\n</a>"));

        // dotAll=true：跨行匹配整段
        QCOMPARE(e.replaceAll(QStringLiteral("<a>.*</a>"), QStringLiteral("X"),
                              true, true, false, /*dotAll=*/true), 1);
        QCOMPARE(e.text(), QStringLiteral("X"));
    }

    // ── 書籤 ────────────────────────────────────────────────────────────

    // 往前找書籤（含循環）——既有測試只驗證 nextBookmark
    void prevBookmarkWrapsAround()
    {
        EditorWidget e;
        e.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4"));
        e.setCursorPosition(1, 0);
        e.toggleBookmark();
        e.setCursorPosition(3, 0);
        e.toggleBookmark();

        int line = 0, index = 0;
        // 由第 4 行往前 → 第 3 行
        e.setCursorPosition(4, 0);
        e.prevBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 3);
        // 再往前 → 第 1 行
        e.prevBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);
        // 循環：由第 1 行再往前 → 繞回第 3 行
        e.prevBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 3);
    }

    // 點擊書籤邊欄（margin 1）切換書籤；其他邊欄不得反應
    void marginClickTogglesBookmark()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a\nb\nc"));
        QVERIFY(e.bookmarkedLines().isEmpty());

        emit e.marginClicked(1, 2, Qt::KeyboardModifiers());
        QCOMPARE(e.bookmarkedLines(), QList<int>{2});

        // 同一行再點一次 → 移除
        emit e.marginClicked(1, 2, Qt::KeyboardModifiers());
        QVERIFY(e.bookmarkedLines().isEmpty());

        // 行號邊欄（margin 0）不切換書籤
        emit e.marginClicked(0, 1, Qt::KeyboardModifiers());
        QVERIFY(e.bookmarkedLines().isEmpty());
    }

    // 無書籤時「以剪貼簿取代書籤行」必須是 no-op（不得動到內容或剪貼簿）
    void pasteReplaceWithoutBookmarksIsNoop()
    {
        EditorWidget e;
        e.setText(QStringLiteral("keep me"));
        QGuiApplication::clipboard()->setText(QStringLiteral("SHOULD NOT APPEAR"));
        e.pasteReplaceBookmarkedLines();
        QCOMPARE(e.text(), QStringLiteral("keep me"));
    }

    // ── 區塊註解：各語言的符號選擇 ───────────────────────────────────────

    void blockCommentPerLanguage()
    {
        // HTML → <!-- -->
        {
            EditorWidget e;
            e.setLanguageLexer(new QsciLexerHTML(&e));
            e.setText(QStringLiteral("<p>hi</p>"));
            e.setCursorPosition(0, 0);
            e.toggleBlockComment();
            QCOMPARE(e.text(), QStringLiteral("<!--<p>hi</p>-->"));
            // 再切換一次 → 還原
            e.toggleBlockComment();
            QCOMPARE(e.text(), QStringLiteral("<p>hi</p>"));
        }
        // CSS → /* */
        {
            EditorWidget e;
            e.setLanguageLexer(new QsciLexerCSS(&e));
            e.setText(QStringLiteral("a{b:c}"));
            e.setCursorPosition(0, 0);
            e.toggleBlockComment();
            QCOMPARE(e.text(), QStringLiteral("/*a{b:c}*/"));
        }
        // Python 無標準區塊註解 → 完全不動內容（交給行註解）
        {
            EditorWidget e;
            e.setLanguageLexer(new QsciLexerPython(&e));
            e.setText(QStringLiteral("x = 1"));
            e.setCursorPosition(0, 0);
            e.toggleBlockComment();
            QCOMPARE(e.text(), QStringLiteral("x = 1"));
        }
    }

    // ── Call tip ────────────────────────────────────────────────────────

    void callTipEmptyAndTriggerEdges()
    {
        EditorWidget e;
        // 空簽名 → 不顯示、不留下多載狀態
        e.showCallTip(QString());
        QCOMPARE(e.currentCallTipOverload(), -1);
        // 全空字串的多載清單 → 過濾後為空，同樣不顯示
        e.showCallTips({QString(), QString()});
        QCOMPARE(e.currentCallTipOverload(), -1);

        // 游標前是空白（無識別字）→ 不得發出 callTipRequested
        e.setText(QStringLiteral("foo "));
        e.setCursorPosition(0, 4);
        QSignalSpy spy(&e, &EditorWidget::callTipRequested);
        QVERIFY(spy.isValid());
        e.triggerCallTip();
        QCOMPARE(spy.count(), 0);

        // 游標前是識別字 → 發出並帶名稱
        e.setCursorPosition(0, 3);
        e.triggerCallTip();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("foo"));
    }

    // 多載切換：點擊「▲ n of m ▼」箭頭時循環切換（SCN_CALLTIPCLICK 1=上 2=下）
    void callTipOverloadArrowCycling()
    {
        EditorWidget e;
        // 註：這裡不需要斷開 QsciScintilla 內建的 handleCallTipClick——EditorWidget
        // 的建構子已經斷開它了（見下方 callTipClickDoesNotCrashWithoutOverloads）。
        e.setText(QStringLiteral("f"));
        e.setCursorPosition(0, 1);
        e.showCallTips({QStringLiteral("f(int)"), QStringLiteral("f(double)"),
                        QStringLiteral("f(char*)")});
        QCOMPARE(e.currentCallTipOverload(), 0);

        emit e.SCN_CALLTIPCLICK(2);          // 下一個
        QCOMPARE(e.currentCallTipOverload(), 1);
        emit e.SCN_CALLTIPCLICK(1);          // 上一個
        QCOMPARE(e.currentCallTipOverload(), 0);
        emit e.SCN_CALLTIPCLICK(1);          // 再往上 → 繞回最後一個
        QCOMPARE(e.currentCallTipOverload(), 2);
        emit e.SCN_CALLTIPCLICK(2);          // 往下 → 繞回第一個
        QCOMPARE(e.currentCallTipOverload(), 0);
        emit e.SCN_CALLTIPCLICK(0);          // 0 = 點在文字上，不切換
        QCOMPARE(e.currentCallTipOverload(), 0);

        // 關閉後索引失效，箭頭事件不得再改動狀態
        e.cancelCallTip();
        QCOMPARE(e.currentCallTipOverload(), -1);
        emit e.SCN_CALLTIPCLICK(2);
        QCOMPARE(e.currentCallTipOverload(), -1);
    }

    // 迴歸測試：點擊 call tip 不得讓程式當掉。
    //
    // QsciScintilla 在建構時就無條件把 SCN_CALLTIPCLICK 連到自己的
    // handleCallTipClick，而那個 slot 依賴「由 QScintilla 自己的 callTip() API
    // 建立」的內部狀態。EditorWidget 走的是原生 SCI_CALLTIPSHOW，那份狀態從未
    // 被建立——使用者只要點一下我們顯示的 call tip 就會 EXC_BAD_ACCESS。
    // 最容易踩到的是單一簽名的提示（沒有箭頭，但 Scintilla 對提示視窗內的任何
    // 點擊都會送出這個訊號，direction=0）。
    //
    // 建構子已改為關掉 QScintilla 那一套並斷開其 slot，讓 call tip 只有一個擁有者。
    // 本測試若崩潰即代表該防護被移除——注意它是「不崩潰」就算通過，
    // 因此崩潰會直接讓整個測試程序死掉，而不是報告一則失敗。
    void callTipClickDoesNotCrashWithoutOverloads()
    {
        // 完全沒顯示過提示
        {
            EditorWidget e;
            emit e.SCN_CALLTIPCLICK(0);
            emit e.SCN_CALLTIPCLICK(1);
            emit e.SCN_CALLTIPCLICK(2);
        }
        // 單一簽名（無多載箭頭）——實測中就是這個情境穩定崩潰
        {
            EditorWidget e;
            e.setText(QStringLiteral("foo("));
            e.setCursorPosition(0, 4);
            e.showCallTip(QStringLiteral("foo(int)"));
            emit e.SCN_CALLTIPCLICK(0);
            emit e.SCN_CALLTIPCLICK(1);
            emit e.SCN_CALLTIPCLICK(2);
            QCOMPARE(e.currentCallTipOverload(), -1);   // 單一簽名不參與多載切換
        }
        // 多載顯示後又關閉
        {
            EditorWidget e;
            e.showCallTips({QStringLiteral("a"), QStringLiteral("b")});
            e.cancelCallTip();
            for (int i = 0; i < 50; ++i) {
                emit e.SCN_CALLTIPCLICK(1);
                emit e.SCN_CALLTIPCLICK(2);
            }
        }
        QVERIFY(true);   // 走到這裡就代表沒崩潰
    }

    // QScintilla 自己那套 call tip 必須維持關閉，否則兩套機制會搶同一個提示視窗
    void nativeCallTipsAreDisabled()
    {
        EditorWidget e;
        QCOMPARE(e.callTipsStyle(), QsciScintilla::CallTipsNone);
    }

    // ── 選取歷史 Undo / Redo（Notepad++ v8.8.1 / v8.8.9）────────────────

    void selectionHistoryRecordsAndRestores()
    {
        EditorWidget e;
        e.setText(QStringLiteral("aaa\nbbb\nccc\nddd"));
        QVERIFY(!e.undoSelectionHistory());
        QCOMPARE(e.selectionHistoryDepth(), 0);

        e.setUndoSelectionHistory(true);
        e.setCursorPosition(0, 0);
        e.setCursorPosition(1, 1);
        e.setSelection(2, 0, 2, 3);          // 有選取的快照（anchor != caret）
        e.setCursorPosition(3, 2);
        const int depth = e.selectionHistoryDepth();
        QVERIFY2(depth >= 3, qPrintable(QString::number(depth)));

        // 重複記錄同一位置不得增加深度
        e.setCursorPosition(3, 2);
        QCOMPARE(e.selectionHistoryDepth(), depth);

        // Undo 先回退選取而不動文字
        const QString before = e.text();
        e.undoWithHistory();
        QCOMPARE(e.text(), before);
        QCOMPARE(e.selectionHistoryDepth(), depth - 1);
        QVERIFY(e.hasSelectedText());        // 回到「有選取」的那一筆快照
        QCOMPARE(e.selectedText(), QStringLiteral("ccc"));

        // 繼續回退 → 回到單純游標位置的快照
        e.undoWithHistory();
        QCOMPARE(e.selectionHistoryDepth(), depth - 2);
        int line = 0, idx = 0;
        e.getCursorPosition(&line, &idx);
        QCOMPARE(line, 1);
        QCOMPARE(idx, 1);

        // 文字一經修改，選取歷史立即重置（只在「上次修改之後」有意義）。
        // 重置後可能立刻再記下「修改後的游標位置」這一筆，故容許 0 或 1。
        e.setCursorPosition(0, 0);
        e.setCursorPosition(1, 0);
        e.setCursorPosition(2, 0);
        QVERIFY(e.selectionHistoryDepth() >= 3);
        e.insert(QStringLiteral("Z"));
        QVERIFY2(e.selectionHistoryDepth() <= 1,
                 qPrintable(QString::number(e.selectionHistoryDepth())));
    }

    // 選取歷史有上限（256 筆），長時間編輯不得無上限成長
    void selectionHistoryIsCapped()
    {
        EditorWidget e;
        QString doc;
        for (int i = 0; i < 400; ++i)
            doc += QStringLiteral("line%1\n").arg(i);
        e.setText(doc);
        e.setUndoSelectionHistory(true);
        for (int i = 0; i < 400; ++i)
            e.setCursorPosition(i, 0);
        QCOMPARE(e.selectionHistoryDepth(), 256);
    }

    // 關閉選取歷史時，undo/redo 直接作用於文字內容並保留視野
    void undoRedoWithHistoryDisabledActsOnText()
    {
        EditorWidget e;
        e.resize(600, 400);
        e.show();
        e.setText(QStringLiteral("base"));
        e.setCursorPosition(0, 4);
        e.insert(QStringLiteral("+more"));
        const QString modified = e.text();
        QVERIFY(modified.contains(QStringLiteral("more")));

        e.undoWithHistory();
        QCOMPARE(e.text(), QStringLiteral("base"));
        QCOMPARE(e.selectionHistoryDepth(), 0);

        e.redoWithHistory();
        QCOMPARE(e.text(), modified);
        QCOMPARE(e.selectionHistoryDepth(), 0);
    }

    // ── 縮放 ────────────────────────────────────────────────────────────

    void zoomHelpersEmitZoomChanged()
    {
        EditorWidget e;
        QSignalSpy spy(&e, &EditorWidget::zoomChanged);
        QVERIFY(spy.isValid());

        e.applyZoomTo(3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 3);

        e.applyZoomIn();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 4);

        e.applyZoomOut();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 3);
    }

    // Ctrl+滾輪縮放：由 Scintilla 內部處理，EditorWidget 只負責偵測變化並轉發
    void ctrlWheelReportsZoomChange()
    {
        ProbeEditor e;
        e.resize(400, 300);
        e.setText(QStringLiteral("wheel"));
        QSignalSpy spy(&e, &EditorWidget::zoomChanged);
        QVERIFY(spy.isValid());

        const QPointF pos(20, 20);
        // 無修飾鍵的滾動 → 純捲動，絕不可發出 zoomChanged
        QWheelEvent plain(pos, e.mapToGlobal(pos.toPoint()), QPoint(0, 0), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        e.wheelEvent(&plain);
        QCOMPARE(spy.count(), 0);

        // Ctrl+滾輪 → 縮放層級改變時轉發
        const int before = static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETZOOM));
        QWheelEvent zoom(pos, e.mapToGlobal(pos.toPoint()), QPoint(0, 0), QPoint(0, 120),
                         Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        e.wheelEvent(&zoom);
        const int after = static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETZOOM));
        // 訊號次數必須與實際縮放變化一致（有變才發、沒變不發）
        QCOMPARE(spy.count(), after != before ? 1 : 0);
    }

    // ── 滑鼠：停用「拖放選取文字」 ───────────────────────────────────────

    void disablingSelectionDragDropClearsSelectionOnPress()
    {
        ProbeEditor e;
        e.resize(600, 400);
        e.show();
        e.setText(QStringLiteral("hello world"));
        QVERIFY(e.selectionDragDropEnabled());   // 預設允許

        e.setSelectionDragDropEnabled(false);
        QVERIFY(!e.selectionDragDropEnabled());
        e.setSelection(0, 0, 0, 5);              // 選取 "hello"
        QVERIFY(e.hasSelectedText());

        // 在選取範圍內按下左鍵 → 應先取消選取（讓這次按下成為重新選取的起點）
        const long x = e.SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0UL, 2L);
        const long y = e.SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0UL, 2L);
        const QPointF local(static_cast<qreal>(x) + 1.0, static_cast<qreal>(y) + 1.0);
        QMouseEvent press(QEvent::MouseButtonPress, local, e.mapToGlobal(local.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        e.mousePressEvent(&press);
        QVERIFY(!e.hasSelectedText());
        QCOMPARE(e.text(), QStringLiteral("hello world"));   // 只動選取，不動內容
    }

    // ── 鍵盤：自動配對、自動閉合標籤、call tip 觸發、進階自動縮排 ──────

    void autoCloseBracketsAndQuotes()
    {
        ProbeEditor e;
        QVERIFY(e.autoClose());

        // 開括號 → 補上閉括號且游標留在中間
        typeChar(e, Qt::Key_ParenLeft, QStringLiteral("("));
        QCOMPARE(e.text(), QStringLiteral("()"));
        int line = 0, idx = 0;
        e.getCursorPosition(&line, &idx);
        QCOMPARE(idx, 1);

        // 引號在行首（前無字元）→ 補對
        ProbeEditor q1;
        typeChar(q1, Qt::Key_QuoteDbl, QStringLiteral("\""));
        QCOMPARE(q1.text(), QStringLiteral("\"\""));

        // 引號緊接在字詞之後（如 don't 的縮寫）→ 不補對
        ProbeEditor q2;
        q2.setText(QStringLiteral("don"));
        q2.setCursorPosition(0, 3);
        typeChar(q2, Qt::Key_Apostrophe, QStringLiteral("'"));
        QCOMPARE(q2.text(), QStringLiteral("don'"));

        // 關閉自動配對後不再補
        ProbeEditor off;
        off.setAutoClose(false);
        QVERIFY(!off.autoClose());
        typeChar(off, Qt::Key_BracketLeft, QStringLiteral("["));
        QCOMPARE(off.text(), QStringLiteral("["));
    }

    void autoCloseHtmlTagOnGreaterThan()
    {
        ProbeEditor e;
        e.setText(QStringLiteral("<div"));
        e.setCursorPosition(0, 4);
        typeChar(e, Qt::Key_Greater, QStringLiteral(">"));
        QCOMPARE(e.text(), QStringLiteral("<div></div>"));
        int line = 0, idx = 0;
        e.getCursorPosition(&line, &idx);
        QCOMPARE(idx, 5);   // 游標停在標籤之間

        // 自閉合標籤不補
        ProbeEditor s;
        s.setText(QStringLiteral("<br/"));
        s.setCursorPosition(0, 4);
        typeChar(s, Qt::Key_Greater, QStringLiteral(">"));
        QCOMPARE(s.text(), QStringLiteral("<br/>"));
    }

    void typingParenRequestsCallTip()
    {
        ProbeEditor e;
        QSignalSpy spy(&e, &EditorWidget::callTipRequested);
        QVERIFY(spy.isValid());

        e.setText(QStringLiteral("printf"));
        e.setCursorPosition(0, 6);
        typeChar(e, Qt::Key_ParenLeft, QStringLiteral("("));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("printf"));

        // 前面是空白（無識別字）→ 不發
        ProbeEditor blank;
        blank.setText(QStringLiteral("x "));
        blank.setCursorPosition(0, 2);
        QSignalSpy spy2(&blank, &EditorWidget::callTipRequested);
        typeChar(blank, Qt::Key_ParenLeft, QStringLiteral("("));
        QCOMPARE(spy2.count(), 0);

        // '(' 位於文件最前端 → 沒有可查詢的識別字
        ProbeEditor first;
        QSignalSpy spy3(&first, &EditorWidget::callTipRequested);
        typeChar(first, Qt::Key_ParenLeft, QStringLiteral("("));
        QCOMPARE(spy3.count(), 0);

        // 關閉 call tip 偏好後不再發出
        ProbeEditor disabled;
        disabled.setCallTipsEnabled(false);
        QVERIFY(!disabled.callTipsEnabled());
        disabled.setText(QStringLiteral("foo"));
        disabled.setCursorPosition(0, 3);
        QSignalSpy spy4(&disabled, &EditorWidget::callTipRequested);
        typeChar(disabled, Qt::Key_ParenLeft, QStringLiteral("("));
        QCOMPARE(spy4.count(), 0);
    }

    // 進階自動縮排：大括號語系於 `{` 之後、冒號語系於 `:` 之後各多縮一級
    void advancedAutoIndentByLanguage()
    {
        // C 家族：`{` 結尾 → 多縮一級
        {
            ProbeEditor e;
            e.setLanguageLexer(new QsciLexerCPP(&e));
            QVERIFY(e.advancedAutoIndent());
            e.setText(QStringLiteral("void f() {"));
            e.setCursorPosition(0, 10);
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), e.tabWidth());
        }
        // C 家族：非 `{` 結尾 → 不追加
        {
            ProbeEditor e;
            e.setLanguageLexer(new QsciLexerCPP(&e));
            e.setText(QStringLiteral("int a = 1;"));
            e.setCursorPosition(0, 10);
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), 0);
        }
        // Python：`:` 結尾 → 多縮一級
        {
            ProbeEditor e;
            e.setLanguageLexer(new QsciLexerPython(&e));
            e.setText(QStringLiteral("if x:"));
            e.setCursorPosition(0, 5);
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), e.tabWidth());
        }
        // 無 lexer（純文字）→ 不做進階縮排
        {
            ProbeEditor e;
            e.setText(QStringLiteral("plain {"));
            e.setCursorPosition(0, 7);
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), 0);
        }
        // 空行按 Enter → 無前一行內容可判斷
        {
            ProbeEditor e;
            e.setLanguageLexer(new QsciLexerCPP(&e));
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), 0);
        }
        // 關閉進階自動縮排 → `{` 之後不再多縮
        {
            ProbeEditor e;
            e.setLanguageLexer(new QsciLexerCPP(&e));
            e.setAdvancedAutoIndent(false);
            QVERIFY(!e.advancedAutoIndent());
            e.setText(QStringLiteral("void f() {"));
            e.setCursorPosition(0, 10);
            typeChar(e, Qt::Key_Return, QStringLiteral("\n"));
            QCOMPARE(e.indentation(1), 0);
        }
    }

    // Ctrl+Alt+Space 於 base 處理前被攔截，不得插入任何字元
    void ctrlAltSpaceTriggersPathCompletionWithoutTyping()
    {
        ProbeEditor e;
        e.setText(QStringLiteral("abc"));
        e.setCursorPosition(0, 3);
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Space,
                     Qt::ControlModifier | Qt::AltModifier, QStringLiteral(" "));
        e.keyPressEvent(&ev);
        QVERIFY(ev.isAccepted());
        QCOMPARE(e.text(), QStringLiteral("abc"));   // 未插入空白
    }

    // ── 路徑自動完成 ────────────────────────────────────────────────────

    void pathCompletionListAndInsertion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char *name : {"alpha.txt", "album.txt"}) {
            QFile f(dir.filePath(QString::fromLatin1(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        EditorWidget e;
        // 空文件、游標在 0 → 無片段可補
        e.triggerPathCompletion();
        QCOMPARE(e.text(), QString());

        // 目錄不存在 → 無候選，不得插入任何內容
        e.setText(QStringLiteral("/definitely/not/here/zz"));
        e.setCursorPosition(0, e.lineLength(0));
        e.triggerPathCompletion();
        QCOMPARE(e.text(), QStringLiteral("/definitely/not/here/zz"));

        // 有候選：顯示清單並記住已輸入前綴長度，選取後以完整檔名取代前綴
        const QString typed = dir.path() + QStringLiteral("/al");
        e.setText(typed);
        e.setCursorPosition(0, static_cast<int>(typed.size()));
        e.triggerPathCompletion();
        emit e.userListActivated(1, QStringLiteral("alpha.txt"));
        QCOMPARE(e.text(), dir.path() + QStringLiteral("/alpha.txt"));

        // 其他清單 id 一律忽略（路徑補完只認自己的 id）
        const QString unchanged = e.text();
        emit e.userListActivated(99, QStringLiteral("IGNORED"));
        QCOMPARE(e.text(), unchanged);
    }

    // ── 自動完成來源（API / 文件字詞）────────────────────────────────────

    void apiCompletionsFallBackToLexerKeywords()
    {
        EditorWidget noLexer;
        noLexer.applyApiCompletions({QStringLiteral("foo")});   // 無 lexer → 安全跳過
        QCOMPARE(noLexer.autoCompletionSource(), QsciScintilla::AcsDocument);

        EditorWidget e;
        e.setLanguageLexer(new QsciLexerCPP(&e));
        e.applyApiCompletions({QStringLiteral("myApiCall"), QStringLiteral("myOther")});
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsAll);

        // 再次套用（entries 為空）→ 舊 QsciAPIs 必須被換掉，並改用 lexer 關鍵字表
        e.applyApiCompletions({});
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsAll);

        // 有 API 來源時，手動觸發走「文件字詞 + API」合併路徑
        e.setText(QStringLiteral("myA"));
        e.setCursorPosition(0, 3);
        e.triggerWordCompletion();

        // 關閉字詞自動完成 → 來源改為 None；重新開啟時因有 API 而回到 AcsAll
        e.setWordCompletionEnabled(false);
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsNone);
        e.setWordCompletionEnabled(true);
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsAll);
    }

    // ── 變更歷史（FR-057）───────────────────────────────────────────────

    void changeHistoryNavigation()
    {
        EditorWidget e;
        e.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4"));
        QVERIFY(!e.changeHistoryEnabled());

        // 未啟用時導覽為 no-op（優雅降級）
        e.setCursorPosition(0, 0);
        e.goToNextChange();
        e.goToPrevChange();
        int line = 0, idx = 0;
        e.getCursorPosition(&line, &idx);
        QCOMPARE(line, 0);

        e.setChangeHistoryEnabled(true);
        QVERIFY(e.changeHistoryEnabled());

        // 手動放置變更歷史 marker（23 = SC_MARKNUM_HISTORY_MODIFIED），
        // 讓導覽在不依賴 Scintilla build 是否支援變更歷史的情況下仍可驗證。
        e.markerDefine(QsciScintilla::Circle, 23);
        e.markerAdd(3, 23);

        e.setCursorPosition(0, 0);
        e.goToNextChange();
        e.getCursorPosition(&line, &idx);
        QCOMPARE(line, 3);

        e.setCursorPosition(4, 0);
        e.goToPrevChange();
        e.getCursorPosition(&line, &idx);
        QCOMPARE(line, 3);

        // 沒有更多 marker 時游標不動
        e.setCursorPosition(4, 0);
        e.goToNextChange();
        e.getCursorPosition(&line, &idx);
        QCOMPARE(line, 4);

        e.setChangeHistoryEnabled(false);
        QVERIFY(!e.changeHistoryEnabled());
        QCOMPARE(e.marginWidth(2), 0);
    }

    // ── 多重選取指令 ────────────────────────────────────────────────────

    void multiSelectCommands()
    {
        // 游標兩側都是非字詞字元（字詞起訖相同）→ 無可選取目標，不得產生額外選取
        EditorWidget blank;
        blank.setText(QStringLiteral("aa  bb"));
        blank.setCursorPosition(0, 3);
        blank.selectAllOccurrences();
        QCOMPARE(static_cast<int>(blank.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), 1);
        QVERIFY(!blank.hasSelectedText());

        EditorWidget e;
        e.setText(QStringLiteral("aa bb aa cc aa"));

        // 游標落在 "aa" → 全選所有出現處
        e.setCursorPosition(0, 0);
        e.selectAllOccurrences();
        const int n = static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS));
        QCOMPARE(n, 3);

        // 略過最後加入者並改選下一個（多重選取狀態下先丟棄再加選）→ 不得歸零
        e.skipAndSelectNext();
        QVERIFY(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS) >= 1);

        // 重新全選後，驗證「丟棄最近一次加入的選取區域」逐次遞減
        e.setCursorPosition(0, 0);
        e.selectAllOccurrences();
        QCOMPARE(static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), n);
        e.undoLastMultiSelect();
        QCOMPARE(static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), n - 1);
        e.undoLastMultiSelect();
        QCOMPARE(static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), 1);
        // 只剩一個時不得再丟（否則會失去主選取）
        e.undoLastMultiSelect();
        QCOMPARE(static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), 1);

        e.selectNextOccurrence();
        QVERIFY(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS) >= 1);
    }

    // 多重選取下的遮蔽：所有範圍都要被遮罩（由後往前處理，位置不得錯位）
    void redactHandlesMultipleSelections()
    {
        EditorWidget e;
        e.setText(QStringLiteral("aa bb aa"));

        // 無選取 → no-op
        e.setCursorPosition(0, 2);
        e.redactSelection();
        QCOMPARE(e.text(), QStringLiteral("aa bb aa"));

        e.setCursorPosition(0, 0);
        e.selectAllOccurrences();
        QCOMPARE(static_cast<int>(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS)), 2);
        e.redactSelection();
        QCOMPARE(e.text(), QStringLiteral("●● bb ●●"));
    }

    // ── 兩段式欄位選取 ──────────────────────────────────────────────────

    void endColumnSelectWithoutAnchorIsNoop()
    {
        EditorWidget e;
        e.setText(QStringLiteral("abc\ndef"));
        e.setCursorPosition(1, 2);
        e.endColumnSelect();          // 未 beginColumnSelect → no-op
        QVERIFY(!e.hasSelectedText());
        e.endSelect();                // 同樣 no-op
        QVERIFY(!e.hasSelectedText());
    }

    // ── 智慧高亮 / 標籤配對高亮 / 詞彙上色 ──────────────────────────────

    void tagMatchHighlightFollowsCaret()
    {
        EditorWidget e;
        e.setText(QStringLiteral("<a><b>text</b></a>"));
        QVERIFY(!e.highlightMatchingTags());

        e.setCursorPosition(0, 4);        // 游標在 <b> 內
        e.setHighlightMatchingTags(true);
        QVERIFY(e.highlightMatchingTags());
        QCOMPARE(e.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 2);  // <b> 與 </b>

        // 游標移到純文字處（不在任何標籤內）→ 標記清空。
        // 無頭環境不會產生 Scintilla 的 SCN_UPDATEUI，cursorPositionChanged 不會自行送出，
        // 故明確補送一次，模擬實際 UI 上移動游標所觸發的重標流程。
        e.setCursorPosition(0, 8);
        emit e.cursorPositionChanged(0, 8);
        QCOMPARE(e.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 0);

        // 游標移回標籤內 → 重新標記配對
        e.setCursorPosition(0, 1);
        emit e.cursorPositionChanged(0, 1);
        QCOMPARE(e.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 2);   // <a> 與 </a>

        // 自閉合標籤只標示自身（開啟/閉合範圍相同，僅填一次）
        EditorWidget selfClose;
        selfClose.setText(QStringLiteral("x<br/>y"));
        selfClose.setCursorPosition(0, 3);
        selfClose.setHighlightMatchingTags(true);
        QCOMPARE(selfClose.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 1);

        // 關閉開關 → 既有標記清除
        selfClose.setHighlightMatchingTags(false);
        QCOMPARE(selfClose.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 0);

        // 純文字文件開啟時不得留下任何標記
        EditorWidget plain;
        plain.setText(QStringLiteral("no tags here"));
        plain.setCursorPosition(0, 3);
        plain.setHighlightMatchingTags(true);
        QCOMPARE(plain.indicatorRangeCount(EditorWidget::kTagMatchIndicator), 0);
    }

    // 游標不在字詞內時，詞彙上色不得標記任何範圍
    void styleTokenWithoutWordIsNoop()
    {
        EditorWidget e;
        e.setText(QStringLiteral("aa  bb"));
        e.setCursorPosition(0, 3);        // 兩個空白之間
        e.styleTokenOccurrences(0);
        QCOMPARE(e.indicatorRangeCount(EditorWidget::kTokenIndicatorBase), 0);
    }

    // ── viewport 事件過濾：拖放開檔與 Ctrl/⌘+雙擊 ──────────────────────

    void viewportDragAndDropFilter()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("dropme.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("content");
        }

        ProbeEditor e;
        e.resize(300, 200);
        QSignalSpy spy(&e, &EditorWidget::filesDropped);
        QVERIFY(spy.isValid());

        QMimeData fileMime;
        fileMime.setUrls({QUrl::fromLocalFile(path)});
        const QPoint p(10, 10);

        // DragEnter / DragMove：檔案拖放必須被接受（否則游標顯示為「禁止放置」）
        QDragEnterEvent enter(p, Qt::CopyAction, &fileMime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(e.eventFilter(e.viewport(), &enter));
        QVERIFY(enter.isAccepted());

        QDragMoveEvent move(p, Qt::CopyAction, &fileMime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(e.eventFilter(e.viewport(), &move));

        // Drop：吃掉事件並轉發路徑
        QDropEvent drop(QPointF(p), Qt::CopyAction, &fileMime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(e.eventFilter(e.viewport(), &drop));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toStringList().first(),
                 QFileInfo(path).absoluteFilePath());

        // 純文字拖放：一律交還 Scintilla 的原生文字拖曳，不得攔截、不得發訊號
        QMimeData textMime;
        textMime.setText(QStringLiteral("just text"));
        QDragEnterEvent tEnter(p, Qt::CopyAction, &textMime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(!e.eventFilter(e.viewport(), &tEnter));
        QDropEvent tDrop(QPointF(p), Qt::CopyAction, &textMime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(!e.eventFilter(e.viewport(), &tDrop));
        QCOMPARE(spy.count(), 0);
    }

    void ctrlDoubleClickSelectsWholeWord()
    {
        ProbeEditor e;
        e.resize(600, 400);
        e.show();
        // delimiter 覆寫下 '-' 可能斷字；Ctrl/⌘+雙擊須以預設字元集選「整個字」
        e.setText(QStringLiteral("alpha snake_case_word beta"));
        QVERIFY(e.ctrlDoubleClickWholeWord());

        const long target = 12;   // 落在 snake_case_word 中間
        const long x = e.SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0UL, target);
        const long y = e.SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0UL, target);
        const QPointF local(static_cast<qreal>(x) + 1.0, static_cast<qreal>(y) + 1.0);

        QMouseEvent dbl(QEvent::MouseButtonDblClick, local, e.mapToGlobal(local.toPoint()),
                        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QVERIFY(e.eventFilter(e.viewport(), &dbl));
        // 以低階 SCI_SETSELECTION 設定，故查詢 Scintilla 的選取範圍（非 QsciScintilla 快取）
        const QString content = e.text();
        const long expectedStart = content.indexOf(QStringLiteral("snake_case_word"));
        QCOMPARE(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART), expectedStart);
        QCOMPARE(e.SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND),
                 expectedStart + static_cast<long>(QStringLiteral("snake_case_word").size()));

        // 未按修飾鍵 → 不攔截，交回 Scintilla 預設雙擊
        QMouseEvent plain(QEvent::MouseButtonDblClick, local, e.mapToGlobal(local.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(!e.eventFilter(e.viewport(), &plain));

        // 關閉偏好 → 即使按住修飾鍵也不攔截
        e.setCtrlDoubleClickWholeWord(false);
        QVERIFY(!e.ctrlDoubleClickWholeWord());
        QMouseEvent off(QEvent::MouseButtonDblClick, local, e.mapToGlobal(local.toPoint()),
                        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QVERIFY(!e.eventFilter(e.viewport(), &off));
    }

    // ── 摺疊邊界樣式 ────────────────────────────────────────────────────

    void foldMarginStyles()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a\nb"));
        // 逐一套用 persistence::FoldMarginStyle 的五種序位（0..4）與越界值
        for (int style : {0, 1, 2, 3, 4, 99, -1})
            e.setFoldMarginStyle(style);
        // None 之後折疊邊欄應無寬度；Box 之後應恢復
        e.setFoldMarginStyle(0);
        QCOMPARE(e.marginWidth(2), 0);
        e.setFoldMarginStyle(4);
        QVERIFY(e.marginWidth(2) > 0);
    }

    // ── Paste Special：HTML / RTF 剝除 ──────────────────────────────────

    void pasteAsHtmlStripsMarkup()
    {
        EditorWidget e;

        // 剪貼簿無任何負載 → no-op
        QGuiApplication::clipboard()->clear();
        e.pasteAsHtml();
        QCOMPARE(e.text(), QString());

        // 有 HTML 負載：移除 script 區塊、<br> 轉換行、標籤剝除、實體還原
        auto *md = new QMimeData;
        md->setHtml(QStringLiteral(
            "<p>Hello <b>World</b></p><script>bad()</script>&amp;X<br/>Y"));
        QGuiApplication::clipboard()->setMimeData(md);
        e.pasteAsHtml();
        QCOMPARE(e.text(), QStringLiteral("Hello World&X\nY"));

        // HTML 剝除後為空字串 → 不插入任何內容
        auto *empty = new QMimeData;
        empty->setHtml(QStringLiteral("<p></p>"));
        QGuiApplication::clipboard()->setMimeData(empty);
        e.pasteAsHtml();
        QCOMPARE(e.text(), QStringLiteral("Hello World&X\nY"));

        // 無 HTML 只有純文字 → 退回一般貼上
        EditorWidget plain;
        QGuiApplication::clipboard()->setText(QStringLiteral("plain payload"));
        plain.pasteAsHtml();
        QCOMPARE(plain.text(), QStringLiteral("plain payload"));
    }

    void pasteAsRtfStripsControlWords()
    {
        EditorWidget e;

        QGuiApplication::clipboard()->clear();
        e.pasteAsRtf();
        QCOMPARE(e.text(), QString());

        // 控制詞（\rtf1）、帶數字參數與尾隨空白（\deff0 ）、十六進位跳脫（\'e9）、
        // 控制符號（\~）、群組括號與換行都必須被剝除
        auto *md = new QMimeData;
        md->setData(QStringLiteral("text/rtf"),
                    QByteArray("{\\rtf1\\ansi\\deff0 Caf\\'e9\\~x\r\nend}"));
        QGuiApplication::clipboard()->setMimeData(md);
        e.pasteAsRtf();
        QCOMPARE(e.text(), QString::fromUtf8("Caféxend"));

        // application/rtf 亦視為 RTF 負載
        EditorWidget alt;
        auto *md2 = new QMimeData;
        md2->setData(QStringLiteral("application/rtf"), QByteArray("{\\rtf1 alt}"));
        QGuiApplication::clipboard()->setMimeData(md2);
        alt.pasteAsRtf();
        QCOMPARE(alt.text(), QStringLiteral("alt"));

        // 剝除後為空 → 不插入
        EditorWidget blank;
        auto *md3 = new QMimeData;
        md3->setData(QStringLiteral("text/rtf"), QByteArray("{\\rtf1}"));
        QGuiApplication::clipboard()->setMimeData(md3);
        blank.pasteAsRtf();
        QCOMPARE(blank.text(), QString());

        // 無 RTF 只有純文字 → 退回一般貼上
        EditorWidget fallback;
        QGuiApplication::clipboard()->setText(QStringLiteral("txt only"));
        fallback.pasteAsRtf();
        QCOMPARE(fallback.text(), QStringLiteral("txt only"));
    }
};

QTEST_MAIN(TestEditorWidgetGaps)
#include "test_editorwidget_gaps.moc"
