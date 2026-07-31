// 單元測試：輸出管線三件套 —— 列印（DocumentPrinter）、RTF 匯出（RtfExporter）、
// 更新下載（UpdateDownloader）。
//
// 這三個模組的共同點是「終點在行程之外」（印表機 / 檔案 / 網路），過去因此難以自動測。
// 本檔的作法是把每個外部終點換成可觀察、可控制的本機替身，讓斷言驗證真實產出：
//   * 列印   → QPrinter::PdfFormat + setOutputFileName，輸出到 QTemporaryDir 的 PDF，
//              再從 PDF 內容讀回實際頁數。絕不觸碰任何 modal 對話框（QPrintDialog::exec()
//              在 offscreen 下會永久阻塞 CI）。
//   * RTF    → 純字串產出，直接比對控制字與 \colortbl 色表。
//   * 下載   → 本機 QTcpServer 假伺服器。絕不對真實網路發出請求：CI 不一定有外網，
//              且對 GitHub 發真請求會讓測試結果取決於第三方服務可用性（違反 IL-1 的精神）。
//              落地目錄以 QStandardPaths 測試模式導向 ~/.qttest/Downloads，不汙染使用者家目錄。

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <Qsci/qscilexercpp.h>

#include "core/EditorWidget.h"
#include "features/export/RtfExporter.h"
#include "features/print/DocumentPrinter.h"
#include "features/update/UpdateDownloader.h"

using macpad::core::EditorWidget;
using macpad::features::DocumentPrinter;
using macpad::features::RtfExporter;
using macpad::features::UpdateDownloader;

// ─────────────────────────────────────────────────────────────────────────────
// 本機假 HTTP 伺服器
//
// 只實作 UpdateDownloader 會用到的最小子集：讀完請求標頭後回一個帶 Content-Length
// 的回應。刻意支援「宣告長度大於實送長度且不關閉連線」（stall）的模式——取消與解構
// 這兩條路徑必須在「下載進行中」的狀態下觸發，否則測到的是下載已完成後的分支。
// ─────────────────────────────────────────────────────────────────────────────
class FakeHttpServer : public QTcpServer {
    Q_OBJECT
public:
    explicit FakeHttpServer(QObject *parent = nullptr) : QTcpServer(parent) {}

    QByteArray body;                 // 實際送出的位元組
    int status = 200;
    QByteArray reason = "OK";
    qint64 declaredLength = -1;      // <0 表示用 body.size()；設大於 body 可模擬未送完
    bool stall = false;              // true = 送完 body 後不關連線，讓下載停在半途

    QString urlFor(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        if (!sock->setSocketDescriptor(socketDescriptor)) {
            delete sock;
            return;
        }
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            if (m_answered.contains(sock))
                return;                       // 同一連線只回應一次
            m_buffers[sock] += sock->readAll();
            if (!m_buffers[sock].contains("\r\n\r\n"))
                return;                       // 請求標頭尚未收齊
            m_answered.insert(sock);
            respond(sock);
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
            m_buffers.remove(sock);
            m_answered.remove(sock);
            sock->deleteLater();
        });
    }

private:
    void respond(QTcpSocket *sock)
    {
        const qint64 len = (declaredLength >= 0) ? declaredLength
                                                 : static_cast<qint64>(body.size());
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        head += "Content-Type: application/octet-stream\r\n";
        head += "Content-Length: " + QByteArray::number(len) + "\r\n";
        head += "Connection: close\r\n\r\n";
        sock->write(head);
        if (!body.isEmpty())
            sock->write(body);
        sock->flush();
        if (!stall)
            sock->disconnectFromHost();
    }

    QHash<QTcpSocket *, QByteArray> m_buffers;
    QSet<QTcpSocket *> m_answered;
};

// ─────────────────────────────────────────────────────────────────────────────

class TestOutputPipeline : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;   // 所有 PDF 產出都落在這裡，測試結束自動清除

    QString outPath(const QString &name) const { return m_tmp.filePath(name); }

    // 從 Qt 產生的 PDF 讀回頁數。頁樹節點是未壓縮的物件，/Count 可直接文字比對；
    // 取最大值以免被巢狀頁樹的子節點數字誤導。
    static int pdfPageCount(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return -1;
        const QByteArray bytes = f.readAll();
        static const QRegularExpression re(QStringLiteral("/Count\\s+(\\d+)"));
        auto it = re.globalMatch(QString::fromLatin1(bytes));
        int best = -1;
        while (it.hasNext())
            best = qMax(best, it.next().captured(1).toInt());
        return best;
    }

    // 建立一份已設定為輸出 PDF 的印表機。解析度與版面固定，讓分頁結果不受環境預設影響。
    static void setupPdf(DocumentPrinter &p, const QString &path)
    {
        p.setOutputFormat(QPrinter::PdfFormat);
        p.setOutputFileName(path);
        p.setResolution(150);
        p.setPageSize(QPageSize(QPageSize::A4));
    }

    // 下載測試會用到的落地檔名（含衝突時的序號變體），供逐一清除。
    // 不整個清空下載目錄：Windows 的 QStandardPaths 測試模式未必會改寫 DownloadLocation，
    // 一旦沒改寫就會誤刪使用者真正的下載檔。只刪自己可能建立的名字才安全。
    static void removeArtifacts()
    {
        const QDir dir(UpdateDownloader::downloadDir());
        const QStringList bases = {QStringLiteral("pkg"), QStringLiteral("empty"),
                                   QStringLiteral("missing"), QStringLiteral("stalled"),
                                   QStringLiteral("short")};
        for (const QString &b : bases) {
            QFile::remove(dir.filePath(b + QStringLiteral(".bin")));
            for (int n = 1; n <= 3; ++n)
                QFile::remove(dir.filePath(QStringLiteral("%1 (%2).bin").arg(b).arg(n)));
        }
    }

    static QString artifactPath(const QString &name)
    {
        return QDir(UpdateDownloader::downloadDir()).filePath(name);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // 把 DownloadLocation 導向 ~/.qttest/Downloads，避免測試把檔案丟進使用者的下載資料夾
        QStandardPaths::setTestModeEnabled(true);
        QDir().mkpath(UpdateDownloader::downloadDir());
    }

    void cleanup() { removeArtifacts(); }

    // ── RtfExporter ─────────────────────────────────────────────────────────

    // RTF 的 \ { } 是控制字元，未跳脫會讓整份文件的群組結構錯位，接收端解析崩壞。
    void rtfEscapesControlCharacters()
    {
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("a\\b{c}d")),
                 QStringLiteral("a\\\\b\\{c\\}d"));
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("a\tb")),
                 QStringLiteral("a\\tab b"));
    }

    // 換行一律轉 \par；CRLF 必須只產生一個 \par，否則 Windows 來源的檔案匯出後行距加倍
    void rtfNewlinesBecomeSinglePar()
    {
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("a\r\nb")),
                 QStringLiteral("a\\par\nb"));
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("a\nb")),
                 QStringLiteral("a\\par\nb"));
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("\r")), QString());
    }

    // 非 ASCII 走 \uN? 形式；> 0x7FFF 者依 RTF 規格須寫成負值（16 位元帶號），
    // 寫成正值會讓解譯器讀到錯誤碼位。emoji 的代理對正是這個邊界。
    void rtfEncodesNonAsciiAsSignedUnicode()
    {
        QCOMPARE(RtfExporter::rtfEscape(QStringLiteral("caf\u00E9")),
                 QStringLiteral("caf\\u233?"));
        // U+1F600 在 UTF-16 下是 D83D DE00：55357-65536 = -10179、56832-65536 = -8704
        QCOMPARE(RtfExporter::rtfEscape(QString::fromUcs4(U"\U0001F600", 1)),
                 QStringLiteral("\\u-10179?\\u-8704?"));
        QCOMPARE(RtfExporter::rtfEscape(QString()), QString());
    }

    void rtfNullEditorYieldsEmpty()
    {
        QVERIFY(RtfExporter::toRtf(nullptr).isEmpty());
    }

    // 空文件仍須是合法 RTF：有 header、有（空的）色表，而不是空字串或半截文件
    void rtfEmptyDocumentIsStillValidRtf()
    {
        EditorWidget e;
        e.setText(QString());
        const QString rtf = RtfExporter::toRtf(&e);
        QVERIFY(rtf.startsWith(QStringLiteral("{\\rtf1\\ansi\\deff0")));
        QVERIFY(rtf.endsWith(QStringLiteral("}\n")));
        QVERIFY(rtf.contains(QStringLiteral("{\\colortbl ;}")));   // 沒有任何色段
        QVERIFY(rtf.contains(QStringLiteral("Courier New")));
        QVERIFY(!rtf.contains(QStringLiteral("\\cf")));
    }

    // 無 lexer（純文字）時所有內容為單一黑色段：色表恰好一筆黑色，內文只有一個 \cf1
    void rtfWithoutLexerUsesSingleBlackRun()
    {
        EditorWidget e;
        e.setLanguageLexer(nullptr);
        e.setText(QStringLiteral("hello {world}\nsecond"));
        const QString rtf = RtfExporter::toRtf(&e);
        QCOMPARE(rtf.count(QStringLiteral("\\red")), 1);
        QVERIFY(rtf.contains(QStringLiteral("{\\colortbl ;\\red0\\green0\\blue0;}")));
        QCOMPARE(rtf.count(QStringLiteral("\\cf1 ")), 1);
        QVERIFY(rtf.contains(QStringLiteral("\\{world\\}")));       // 內文已跳脫
        QVERIFY(rtf.contains(QStringLiteral("\\par\nsecond")));
    }

    // 有 lexer 時必須依樣式切段：註解與程式碼顏色不同 → 色表至少兩筆、內文多個 \cf。
    // 這是 toRtf 的核心價值（帶語法高亮匯出），只測純文字會漏掉整個切段迴圈。
    void rtfWithLexerProducesMultipleColorRuns()
    {
        EditorWidget e;
        auto *lexer = new QsciLexerCPP(&e);
        e.setLanguageLexer(lexer);
        e.setText(QStringLiteral("int x = 1; // note\nint y = 2;\n"));
        // Scintilla 是延遲上色的；不強制 colourise 的話 SCI_GETSTYLEAT 會全部回 0
        e.SendScintilla(QsciScintilla::SCI_COLOURISE, 0, -1);

        const QString rtf = RtfExporter::toRtf(&e);
        QVERIFY2(rtf.count(QStringLiteral("\\red")) >= 2,
                 qPrintable(QStringLiteral("色表筆數：%1").arg(rtf.count(QStringLiteral("\\red")))));
        QVERIFY(rtf.count(QStringLiteral("\\cf")) >= 2);
        QVERIFY(rtf.contains(QStringLiteral("\\par")));
        // 相同顏色必須共用同一個索引，不可每段都新增一筆色表
        QVERIFY(rtf.count(QStringLiteral("\\red")) < rtf.count(QStringLiteral("\\cf")));
    }

    // ── DocumentPrinter ─────────────────────────────────────────────────────

    // 只有樣板真的引用 $(NB_PAGES) 才值得付出試排（整份文件多繪一次）的成本
    void printerDetectsPageCountToken()
    {
        DocumentPrinter p;
        QVERIFY(!p.needsPageCount());
        p.setHeaderTemplate(QStringLiteral("$(FILE_NAME)"));
        QVERIFY(!p.needsPageCount());
        p.setHeaderTemplate(QStringLiteral("p $(CURRENT_PAGE)/$(NB_PAGES)"));
        QVERIFY(p.needsPageCount());
        p.setHeaderTemplate(QString());
        p.setFooterTemplate(QStringLiteral("$(NB_PAGES)"));
        QVERIFY(p.needsPageCount());
    }

    // formatPage 的量測路徑（drawing=false）必須與繪製路徑做出相同的區域縮減，
    // 否則 Qt 量到的可用高度比實際大，內文會壓到頁首頁尾上。這裡直接驗證區域被縮小。
    void formatPageReservesSpaceWhenMeasuring()
    {
        QImage canvas(600, 800, QImage::Format_ARGB32);
        canvas.fill(Qt::white);
        const QImage blank = canvas;   // 比對基準；QImage(w,h,fmt) 的像素未初始化，不可直接拿來比
        QPainter painter(&canvas);

        DocumentPrinter p;
        p.setHeaderTemplate(QStringLiteral("head"));
        p.setFooterTemplate(QStringLiteral("foot"));

        const QRect original(10, 10, 580, 780);
        QRect area = original;
        p.formatPage(painter, false, area, 1);      // drawing=false：只量測，不得繪圖
        QVERIFY(area.top() > original.top());
        QVERIFY(area.bottom() < original.bottom());
        QCOMPARE(area.left(), original.left());     // 只縮上下，左右不動
        QCOMPARE(area.right(), original.right());

        // 繪製路徑必須縮減出完全相同的區域，兩趟版面才會一致
        QRect drawArea = original;
        p.formatPage(painter, true, drawArea, 1);
        QCOMPARE(drawArea, area);
        painter.end();
        // drawing=true 確實畫了頁首/頁尾與分隔線：畫布不再全白
        QVERIFY(canvas != blank);
    }

    // 沒設頁首頁尾時完全不介入版面——維持 QsciPrinter 原本行為，不平白吃掉可用高度
    void formatPageWithoutTemplatesLeavesAreaUntouched()
    {
        QImage canvas(400, 400, QImage::Format_ARGB32);
        canvas.fill(Qt::white);
        const QImage blank = canvas;
        QPainter painter(&canvas);

        DocumentPrinter p;
        const QRect original(5, 5, 390, 390);
        QRect area = original;
        p.formatPage(painter, true, area, 1);
        QCOMPARE(area, original);
        painter.end();
        QCOMPARE(canvas, blank);   // 連一條分隔線都不該畫
    }

    // 只設頁首（或只設頁尾）時，只能縮那一邊
    void formatPageHandlesHeaderOnlyAndFooterOnly()
    {
        QImage canvas(400, 400, QImage::Format_ARGB32);
        QPainter painter(&canvas);
        const QRect original(0, 0, 400, 400);

        DocumentPrinter headerOnly;
        headerOnly.setHeaderTemplate(QStringLiteral("h"));
        QRect a = original;
        headerOnly.formatPage(painter, false, a, 1);
        QVERIFY(a.top() > original.top());
        QCOMPARE(a.bottom(), original.bottom());

        DocumentPrinter footerOnly;
        footerOnly.setFooterTemplate(QStringLiteral("f"));
        QRect b = original;
        footerOnly.formatPage(painter, false, b, 1);
        QCOMPARE(b.top(), original.top());
        QVERIFY(b.bottom() < original.bottom());
        painter.end();
    }

    // 空指標必須明確回傳失敗（IL-4：失敗快失敗明），不得解參考崩潰
    void printerRejectsNullEditor()
    {
        DocumentPrinter p;
        QCOMPARE(p.printDocument(nullptr, false), 0);
        QCOMPARE(p.printDocument(nullptr, true), 0);
        QCOMPARE(p.printWithFormFeeds(nullptr), 0);
    }

    // 端到端：真的產出一份 PDF，且頁首頁尾不影響列印成功。
    // 斷言看的是實體檔案而不只是回傳值——回傳非 0 但寫出 0 位元組的檔案是真實會發生的失敗。
    void printsPdfWithHeaderAndFooter()
    {
        EditorWidget e;
        e.setText(QStringLiteral("alpha\nbeta\ngamma\n"));

        const QString path = outPath(QStringLiteral("header-footer.pdf"));
        DocumentPrinter p;
        setupPdf(p, path);
        p.setFilePath(QStringLiteral("/docs/report.txt"));
        p.setHeaderTemplate(QStringLiteral("$(FILE_NAME) $(CURRENT_DATE)"));
        p.setFooterTemplate(QStringLiteral("page $(CURRENT_PAGE)"));

        QVERIFY(p.printDocument(&e, false) != 0);
        const QFileInfo fi(path);
        QVERIFY(fi.exists());
        QVERIFY(fi.size() > 0);
        QCOMPARE(pdfPageCount(path), 1);
    }

    // 沒有頁首頁尾的一般列印路徑（formatPage 的早期 return 分支）也必須產出有效 PDF
    void printsPdfWithoutTemplates()
    {
        EditorWidget e;
        e.setText(QStringLiteral("plain document\n"));

        const QString path = outPath(QStringLiteral("plain.pdf"));
        DocumentPrinter p;
        setupPdf(p, path);
        QVERIFY(p.printDocument(&e, false) != 0);
        QVERIFY(QFileInfo(path).size() > 0);
        QCOMPARE(pdfPageCount(path), 1);
    }

    // $(NB_PAGES) 會觸發「先試排求總頁數」的路徑：試排另開一台 PDF 印表機把整份文件
    // 再排一次。這裡用一份必然跨頁的長文件，確認試排不但不崩潰、也不影響最終輸出頁數。
    void printsMultiPageDocumentWithPageCountToken()
    {
        QStringList lines;
        for (int i = 0; i < 400; ++i)
            lines << QStringLiteral("line %1 of the long document").arg(i);
        EditorWidget e;
        e.setText(lines.join(QLatin1Char('\n')));

        const QString path = outPath(QStringLiteral("nbpages.pdf"));
        DocumentPrinter p;
        setupPdf(p, path);
        p.setHeaderTemplate(QStringLiteral("$(CURRENT_PAGE) / $(NB_PAGES)"));
        QVERIFY(p.printDocument(&e, false) != 0);

        const int pages = pdfPageCount(path);
        QVERIFY2(pages > 1, qPrintable(QStringLiteral("實得頁數 %1").arg(pages)));

        // 沒有 $(NB_PAGES) 的相同文件（不試排）必須排出相同頁數——
        // 試排若用了不同 DPI/版面，兩者會不一致，$(NB_PAGES) 就會印出錯的總頁數。
        const QString ref = outPath(QStringLiteral("nbpages-ref.pdf"));
        DocumentPrinter q;
        setupPdf(q, ref);
        q.setHeaderTemplate(QStringLiteral("$(CURRENT_PAGE)"));
        QVERIFY(q.printDocument(&e, false) != 0);
        QCOMPARE(pdfPageCount(ref), pages);
    }

    // FormFeed 分頁：\f 自成一行時，文件被切成 3 段 → 3 頁。
    // 同一份文字在 formFeeds=false 下只有 1 頁，兩者對照才證明分頁真的來自 \f。
    void formFeedSplitsIntoOnePagePerSegment()
    {
        EditorWidget e;
        e.setText(QStringLiteral("first\n\f\nsecond\n\f\nthird"));

        const QString split = outPath(QStringLiteral("formfeed-on.pdf"));
        DocumentPrinter p;
        setupPdf(p, split);
        p.setFooterTemplate(QStringLiteral("$(CURRENT_PAGE)"));   // 逐段頁碼偏移也一併走過
        QVERIFY(p.printWithFormFeeds(&e) != 0);
        QCOMPARE(pdfPageCount(split), 3);

        const QString merged = outPath(QStringLiteral("formfeed-off.pdf"));
        DocumentPrinter q;
        setupPdf(q, merged);
        QVERIFY(q.printDocument(&e, false) != 0);
        QCOMPARE(pdfPageCount(merged), 1);
    }

    // 文件裡沒有 \f 時，formFeeds=true 必須退化成單段列印，輸出與關閉此選項時完全一致
    void formFeedWithoutBreaksFallsBackToSingleRange()
    {
        EditorWidget e;
        e.setText(QStringLiteral("no form feeds here\nsecond line"));

        const QString path = outPath(QStringLiteral("formfeed-none.pdf"));
        DocumentPrinter p;
        setupPdf(p, path);
        QVERIFY(p.printDocument(&e, true) != 0);
        QCOMPARE(pdfPageCount(path), 1);
    }

    // \f + $(NB_PAGES)：試排也必須走 FormFeed 分段路徑，否則算出的總頁數會少算換頁
    void formFeedWithPageCountTokenCountsAllSegments()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a\n\f\nb\n\f\nc\n\f\nd"));

        const QString path = outPath(QStringLiteral("formfeed-nbpages.pdf"));
        DocumentPrinter p;
        setupPdf(p, path);
        p.setHeaderTemplate(QStringLiteral("$(CURRENT_PAGE)/$(NB_PAGES)"));
        QVERIFY(p.printDocument(&e, true) != 0);
        QCOMPARE(pdfPageCount(path), 4);
    }

    // ── UpdateDownloader ────────────────────────────────────────────────────

    // 落地檔名與目的目錄（純函式部分，不需伺服器）
    void downloadPathHelpers()
    {
        QCOMPARE(UpdateDownloader::fileNameForUrl(
                     QStringLiteral("https://h/d/v1/macpad-1.0.dmg?sig=x")),
                 QStringLiteral("macpad-1.0.dmg"));
        QVERIFY(UpdateDownloader::fileNameForUrl(QString()).isEmpty());
        QVERIFY(QFileInfo(UpdateDownloader::downloadDir()).isDir());
    }

    // 網址推不出檔名時直接失敗，不得建立無名檔案或發出請求
    void downloadRejectsUrlWithoutFileName()
    {
        UpdateDownloader dl;
        QSignalSpy spy(&dl, &UpdateDownloader::finished);
        dl.start(QString());
        QCOMPARE(spy.count(), 1);              // 同步失敗，不進事件迴圈
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().isEmpty());
        QVERIFY(!args.at(2).toString().isEmpty());
    }

    // 正常下載：檔案落地、位元組數與內容都必須與伺服器送出的完全相同。
    // 只檢查「檔案存在」不足以抓出串流寫檔漏寫尾段的 bug，故逐位元組比對。
    void downloadsFileAndVerifiesContent()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray("MACPAD-PAYLOAD-").repeated(4096);   // ~60 KB，會分多次 readyRead

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        QSignalSpy prog(&dl, &UpdateDownloader::progress);
        dl.start(server.urlFor(QStringLiteral("/pkg.bin")), server.body.size());
        QVERIFY(done.wait(10000));

        const QList<QVariant> args = done.takeFirst();
        QVERIFY2(args.at(0).toBool(), qPrintable(args.at(2).toString()));
        const QString path = args.at(1).toString();
        QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("pkg.bin"));
        QCOMPARE(QFileInfo(path).size(), static_cast<qint64>(server.body.size()));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), server.body);     // 內容完全一致，沒有截斷或錯位
        f.close();
        QVERIFY(prog.count() > 0);              // 進度有回報，UI 才有東西可顯示
    }

    // 目的檔已存在時必須改用序號檔名，絕不覆寫使用者既有檔案
    void downloadNeverOverwritesExistingFile()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray("new-content");

        const QString occupied = artifactPath(QStringLiteral("pkg.bin"));
        QFile pre(occupied);
        QVERIFY(pre.open(QIODevice::WriteOnly));
        pre.write("original-content");
        pre.close();

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/pkg.bin")), server.body.size());
        QVERIFY(done.wait(10000));

        const QList<QVariant> args = done.takeFirst();
        QVERIFY2(args.at(0).toBool(), qPrintable(args.at(2).toString()));
        QCOMPARE(QFileInfo(args.at(1).toString()).fileName(), QStringLiteral("pkg (1).bin"));

        QFile old(occupied);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("original-content"));   // 原檔原封不動
    }

    // HTTP 錯誤：必須回報失敗並刪掉殘檔——留下一個內容是錯誤頁的 .dmg 比沒有更糟
    void downloadReportsHttpError()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = 404;
        server.reason = "Not Found";
        server.body = QByteArray("<html>not found</html>");

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/missing.bin")));
        QVERIFY(done.wait(10000));

        const QList<QVariant> args = done.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().isEmpty());
        QVERIFY(!args.at(2).toString().isEmpty());
        QVERIFY(!QFileInfo::exists(artifactPath(QStringLiteral("missing.bin"))));
    }

    // 大小不符 = 下載被截斷，不可交給使用者安裝；殘檔一併刪除
    void downloadRejectsSizeMismatch()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray("twelve bytes");

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/short.bin")), server.body.size() + 100);
        QVERIFY(done.wait(10000));

        const QList<QVariant> args = done.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().isEmpty());
        QVERIFY(args.at(2).toString().contains(QStringLiteral("下載不完整")));
        QVERIFY(!QFileInfo::exists(artifactPath(QStringLiteral("short.bin"))));
    }

    // 伺服器回 200 但內容為空：仍是失敗（0 位元組的安裝檔沒有意義），且不留空檔
    void downloadRejectsEmptyBody()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body.clear();

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/empty.bin")));
        QVERIFY(done.wait(10000));

        const QList<QVariant> args = done.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(2).toString().contains(QStringLiteral("下載內容為空")));
        QVERIFY(!QFileInfo::exists(artifactPath(QStringLiteral("empty.bin"))));
    }

    // 使用者取消：伺服器宣告了很大的 Content-Length 卻不送完，下載會停在半途；
    // 此時 cancel() 必須讓 finished 回報「已取消」並清掉殘檔。
    void cancelStopsDownloadAndRemovesPartialFile()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray(64 * 1024, 'x');
        server.declaredLength = 64 * 1024 * 1024;   // 宣告 64 MB，實送 64 KB
        server.stall = true;                        // 不關連線，讓下載卡住

        const QString partial = artifactPath(QStringLiteral("stalled.bin"));
        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/stalled.bin")), 64 * 1024 * 1024);

        // 等到殘檔真的有內容，才確定是「下載到一半」被取消，而不是還沒開始就取消。
        // 不用 progress 訊號當條件：Qt 對 downloadProgress 有 100ms 節流，
        // 這種「一次送完就卡住」的情境第一次進度可能整個被吃掉，等它會假性逾時。
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(partial).size() > 0, 10000);
        QVERIFY(done.isEmpty());
        dl.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);

        const QList<QVariant> args = done.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(1).toString().isEmpty());
        QVERIFY(args.at(2).toString().contains(QStringLiteral("已取消")));
        QVERIFY(!QFileInfo::exists(artifactPath(QStringLiteral("stalled.bin"))));
    }

    // 沒有下載進行中時 cancel() 必須是安全的 no-op（UI 可能重複觸發）
    void cancelWithoutActiveDownloadIsHarmless()
    {
        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.cancel();
        QCOMPARE(done.count(), 0);
    }

    // 重入防護：第二次 start() 必須立刻失敗，而不是覆寫掉前一個下載的 reply/檔案
    // （那會讓舊連線的資料寫進新檔案，兩份下載互相汙染）
    void concurrentStartIsRejected()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray(64 * 1024, 'y');
        server.declaredLength = 64 * 1024 * 1024;
        server.stall = true;

        UpdateDownloader dl;
        QSignalSpy done(&dl, &UpdateDownloader::finished);
        dl.start(server.urlFor(QStringLiteral("/stalled.bin")));
        dl.start(server.urlFor(QStringLiteral("/pkg.bin")));   // 第二次：應同步被拒

        QCOMPARE(done.count(), 1);
        const QList<QVariant> args = done.at(0);
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(!args.at(2).toString().isEmpty());
        QVERIFY(!QFileInfo::exists(artifactPath(QStringLiteral("pkg.bin"))));   // 沒開第二個檔

        // 收尾：取消第一個下載。abort() 可能同步就發出 finished，故用 QTRY 而非 wait()
        dl.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 10000);
        QCOMPARE(done.at(1).at(0).toBool(), false);
    }

    // 下載中物件被銷毀（例如關掉更新視窗）：不得崩潰、不得從解構子發訊號、不得留殘檔
    void destroyingMidDownloadLeavesNoPartialFile()
    {
        FakeHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = QByteArray(64 * 1024, 'z');
        server.declaredLength = 64 * 1024 * 1024;
        server.stall = true;

        const QString partial = artifactPath(QStringLiteral("stalled.bin"));
        auto *dl = new UpdateDownloader;
        QSignalSpy done(dl, &UpdateDownloader::finished);
        dl->start(server.urlFor(QStringLiteral("/stalled.bin")));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(partial).size() > 0, 10000);

        delete dl;
        QCOMPARE(done.count(), 0);   // 解構子不得發出 finished（Qt 明文警告的危險操作）
        QVERIFY(!QFileInfo::exists(partial));
    }
};

QTEST_MAIN(TestOutputPipeline)
#include "test_outputpipeline.moc"
