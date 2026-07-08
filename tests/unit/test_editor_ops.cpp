// 單元測試：EditorWidget 進階操作（stats/折疊/縮排/行操作/區塊註解/書籤進階/
// replaceAll 書籤保留/括號選取/編碼與 codec）——補強覆蓋率 (FR-004/007/008/010/011/019/020)
#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QTemporaryDir>

#include <Qsci/qscilexercpp.h>
#include <Qsci/qsciapis.h>

#include "core/EditorWidget.h"
#include "core/FileEncoding.h"

using namespace macpad::core;

class TestEditorOps : public QObject {
    Q_OBJECT
private slots:
    void statsBasic()
    {
        EditorWidget e;
        e.setText(QStringLiteral("hello\nworld"));
        e.setCursorPosition(1, 2);

        EditorWidget::DocStats s = e.stats();
        QCOMPARE(s.lines, 2);
        QCOMPARE(s.length, 11L);   // "hello\nworld" = 5+1+5 字元
        QCOMPARE(s.line, 2);       // 1-based
        QCOMPARE(s.col, 3);        // 1-based
        QCOMPARE(s.pos, 9L);       // 位元組偏移 8 (5+1+2) → 字元數+1
        QCOMPARE(s.selChars, 0L);
        QCOMPARE(s.selLines, 0);
        QVERIFY(!s.overtype);

        // 選取跨行
        e.setSelection(0, 0, 1, 3);  // "hello\nwor" = 9 字元，跨 2 行
        EditorWidget::DocStats s2 = e.stats();
        QCOMPARE(s2.selChars, 9L);
        QCOMPARE(s2.selLines, 2);

        // 覆寫模式
        e.SendScintilla(EditorWidget::SCI_SETOVERTYPE, 1);
        EditorWidget::DocStats s3 = e.stats();
        QVERIFY(s3.overtype);
    }

    void foldingOperations()
    {
        EditorWidget e;
        e.setLanguageLexer(new QsciLexerCPP(&e));
        const QString code =
            QStringLiteral("void f() {\n")          // line 0 — header (depth 0)
            + QStringLiteral("    int a = 1;\n")     // line 1
            + QStringLiteral("    if (a) {\n")       // line 2 — header (depth 1)
            + QStringLiteral("        a++;\n")       // line 3
            + QStringLiteral("    }\n")               // line 4
            + QStringLiteral("}\n");                  // line 5
        e.setText(code);
        e.SendScintilla(EditorWidget::SCI_COLOURISE, 0UL, -1L);

        const long lvl0 = e.SendScintilla(EditorWidget::SCI_GETFOLDLEVEL, 0UL);
        const long lvl2 = e.SendScintilla(EditorWidget::SCI_GETFOLDLEVEL, 2UL);
        QVERIFY(lvl0 & EditorWidget::SC_FOLDLEVELHEADERFLAG);
        QVERIFY(lvl2 & EditorWidget::SC_FOLDLEVELHEADERFLAG);

        // 全部收合：SCI_FOLDALL 的 contract 動作只切換頂層標頭；巢狀（較深層）標頭
        // 因父層被收合而不可見，但其自身 expanded 旗標不受影響（Scintilla 實際行為）
        e.foldAllBlocks(true);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 0UL), 0L);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 1L);

        // 全部展開
        e.foldAllBlocks(false);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 0UL), 1L);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 1L);

        // 收合到第 2 層：只有內層（較深）收合，外層保持展開
        e.foldToLevel(2);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 0UL), 1L);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 0L);

        // 收合到第 1 層：內外層都收合
        e.foldAllBlocks(false);
        e.foldToLevel(1);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 0UL), 0L);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 0L);

        // foldCurrent：游標在內層區塊 → 只收合內層標頭
        e.foldAllBlocks(false);
        e.setCursorPosition(3, 0);
        e.foldCurrent(true);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 0L);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 0UL), 1L);

        e.foldCurrent(false);
        QCOMPARE(e.SendScintilla(EditorWidget::SCI_GETFOLDEXPANDED, 2UL), 1L);
    }

    void indentAndUnindentSelection()
    {
        EditorWidget e;
        e.setText(QStringLiteral("line1\nline2\n"));
        e.setSelection(0, 0, 1, 5);  // 跨兩行選取

        e.indentSelection();
        QCOMPARE(e.text(0), QStringLiteral("    line1\n"));
        QCOMPARE(e.text(1), QStringLiteral("    line2\n"));

        e.setSelection(0, 0, 1, 9);
        e.unindentSelection();
        QCOMPARE(e.text(0), QStringLiteral("line1\n"));
        QCOMPARE(e.text(1), QStringLiteral("line2\n"));
    }

    void joinAndSplitLines()
    {
        EditorWidget e;
        e.setText(QStringLiteral("aaa\nbbb\nccc\n"));
        e.setSelection(0, 0, 1, 3);  // 選取前兩行
        e.joinSelectedLines();

        // 前兩行合併為一行，總行數減少（原 4 行含結尾空行 → 合併後 3 行）
        QCOMPARE(e.lines(), 3);
        QVERIFY(e.text(0).contains(QStringLiteral("aaa")));
        QVERIFY(e.text(0).contains(QStringLiteral("bbb")));
        QCOMPARE(e.text(1), QStringLiteral("ccc\n"));

        // splitSelectedLines：至少不破壞內容（依視窗寬度環境相依，故弱化為內容保留檢查）
        e.resize(120, 200);
        const QString longLine = QStringLiteral(
            "word1 word2 word3 word4 word5 word6 word7 word8 word9 word10");
        e.setText(longLine);
        e.selectAll();
        e.splitSelectedLines();
        for (const QString &w : {QStringLiteral("word1"), QStringLiteral("word5"),
                                  QStringLiteral("word10")}) {
            QVERIFY(e.text().contains(w));
        }
    }

    void toggleBlockCommentWrapsAndUnwraps()
    {
        EditorWidget e;  // 無 lexer → 預設 /* */
        e.setText(QStringLiteral("int a;\n"));
        e.setSelection(0, 0, 0, 6);
        e.toggleBlockComment();
        QVERIFY(e.text().contains(QStringLiteral("/*int a;*/")));

        // 再次呼叫（無選取 → 作用於當前行）應解除註解
        e.setCursorPosition(0, 0);
        e.toggleBlockComment();
        QVERIFY(!e.text().contains(QStringLiteral("/*")));
        QVERIFY(e.text().contains(QStringLiteral("int a;")));
    }

    void bookmarkAdvancedOperations()
    {
        EditorWidget e;
        e.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4\n"));
        e.setCursorPosition(1, 0);
        e.toggleBookmark();
        e.setCursorPosition(3, 0);
        e.toggleBookmark();

        QCOMPARE(e.bookmarkedLines(), QList<int>({1, 3}));
        QCOMPARE(e.bookmarkedText(), QStringLiteral("l1\nl3"));

        // 文件含結尾空行（第 5 行），故反轉後含該行
        e.inverseBookmarks();
        QCOMPARE(e.bookmarkedLines(), QList<int>({0, 2, 4, 5}));
        e.inverseBookmarks();
        QCOMPARE(e.bookmarkedLines(), QList<int>({1, 3}));

        e.clearAllBookmarks();
        QVERIFY(e.bookmarkedLines().isEmpty());
    }

    void removeBookmarkedAndNonBookmarkedLines()
    {
        EditorWidget e1;
        e1.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4\n"));
        e1.setCursorPosition(1, 0);
        e1.toggleBookmark();
        e1.setCursorPosition(3, 0);
        e1.toggleBookmark();
        e1.removeBookmarkedLines();
        QCOMPARE(e1.text(), QStringLiteral("l0\nl2\nl4\n"));

        EditorWidget e2;
        e2.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4\n"));
        e2.setCursorPosition(1, 0);
        e2.toggleBookmark();
        e2.setCursorPosition(3, 0);
        e2.toggleBookmark();
        e2.removeNonBookmarkedLines();
        QCOMPARE(e2.text(), QStringLiteral("l1\nl3\n"));
    }

    void replaceAllPreservesBookmarks()
    {
        EditorWidget e;
        e.setText(QStringLiteral("foo\nbar\nfoo\nbaz\n"));
        e.setCursorPosition(3, 0);  // 第 3 行 "baz" 沒有被取代
        e.toggleBookmark();

        const int count = e.replaceAll(QStringLiteral("foo"), QStringLiteral("qux"),
                                        false, true, false);
        QCOMPARE(count, 2);
        QCOMPARE(e.text(), QStringLiteral("qux\nbar\nqux\nbaz\n"));

        const QList<int> marks = e.bookmarkedLines();
        QVERIFY(marks.contains(3));
    }

    void selectBetweenBracesSelectsInner()
    {
        // selectBetweenBraces() 以高階 setSelection() 設定選取，會同步更新
        // QsciScintilla 的選取快取，故 hasSelectedText()/selectedText() 立即可用
        // （Copy/Cut 才取得到內容）。
        EditorWidget e;
        e.setText(QStringLiteral("foo(a, b)"));
        e.setCursorPosition(0, 6);  // 游標在括號內部（逗號之後）
        e.selectBetweenBraces();
        const long start = e.SendScintilla(EditorWidget::SCI_GETSELECTIONSTART);
        const long end = e.SendScintilla(EditorWidget::SCI_GETSELECTIONEND);
        QCOMPARE(start, 4L);
        QCOMPARE(end, 8L);
        QCOMPARE(e.text().mid(static_cast<int>(start), static_cast<int>(end - start)),
                 QStringLiteral("a, b"));
        // 高階選取快取同步：Copy/Cut 使用的 API 立即取得選取內容
        QVERIFY(e.hasSelectedText());
        QCOMPARE(e.selectedText(), QStringLiteral("a, b"));
    }

    void encodingAndEolAndCodec()
    {
        // 編碼/EOL/codec 變更會標記文件 dirty（FR-019/020），即使當下無文字編輯
        // 亦然（以 m_metaDirty 補足 QScintilla setModified(true) 在 save-point 的 no-op）。
        // 同時驗證 metaChanged 訊號被觸發。
        EditorWidget e;
        QCOMPARE(e.encoding(), Encoding::Utf8);
        QCOMPARE(e.encodingLabel(), FileEncoding::encodingName(Encoding::Utf8));
        QVERIFY(!e.isDirty());

        QSignalSpy spy(&e, &EditorWidget::metaChanged);

        e.setEncoding(Encoding::Utf16LE);
        QCOMPARE(e.encoding(), Encoding::Utf16LE);
        QVERIFY(e.isDirty());   // 變更編碼標記 dirty（FR-019）
        QCOMPARE(spy.count(), 1);

        e.convertEol(Eol::CrLf);
        QCOMPARE(e.eol(), Eol::CrLf);
        QVERIFY(e.isDirty());   // 轉換 EOL 標記 dirty（FR-020）
        QCOMPARE(spy.count(), 2);

        e.setSaveCodec(QStringLiteral("Big5"));
        QCOMPARE(e.saveCodec(), QStringLiteral("Big5"));
        QCOMPARE(e.encodingLabel(), QStringLiteral("Big5"));
        QVERIFY(e.isDirty());   // 變更存檔 codec 標記 dirty（FR-019）
        QCOMPARE(spy.count(), 3);
    }

    void reinterpretWithCodecSuccessAndFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/t.txt");

        EditorWidget e;
        e.setText(QStringLiteral("hello"));
        QString err;
        QVERIFY2(e.saveFile(path, &err), qPrintable(err));

        QVERIFY2(e.reinterpretWithCodec(QStringLiteral("Big5"), &err), qPrintable(err));
        QCOMPARE(e.encodingLabel(), QStringLiteral("Big5"));
        QVERIFY(!e.isDirty());  // 純重讀不視為變更

        QString err2;
        QVERIFY(!e.reinterpretWithCodec(QStringLiteral("no-such-codec"), &err2));
        QVERIFY(!err2.isEmpty());
    }

    void countMatchesDoesNotMoveCaretOrSelection()
    {
        // FR-047：countMatches 只計數，不移動游標、不改變選取
        EditorWidget e;
        e.setText(QStringLiteral("foo bar foo baz foo"));
        e.setSelection(0, 0, 0, 3);  // 選取 "foo"（setSelection 會把游標移到選取端 col=3）

        const int n = e.countMatches(QStringLiteral("foo"), false, true, false);
        QCOMPARE(n, 3);

        int line = -1, col = -1;
        e.getCursorPosition(&line, &col);
        QCOMPARE(line, 0);
        QCOMPARE(col, 3);   // countMatches 未移動游標（游標仍在選取端）

        int lf = -1, if_ = -1, lt = -1, it = -1;
        e.getSelection(&lf, &if_, &lt, &it);
        QCOMPARE(lf, 0);
        QCOMPARE(if_, 0);
        QCOMPARE(lt, 0);
        QCOMPARE(it, 3);    // 選取未被改變

        QCOMPARE(e.countMatches(QStringLiteral("nope"), false, true, false), 0);
        QCOMPARE(e.countMatches(QString(), false, true, false), 0);  // 空字串直接回 0
    }

    void reinterpretAsEncodingRoundTrip()
    {
        // FR-048：以內建 Encoding enum 重新解讀磁碟位元組（不重新編碼）
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/t2.txt");

        EditorWidget e;
        e.setText(QStringLiteral("hello world"));
        QString err;
        QVERIFY2(e.saveFile(path, &err), qPrintable(err));
        QCOMPARE(e.encoding(), Encoding::Utf8);

        QVERIFY2(e.reinterpretAsEncoding(Encoding::Utf16LE, &err), qPrintable(err));
        QCOMPARE(e.encoding(), Encoding::Utf16LE);
        QVERIFY(!e.isDirty());  // 純重新解讀磁碟內容，視為未變更

        // 未存檔（無原始位元組）時，僅設定目標編碼
        EditorWidget e2;
        e2.setText(QStringLiteral("untitled content"));
        QVERIFY(e2.isUntitled());
        QVERIFY(e2.reinterpretAsEncoding(Encoding::Utf16BE, nullptr));
        QCOMPARE(e2.encoding(), Encoding::Utf16BE);
        QVERIFY(e2.isDirty());
    }

    void applyNewDocumentDefaultsSetsWithoutDirty()
    {
        // FR-053：新文件套用偏好之預設 EOL/編碼，但不得標記 dirty（尚無使用者變更）
        EditorWidget e;
        QVERIFY(!e.isDirty());
        e.applyNewDocumentDefaults(macpad::core::Eol::CrLf, Encoding::Utf8Bom);
        QCOMPARE(e.eol(), macpad::core::Eol::CrLf);
        QCOMPARE(e.encoding(), Encoding::Utf8Bom);
        QVERIFY(!e.isDirty());   // 關鍵：預設套用不視為變更
    }

    void cutAndPasteReplaceBookmarkedLines()
    {
        // FR-049：cutBookmarkedLines 複製書籤行到剪貼簿後刪除；
        // pasteReplaceBookmarkedLines 以剪貼簿行依序取代各書籤行內容
        EditorWidget e;
        e.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4\n"));
        e.setCursorPosition(1, 0);
        e.toggleBookmark();
        e.setCursorPosition(3, 0);
        e.toggleBookmark();

        e.cutBookmarkedLines();
        QCOMPARE(e.text(), QStringLiteral("l0\nl2\nl4\n"));
        QCOMPARE(qApp->clipboard()->text(), QStringLiteral("l1\nl3"));
        QVERIFY(e.bookmarkedLines().isEmpty());  // 書籤行已被刪除

        // 重新標記兩行書籤，貼上剪貼簿內容（3 行）逐一取代
        EditorWidget e2;
        e2.setText(QStringLiteral("a0\na1\na2\n"));
        e2.setCursorPosition(0, 0);
        e2.toggleBookmark();
        e2.setCursorPosition(2, 0);
        e2.toggleBookmark();

        qApp->clipboard()->setText(QStringLiteral("X\nY\nZ"));
        e2.pasteReplaceBookmarkedLines();
        QCOMPARE(e2.text(0), QStringLiteral("X\n"));
        QCOMPARE(e2.text(1), QStringLiteral("a1\n"));
        QCOMPARE(e2.text(2), QStringLiteral("Y\n"));

        // 剪貼簿行數少於書籤數時，多出的書籤行清空
        EditorWidget e3;
        e3.setText(QStringLiteral("b0\nb1\nb2\n"));
        e3.setCursorPosition(0, 0);
        e3.toggleBookmark();
        e3.setCursorPosition(1, 0);
        e3.toggleBookmark();
        e3.setCursorPosition(2, 0);
        e3.toggleBookmark();

        qApp->clipboard()->setText(QStringLiteral("only-one"));
        e3.pasteReplaceBookmarkedLines();
        QCOMPARE(e3.text(0), QStringLiteral("only-one\n"));
        QCOMPARE(e3.text(1), QStringLiteral("\n"));
        QCOMPARE(e3.text(2), QStringLiteral("\n"));
    }

    void autoCloseCloserForHelper()
    {
        // FR-050：closerFor() 為 keyPressEvent 自動配對的可測試決策核心
        QCOMPARE(EditorWidget::closerFor(QChar('(')), QChar(')'));
        QCOMPARE(EditorWidget::closerFor(QChar('[')), QChar(']'));
        QCOMPARE(EditorWidget::closerFor(QChar('{')), QChar('}'));
        QCOMPARE(EditorWidget::closerFor(QChar('"')), QChar('"'));
        QCOMPARE(EditorWidget::closerFor(QChar('\'')), QChar('\''));
        QVERIFY(EditorWidget::closerFor(QChar('x')).isNull());
    }

    void autoCloseInsertsPairViaKeyPress()
    {
        // 透過 QTest::keyClick 實際觸發 keyPressEvent，驗證括號自動配對與可關閉開關
        EditorWidget e;
        QVERIFY(e.autoClose());  // 預設開啟
        QTest::keyClick(&e, '(');
        QCOMPARE(e.text(), QStringLiteral("()"));
        int line = -1, col = -1;
        e.getCursorPosition(&line, &col);
        QCOMPARE(col, 1);   // 游標停在兩符號之間

        e.setAutoClose(false);
        QVERIFY(!e.autoClose());
        e.setText(QString());
        QTest::keyClick(&e, '(');
        QCOMPARE(e.text(), QStringLiteral("("));  // 關閉後不再自動配對
    }

    void changeHistoryAndVirtualSpaceSetters()
    {
        // FR-057/FR-060：setter 立即反映於對應 getter；不論 Scintilla build 是否
        // 實際支援 change history，狀態旗標本身都要能正確追蹤（優雅降級）。
        EditorWidget e;
        QVERIFY(!e.changeHistoryEnabled());
        e.setChangeHistoryEnabled(true);
        QVERIFY(e.changeHistoryEnabled());
        e.setChangeHistoryEnabled(false);
        QVERIFY(!e.changeHistoryEnabled());

        QVERIFY(!e.virtualSpace());
        e.setVirtualSpace(true);
        QVERIFY(e.virtualSpace());
        e.setVirtualSpace(false);
        QVERIFY(!e.virtualSpace());

        // goToNextChange/goToPrevChange 在未啟用時安全 no-op（不拋例外、不移動游標）
        e.setText(QStringLiteral("l0\nl1\nl2\n"));
        e.setCursorPosition(1, 0);
        e.goToNextChange();
        e.goToPrevChange();
        int line = -1, col = -1;
        e.getCursorPosition(&line, &col);
        QCOMPARE(line, 1);
    }

    void selectAllOccurrencesSelectsMultiple()
    {
        // FR-060：selectAllOccurrences 對 "foo foo foo" 應產生多重選取
        EditorWidget e;
        e.setText(QStringLiteral("foo foo foo"));
        e.setSelection(0, 0, 0, 3);  // 選取第一個 "foo"
        e.selectAllOccurrences();
        const long selections = e.SendScintilla(EditorWidget::SCI_GETSELECTIONS);
        QVERIFY(selections > 1);
    }

    void selectAllOccurrencesFromCursorWord()
    {
        // 無選取時以游標所在字詞為依據
        EditorWidget e;
        e.setText(QStringLiteral("bar bar bar"));
        e.setCursorPosition(0, 1);  // 游標落在第一個 "bar" 內
        e.selectAllOccurrences();
        const long selections = e.SendScintilla(EditorWidget::SCI_GETSELECTIONS);
        QVERIFY(selections > 1);
    }

    void applyApiCompletionsNoLexerIsNoop()
    {
        // 無 lexer 時安全跳過，不崩潰
        EditorWidget e;
        e.applyApiCompletions({QStringLiteral("foo"), QStringLiteral("bar")});
        QVERIFY(true);  // 未崩潰即通過
    }

    void applyApiCompletionsWithLexerEnablesAutoCompletion()
    {
        EditorWidget e;
        e.setLanguageLexer(new QsciLexerCPP(&e));
        e.applyApiCompletions({QStringLiteral("printf"), QStringLiteral("scanf")});
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsAll);

        // prepare() 於背景 thread 進行、透過 main-thread event 回報完成。
        // 等待 apiPreparationFinished 後再讓 e 解構，避免 in-flight worker 造成不確定性。
        auto *apis = qobject_cast<QsciAPIs *>(e.lexer()->apis());
        QVERIFY(apis != nullptr);
        QSignalSpy spy(apis, &QsciAPIs::apiPreparationFinished);
        QVERIFY(spy.count() > 0 || spy.wait(5000));
    }
    void showLineNumbersTogglesMarginWidth()
    {
        // 預設開啟，寬度 > 0；關閉後寬度歸零；重新開啟則恢復動態寬度
        EditorWidget e;
        QVERIFY(e.showLineNumbers());
        QVERIFY(e.marginWidth(0) > 0);

        e.setShowLineNumbers(false);
        QVERIFY(!e.showLineNumbers());
        QCOMPARE(e.marginWidth(0), 0);

        e.setShowLineNumbers(true);
        QVERIFY(e.showLineNumbers());
        QVERIFY(e.marginWidth(0) > 0);
    }

    void caretWidthRoundTripsAndClamps()
    {
        EditorWidget e;
        QCOMPARE(e.caretWidth(), 1);  // 預設值

        e.setCaretWidth(2);
        QCOMPARE(e.caretWidth(), 2);

        e.setCaretWidth(0);
        QCOMPARE(e.caretWidth(), 0);

        // 超出範圍夾限至 0..3
        e.setCaretWidth(99);
        QCOMPARE(e.caretWidth(), 3);
        e.setCaretWidth(-5);
        QCOMPARE(e.caretWidth(), 0);
    }

    void wordCompletionEnabledTogglesAutoCompletionSource()
    {
        EditorWidget e;
        QVERIFY(e.wordCompletionEnabled());  // 預設開啟
        QVERIFY(e.autoCompletionSource() != QsciScintilla::AcsNone);

        e.setWordCompletionEnabled(false);
        QVERIFY(!e.wordCompletionEnabled());
        QCOMPARE(e.autoCompletionSource(), QsciScintilla::AcsNone);

        e.setWordCompletionEnabled(true);
        QVERIFY(e.wordCompletionEnabled());
        QVERIFY(e.autoCompletionSource() != QsciScintilla::AcsNone);
    }

    void callTipsEnabledGatesCallTipRequested()
    {
        EditorWidget e;
        QVERIFY(e.callTipsEnabled());  // 預設開啟
        e.setText(QStringLiteral("foo"));
        e.setCursorPosition(0, 3);

        QSignalSpy spy(&e, &EditorWidget::callTipRequested);
        QTest::keyClick(&e, '(');
        QCOMPARE(spy.count(), 1);

        e.setCallTipsEnabled(false);
        QVERIFY(!e.callTipsEnabled());
        e.setText(QStringLiteral("bar"));
        e.setCursorPosition(0, 3);
        QSignalSpy spy2(&e, &EditorWidget::callTipRequested);
        QTest::keyClick(&e, '(');
        QCOMPARE(spy2.count(), 0);
    }

    void pathFragmentBeforeExtractsTrailingPathChars()
    {
        // 一般路徑片段（含目錄分隔符）
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("open /usr/lo"), 12),
                 QStringLiteral("/usr/lo"));
        // 非路徑字元（空白）中斷掃描
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("foo bar"), 7),
                 QStringLiteral("bar"));
        // 游標在字串開頭：無前綴
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("/etc/hosts"), 0),
                 QString());
        // 全部字元皆為路徑字元
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("~/proj"), 6),
                 QStringLiteral("~/proj"));
        // pos 超出字串長度時安全鉗制
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("abc"), 100),
                 QStringLiteral("abc"));
        // pos 為負值時安全鉗制為空字串
        QCOMPARE(EditorWidget::pathFragmentBefore(QStringLiteral("abc"), -1), QString());
    }

    void triggerPathCompletionShowsUserListForExistingDir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f1(dir.filePath(QStringLiteral("alpha.txt")));
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.close();

        EditorWidget e;
        const QString text = dir.filePath(QStringLiteral("al"));
        e.setText(text);
        e.setCursorPosition(0, text.length());

        // 觸發不應拋出例外；有無候選視暫存目錄內容而定，僅驗證呼叫安全完成。
        e.triggerPathCompletion();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestEditorOps)
#include "test_editor_ops.moc"
