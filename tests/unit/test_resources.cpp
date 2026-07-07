// 單元測試：確認 Markdown 預覽的內嵌資源(qrc:/webview/*)真的可存取。
// 靜態庫的 qrc 需顯式 Q_INIT_RESOURCE 才會註冊,否則執行期 WebEngine 會顯示 page not found。
#include <QtTest>
#include <QFile>

class TestResources : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(webview);   // 與 main.cpp 相同的初始化
    }

    void previewPageExists()
    {
        QFile f(QStringLiteral(":/webview/preview.html"));
        QVERIFY2(f.exists(), "qrc:/webview/preview.html 未註冊(靜態庫資源未初始化)");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray html = f.readAll();
        QVERIFY(html.contains("marked.min.js"));
        QVERIFY(html.contains("mermaid.min.js"));
        QVERIFY(html.contains("function render"));
    }

    void jsLibrariesEmbedded()
    {
        QFile marked(QStringLiteral(":/webview/marked.min.js"));
        QVERIFY(marked.exists());
        QVERIFY(marked.size() > 10000);          // marked ~35KB

        QFile mermaid(QStringLiteral(":/webview/mermaid.min.js"));
        QVERIFY(mermaid.exists());
        QVERIFY(mermaid.size() > 1000000);       // mermaid ~3.3MB
    }
};

QTEST_APPLESS_MAIN(TestResources)
#include "test_resources.moc"
