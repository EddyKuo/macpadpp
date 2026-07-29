// 單元測試：工具列圖示資源必須真的能被 Qt 算繪出來。
//
// 動機：Qt 的 QSvgRenderer 不解析 SVG 的 `currentColor`——多數圖示庫（Lucide/Tabler/
// Phosphor…）預設就是用它。直接把這種 SVG 丟進來「編譯得過、資源也在」，但工具列上
// 會是一片空白，而且沒有任何錯誤訊息。這個測試把「看得見」變成可驗證的條件。
#include <QtTest>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QDir>

class TestIcons : public QObject {
    Q_OBJECT

    // 與 MainWindow 的 tintedSvgIcon 相同作法：算繪後以 SourceIn 換色，
    // 因此真正被使用的只有 alpha 通道。
    static QPixmap renderTinted(const QString &path, const QColor &color)
    {
        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return {};
        QPixmap pm(40, 40);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        renderer.render(&p);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        return pm;
    }

    // 不透明像素比例——用來判定「圖示不是一片空白」
    static double opaqueRatio(const QPixmap &pm)
    {
        if (pm.isNull())
            return 0.0;
        const QImage img = pm.toImage();
        int opaque = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0)
                    ++opaque;
        return double(opaque) / (img.width() * img.height());
    }

private slots:
    void initTestCase() { Q_INIT_RESOURCE(icons); }

    // 工具列用到的每一個圖示都必須存在、可解析、且算繪出可見內容
    void everyToolbarIconRenders()
    {
        const QStringList names = {
            QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("save"),
            QStringLiteral("saveall"), QStringLiteral("close"), QStringLiteral("closeall"),
            QStringLiteral("print"), QStringLiteral("cut"), QStringLiteral("copy"),
            QStringLiteral("paste"), QStringLiteral("undo"), QStringLiteral("redo"),
            QStringLiteral("find"), QStringLiteral("replace"), QStringLiteral("zoomin"),
            QStringLiteral("zoomout"), QStringLiteral("wordwrap"), QStringLiteral("showall"),
        };

        for (const QString &n : names) {
            const QString path = QStringLiteral(":/icons/%1.svg").arg(n);
            QVERIFY2(QFile::exists(path), qPrintable(path));

            QSvgRenderer renderer(path);
            QVERIFY2(renderer.isValid(), qPrintable(QStringLiteral("無法解析 %1").arg(path)));

            const QPixmap pm = renderTinted(path, Qt::black);
            const double ratio = opaqueRatio(pm);
            // 門檻刻意壓低：只要求「畫得出東西」，不對造型做假設。
            // 全空（ratio==0）正是 currentColor 未處理時的症狀。
            QVERIFY2(ratio > 0.01,
                     qPrintable(QStringLiteral("%1 算繪為空白（不透明像素比例 %2）")
                                    .arg(n).arg(ratio)));
        }
    }

    // 換色機制必須真的生效：同一圖示以不同顏色算繪，像素應不同。
    // 若圖示自帶硬編顏色而未被 SourceIn 覆蓋，深色主題下就會看不見。
    void tintingActuallyChangesColour()
    {
        const QString path = QStringLiteral(":/icons/save.svg");
        const QPixmap black = renderTinted(path, Qt::black);
        const QPixmap white = renderTinted(path, Qt::white);
        QVERIFY(!black.isNull() && !white.isNull());
        QVERIFY(black.toImage() != white.toImage());
    }

    // 資源中不得再出現 currentColor——它可編譯、可打包，但在 Qt 下就是看不見。
    void noCurrentColorInIconResources()
    {
        const QDir dir(QStringLiteral(":/icons"));
        const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.svg"),
                                                QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString &f : files) {
            QFile file(dir.filePath(f));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray content = file.readAll();
            QVERIFY2(!content.contains("currentColor"),
                     qPrintable(QStringLiteral("%1 含 currentColor，Qt 會算繪不出來").arg(f)));
        }
    }
};

QTEST_MAIN(TestIcons)
#include "test_icons.moc"
