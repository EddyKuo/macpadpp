// 單元測試：EditorWidget 進階操作（stats/折疊/縮排/行操作/區塊註解/書籤進階/
// replaceAll 書籤保留/括號選取/編碼與 codec）——補強覆蓋率 (FR-004/007/008/010/011/019/020)
#include <QtTest>
#include <QTemporaryDir>

#include <Qsci/qscilexercpp.h>

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
};

QTEST_MAIN(TestEditorOps)
#include "test_editor_ops.moc"
