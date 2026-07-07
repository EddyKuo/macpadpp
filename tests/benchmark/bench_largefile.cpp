// 效能基準：大檔案開啟（NFR-002）與 10MB 全域正則取代（NFR-004）
// 非 CTest 必跑；手動執行：QT_QPA_PLATFORM=offscreen ./bench_largefile [MB]
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDebug>

#include "core/EditorWidget.h"

using macpad::core::EditorWidget;

static QString makeFile(const QString &dir, int megabytes)
{
    const QString path = dir + QStringLiteral("/bench.txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QTextStream out(&f);
    const QString line = QStringLiteral(
        "The quick brown fox 2024-01 jumps over the lazy dog 12345 value=abc\n");
    const int lineBytes = line.toUtf8().size();
    const qint64 target = qint64(megabytes) * 1024 * 1024;
    qint64 written = 0;
    while (written < target) {
        out << line;
        written += lineBytes;
    }
    out.flush();
    f.close();
    return path;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const int mb = (argc > 1) ? QString(argv[1]).toInt() : 50;

    QTemporaryDir tmp;
    const QString path = makeFile(tmp.path(), mb);
    qInfo().noquote() << QStringLiteral("測試檔: %1 MB").arg(mb);

    // --- NFR-002：開啟時間 ---
    EditorWidget editor;
    QElapsedTimer timer;
    timer.start();
    editor.loadFile(path);
    const qint64 openMs = timer.elapsed();
    qInfo().noquote() << QStringLiteral("開啟 %1MB: %2 ms  (NFR-002 目標 100MB<3000ms)")
                             .arg(mb).arg(openMs);

    // --- NFR-004：10MB 全域正則取代 ---
    const QString path10 = makeFile(tmp.path(), 10);
    EditorWidget e2;
    e2.loadFile(path10);
    timer.restart();
    const int count = e2.replaceAll(QStringLiteral("(\\d+)-(\\d+)"),
                                    QStringLiteral("\\2-\\1"), true, true, false);
    const qint64 reMs = timer.elapsed();
    qInfo().noquote() << QStringLiteral("10MB 正則取代 %1 處: %2 ms  (NFR-004 目標 <2000ms)")
                             .arg(count).arg(reMs);

    return 0;
}
