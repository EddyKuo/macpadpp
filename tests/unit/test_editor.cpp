// 單元測試：EditorWidget 書籤導覽、EOL 轉換、經磁碟編碼 round-trip（FR-008/019/020）
#include <QtTest>
#include <QTemporaryDir>

#include "core/EditorWidget.h"

using namespace macpad::core;

class TestEditor : public QObject {
    Q_OBJECT
private slots:
    void bookmarkToggleAndNavigate()
    {
        EditorWidget e;
        e.setText(QStringLiteral("l0\nl1\nl2\nl3\nl4"));

        e.setCursorPosition(1, 0);
        e.toggleBookmark();
        e.setCursorPosition(3, 0);
        e.toggleBookmark();

        // 從第 0 行找下一書籤 → 第 1 行
        e.setCursorPosition(0, 0);
        e.nextBookmark();
        int line = 0, index = 0;
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);

        // 再下一個 → 第 3 行
        e.nextBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 3);

        // 循環：再下一個回到第 1 行
        e.nextBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);

        // 切換掉第 1 行書籤後，往下只剩第 3 行
        e.toggleBookmark();  // 游標在第 1 行 → 移除
        e.setCursorPosition(0, 0);
        e.nextBookmark();
        e.getCursorPosition(&line, &index);
        QCOMPARE(line, 3);
    }

    void eolConversion()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a\nb\nc"));
        e.convertEol(Eol::CrLf);
        QCOMPARE(e.eol(), Eol::CrLf);
        QVERIFY(e.text().contains(QStringLiteral("\r\n")));
        e.convertEol(Eol::Lf);
        QCOMPARE(e.eol(), Eol::Lf);
        QVERIFY(!e.text().contains(QStringLiteral("\r")));
    }

    void encodingRoundtripThroughDisk_data()
    {
        QTest::addColumn<int>("enc");
        QTest::newRow("utf8")    << int(Encoding::Utf8);
        QTest::newRow("utf8bom") << int(Encoding::Utf8Bom);
        QTest::newRow("utf16le") << int(Encoding::Utf16LE);
    }
    void encodingRoundtripThroughDisk()
    {
        QFETCH(int, enc);
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/t.txt");
        const QString content = QStringLiteral("Hello 世界 café\nsecond line");

        EditorWidget writer;
        writer.setText(content);
        writer.setEncoding(Encoding(enc));
        QString err;
        QVERIFY2(writer.saveFile(path, &err), qPrintable(err));

        EditorWidget reader;
        QVERIFY(reader.loadFile(path, &err));
        QCOMPARE(reader.text(), content);
        QCOMPARE(reader.encoding(), Encoding(enc));
    }

    void dirtyFlagLifecycle()
    {
        EditorWidget e;
        QVERIFY(!e.isDirty());
        e.setText(QStringLiteral("hi"));
        e.setModified(true);
        QVERIFY(e.isDirty());
        QVERIFY(e.displayName().startsWith(QStringLiteral("●")));
    }

    // 標籤配對（highlightMatchingTags 引擎邏輯）——純函式，字元索引空間
    void matchingTagRanges()
    {
        // 游標落在開啟標籤 <b> 內 → 配對到閉合 </b>
        const QString html = QStringLiteral("<a><b>text</b></a>");
        //                                    0123456789...
        int os = 0, oe = 0, cs = 0, ce = 0;
        // caret 在 <b>（index 3..5）
        QVERIFY(EditorWidget::matchingTagRanges(html, 4, &os, &oe, &cs, &ce));
        QCOMPARE(html.mid(os, oe - os), QStringLiteral("<b>"));
        QCOMPARE(html.mid(cs, ce - cs), QStringLiteral("</b>"));

        // 游標落在閉合標籤 </a> → 往前配對到開啟 <a>
        const int closeAPos = html.indexOf(QStringLiteral("</a>")) + 1;
        QVERIFY(EditorWidget::matchingTagRanges(html, closeAPos, &os, &oe, &cs, &ce));
        QCOMPARE(html.mid(os, oe - os), QStringLiteral("<a>"));
        QCOMPARE(html.mid(cs, ce - cs), QStringLiteral("</a>"));

        // 巢狀同名標籤：深度計數需正確配對
        const QString nested = QStringLiteral("<div><div>x</div></div>");
        QVERIFY(EditorWidget::matchingTagRanges(nested, 1, &os, &oe, &cs, &ce));
        QCOMPARE(os, 0);                       // 外層 <div>
        QCOMPARE(ce, nested.size());           // 配對到最後的 </div>

        // 自閉合標籤 <br/>：開啟/閉合範圍相同
        const QString selfClose = QStringLiteral("a<br/>b");
        QVERIFY(EditorWidget::matchingTagRanges(selfClose, 2, &os, &oe, &cs, &ce));
        QCOMPARE(os, cs);
        QCOMPARE(oe, ce);
        QCOMPARE(selfClose.mid(os, oe - os), QStringLiteral("<br/>"));

        // 游標不在任何標籤內（純文字）→ 不配對
        QVERIFY(!EditorWidget::matchingTagRanges(html, 8, &os, &oe, &cs, &ce));

        // 沒有配對的孤立開啟標籤 → 不配對
        const QString unmatched = QStringLiteral("<p>hello");
        QVERIFY(!EditorWidget::matchingTagRanges(unmatched, 1, &os, &oe, &cs, &ce));
    }

    // 右鍵選單：有接收者時攔截 contextMenuEvent，改發 contextMenuRequested 並帶全域座標
    // （複刻 Notepad++ 編輯區右鍵：停用 Scintilla 內建 popup 後由上層建構完整選單）。
    void contextMenuRequestedSignal()
    {
        EditorWidget e;
        e.setText(QStringLiteral("hello world"));
        e.resize(200, 100);

        QSignalSpy spy(&e, &EditorWidget::contextMenuRequested);
        QVERIFY(spy.isValid());

        const QPoint local(20, 10);
        const QPoint global = e.viewport()->mapToGlobal(local);
        QContextMenuEvent ev(QContextMenuEvent::Mouse, local, global);
        QApplication::sendEvent(e.viewport(), &ev);

        QCOMPARE(spy.count(), 1);
        // 事件被接受（不再冒泡到父層 / Scintilla 內建 popup）
        QVERIFY(ev.isAccepted());
        // 轉發的座標即事件的全域座標
        const QPoint emitted = spy.takeFirst().at(0).toPoint();
        QCOMPARE(emitted, global);
    }
};

QTEST_MAIN(TestEditor)
#include "test_editor.moc"
