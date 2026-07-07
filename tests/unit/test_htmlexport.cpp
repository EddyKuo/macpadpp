// 單元測試：HtmlExporter htmlEscape + toHtml（FR-036）
#include <QtTest>

#include "features/export/HtmlExporter.h"
#include "core/EditorWidget.h"

using namespace macpad::features;

class TestHtmlExport : public QObject {
    Q_OBJECT
private slots:
    void escapesSpecialChars()
    {
        QCOMPARE(HtmlExporter::htmlEscape("a < b && c > d"),
                 QStringLiteral("a &lt; b &amp;&amp; c &gt; d"));
        QCOMPARE(HtmlExporter::htmlEscape("say \"hi\""),
                 QStringLiteral("say &quot;hi&quot;"));
    }

    void escapePlainUnchanged()
    {
        QCOMPARE(HtmlExporter::htmlEscape("plain text 123"),
                 QStringLiteral("plain text 123"));
    }

    void toHtmlWrapsContent()
    {
        macpad::core::EditorWidget e;
        e.setText(QStringLiteral("hello <world>"));
        const QString html = HtmlExporter::toHtml(&e);
        QVERIFY(html.contains(QStringLiteral("<pre")));
        QVERIFY(html.contains(QStringLiteral("&lt;world&gt;")));  // 內容已跳脫
        QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    }

    void toHtmlNullEditor()
    {
        QVERIFY(HtmlExporter::toHtml(nullptr).isEmpty());
    }
};

QTEST_MAIN(TestHtmlExport)
#include "test_htmlexport.moc"
