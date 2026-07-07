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
};

QTEST_MAIN(TestEditor)
#include "test_editor.moc"
