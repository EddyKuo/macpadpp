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

    // OS 層唯讀屬性偵測與切換（複刻 Notepad++ Clear Read-Only Flag）。
    // Qt 權限 API 跨平台：Windows 對應 FILE_ATTRIBUTE_READONLY、POSIX 對應寫入位元。
    void fileReadOnlyAttributeRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("ro.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("hello");
        }

        // 一般可寫檔案 → 非唯讀
        QVERIFY(!EditorWidget::isFileReadOnly(path));

        // 設為唯讀 → 偵測得到
        QVERIFY(EditorWidget::setFileReadOnly(path, true));
        QVERIFY(EditorWidget::isFileReadOnly(path));

        // 清除唯讀 → 回復可寫，且內容仍在（只動權限位元，不碰內容）
        QVERIFY(EditorWidget::setFileReadOnly(path, false));
        QVERIFY(!EditorWidget::isFileReadOnly(path));
        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("hello"));
    }

    // 鎖定→解鎖必須完整還原原始權限：只動 owner 寫入位元，group/other 不得被吃掉。
    // （若鎖定時連 group 寫入一起清掉，解鎖時無從得知原本該不該補回，必然失真。）
    void fileReadOnlyPreservesGroupOtherBits()
    {
#ifndef Q_OS_WIN   // Windows 無 POSIX group/other 位元概念
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("shared.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("team file");
        }
        // 模擬團隊共用檔 664：owner rw / group rw / other r
        const QFileDevice::Permissions original =
            QFileDevice::ReadOwner  | QFileDevice::WriteOwner |
            QFileDevice::ReadUser   | QFileDevice::WriteUser  |
            QFileDevice::ReadGroup  | QFileDevice::WriteGroup |
            QFileDevice::ReadOther;
        QVERIFY(QFile::setPermissions(path, original));
        QCOMPARE(QFileInfo(path).permissions(), original);

        QVERIFY(EditorWidget::setFileReadOnly(path, true));
        QVERIFY(EditorWidget::isFileReadOnly(path));
        // group 的寫入位元必須原封不動（未被鎖定動作吃掉）
        QVERIFY(QFileInfo(path).permissions() & QFileDevice::WriteGroup);

        QVERIFY(EditorWidget::setFileReadOnly(path, false));
        QVERIFY(!EditorWidget::isFileReadOnly(path));
        // 解鎖後權限應與原始完全一致
        QCOMPARE(QFileInfo(path).permissions(), original);
#endif
    }

    // untitled / 不存在的路徑不得被誤判為唯讀，且設定時要明確失敗（非靜默吞噬，IL-4）
    void fileReadOnlyIgnoresMissingPaths()
    {
        QVERIFY(!EditorWidget::isFileReadOnly(QString()));
        QVERIFY(!EditorWidget::isFileReadOnly(QStringLiteral("/no/such/file/anywhere.txt")));

        QString err;
        QVERIFY(!EditorWidget::setFileReadOnly(QStringLiteral("/no/such/file/anywhere.txt"), true, &err));
        QVERIFY(!err.isEmpty());
    }

    // 宣告了 Big5 的 HTML 檔應依宣告解碼（先前只看 BOM/UTF-8，會落入 Latin1 顯示亂碼）。
    // 這是 XML/HTML charset 宣告嗅探的端對端驗證：實際寫檔 → loadFile → 比對文字。
    void htmlDeclaredCharsetDecodesCorrectly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("big5.html"));
        const QString chinese = QStringLiteral("繁體中文測試");
        const QString html =
            QStringLiteral("<html><head><meta charset=\"Big5\"></head><body>%1</body></html>")
                .arg(chinese);

        // 以 Big5 位元組寫入（刻意不是 UTF-8，否則測不到宣告嗅探）
        const QByteArray big5 = FileEncoding::encodeWithCodec(html, QStringLiteral("Big5"));
        QVERIFY(!big5.isEmpty());
        QVERIFY(big5 != html.toUtf8());   // 確認真的不是 UTF-8 位元組
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(big5);
        }

        EditorWidget e;
        QVERIFY(e.loadFile(path));
        // 中文字必須正確還原（若走 Latin1 路徑會是亂碼）
        QVERIFY2(e.text().contains(chinese), qPrintable(e.text().left(80)));
    }

    // 拖放開檔：只有「本機既有檔案」才被攔截，其餘一律讓事件回到 Scintilla 文字拖放
    void dropMimeExtractsOnlyLocalFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString f1 = dir.filePath(QStringLiteral("a.txt"));
        const QString f2 = dir.filePath(QStringLiteral("b.txt"));
        for (const QString &p : {f1, f2}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(f1),
                      QUrl::fromLocalFile(f2),
                      QUrl(QStringLiteral("https://example.com/remote.txt")),   // 遠端 → 略過
                      QUrl::fromLocalFile(dir.path()),                          // 目錄 → 略過
                      QUrl::fromLocalFile(dir.filePath(QStringLiteral("ghost.txt")))});  // 不存在 → 略過

        const QStringList got = EditorWidget::localFilePathsFromMime(&mime);
        QCOMPARE(got.size(), 2);
        QVERIFY(got.contains(QFileInfo(f1).absoluteFilePath()));
        QVERIFY(got.contains(QFileInfo(f2).absoluteFilePath()));

        // 純文字拖放不得被誤判為檔案拖放（否則會吃掉 Scintilla 的文字拖放行為）
        QMimeData textMime;
        textMime.setText(QStringLiteral("just some text"));
        QVERIFY(EditorWidget::localFilePathsFromMime(&textMime).isEmpty());
        QVERIFY(EditorWidget::localFilePathsFromMime(nullptr).isEmpty());
    }

    // 拖入檔案 → 發出 filesDropped 讓上層開分頁；拖入純文字 → 不發訊號（交回 Scintilla）。
    // 關鍵：事件必須送到 viewport()——QsciScintilla 繼承 QAbstractScrollArea，拖放落點在
    // viewport 上，送到 widget 本身不會觸發（與 contextMenuRequestedSignal 同一個坑）。
    void dropEventEmitsFilesDropped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("dropped.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("content");
        }

        EditorWidget e;
        e.resize(200, 100);
        QSignalSpy spy(&e, &EditorWidget::filesDropped);
        QVERIFY(spy.isValid());

        // 檔案拖放 → 吃掉事件並發訊號
        QMimeData fileMime;
        fileMime.setUrls({QUrl::fromLocalFile(path)});
        QVERIFY(e.handleFileDropMime(&fileMime));
        QCOMPARE(spy.count(), 1);
        const QStringList emitted = spy.takeFirst().at(0).toStringList();
        QCOMPARE(emitted.size(), 1);
        QCOMPARE(emitted.first(), QFileInfo(path).absoluteFilePath());

        // 純文字拖放 → 不吃事件、不發訊號（交還 Scintilla 原生文字拖曳）
        QMimeData textMime;
        textMime.setText(QStringLiteral("plain"));
        QVERIFY(!e.handleFileDropMime(&textMime));
        QCOMPARE(spy.count(), 0);

        // 空 mime 亦不得誤判
        QVERIFY(!e.handleFileDropMime(nullptr));
        QCOMPARE(spy.count(), 0);

        // 拖放後編輯器本身必須開放接收拖放（否則實際使用時系統不會送事件過來）
        QVERIFY(e.acceptDrops());
        QVERIFY(e.viewport()->acceptDrops());
    }

    // 開啟磁碟唯讀檔 → 自動進入唯讀模式（避免編輯半天才在存檔時失敗）
    void loadingReadOnlyFileLocksEditor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("locked.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("locked content");
        }
        QVERIFY(EditorWidget::setFileReadOnly(path, true));

        EditorWidget e;
        QVERIFY(e.loadFile(path));
        QCOMPARE(e.text(), QStringLiteral("locked content"));
        QVERIFY(e.isReadOnly());        // app 內編輯鎖已自動開啟
        QVERIFY(e.isFileReadOnly());    // 磁碟屬性確實唯讀

        // 清除屬性後，可解除編輯鎖
        QVERIFY(EditorWidget::setFileReadOnly(path, false));
        QVERIFY(!e.isFileReadOnly());

        // 一般可寫檔案則不應被鎖
        const QString rw = dir.filePath(QStringLiteral("normal.txt"));
        {
            QFile f(rw);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("free");
        }
        EditorWidget e2;
        QVERIFY(e2.loadFile(rw));
        QVERIFY(!e2.isReadOnly());
    }
};

QTEST_MAIN(TestEditor)
#include "test_editor.moc"
