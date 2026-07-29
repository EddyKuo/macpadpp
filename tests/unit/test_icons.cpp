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
    static QPixmap renderTinted(const QString &path, const QColor &color, int px = 40)
    {
        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return {};
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        renderer.render(&p);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        return pm;
    }

    // kInked：判定「這個像素看得見」的 alpha 門檻。用 >0 會把幾乎全透明的反鋸齒
    // 像素也算進來，在小尺寸下足以把空隙「橋接」起來，讓下面的檢查失去意義。
    static constexpr int kInked = 128;

    // 不透明區塊的連通元件數（8 連通）——用來判定「兩個疊放的圖形之間確實有空隙」。
    // 換色管線只用 alpha，若空隙被填滿，兩份會融成同一個元件而數量下降。
    static int opaqueComponents(const QImage &img)
    {
        QVector<bool> seen(img.width() * img.height(), false);
        int count = 0;
        for (int y0 = 0; y0 < img.height(); ++y0) {
            for (int x0 = 0; x0 < img.width(); ++x0) {
                const int start = y0 * img.width() + x0;
                if (seen[start] || qAlpha(img.pixel(x0, y0)) < kInked)
                    continue;
                ++count;
                QVector<QPoint> stack{QPoint(x0, y0)};
                seen[start] = true;
                while (!stack.isEmpty()) {
                    const QPoint p = stack.takeLast();
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = p.x() + dx, ny = p.y() + dy;
                            if (nx < 0 || ny < 0 || nx >= img.width() || ny >= img.height())
                                continue;
                            const int idx = ny * img.width() + nx;
                            if (seen[idx] || qAlpha(img.pixel(nx, ny)) < kInked)
                                continue;
                            seen[idx] = true;
                            stack.append(QPoint(nx, ny));
                        }
                    }
                }
            }
        }
        return count;
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

    // saveall 是以 save 位移疊放兩份合成的（scripts/icons/compose_stack.py），
    // 交界處必須挖出實體空隙。若空隙消失，兩份會融成一團實心色——因為換色管線
    // 只用 alpha，重疊區與非重疊區顏色完全相同，肉眼只看得到一個奇怪的形狀。
    // 用連通元件數量驗證：合成後應比單份 save 多出可辨識的區塊。
    void saveAllKeepsVisibleGapBetweenCopies()
    {
        // 涵蓋工具列全部四種尺寸（16/18/24/32），最小尺寸最容易讓空隙消失
        for (int px : {16, 18, 24, 32}) {
            const int single = opaqueComponents(
                renderTinted(QStringLiteral(":/icons/save.svg"), Qt::black, px).toImage());
            const int stacked = opaqueComponents(
                renderTinted(QStringLiteral(":/icons/saveall.svg"), Qt::black, px).toImage());
            QVERIFY2(stacked > single,
                     qPrintable(QStringLiteral("%1px：saveall 連通元件 %2 未多於 save 的 %3，"
                                               "後方那份可能已與前方融合")
                                    .arg(px).arg(stacked).arg(single)));
        }
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
