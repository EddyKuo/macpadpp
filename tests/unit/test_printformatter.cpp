// 單元測試：列印頁首/頁尾變數展開（複刻 Notepad++ Print 偏好的 header/footer 變數）
#include <QtTest>

#include "features/print/PrintFormatter.h"

using macpad::features::PrintContext;
using macpad::features::PrintFormatter;

class TestPrintFormatter : public QObject {
    Q_OBJECT
private slots:
    void expandsPathVariables()
    {
        PrintContext ctx;
        ctx.filePath = QStringLiteral("/home/eddy/docs/report.final.txt");

        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(FULL_CURRENT_PATH)"), ctx),
                 QStringLiteral("/home/eddy/docs/report.final.txt"));
        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(FILE_NAME)"), ctx),
                 QStringLiteral("report.final.txt"));
        // completeBaseName：最後一個點之前全部（report.final），非第一個點之前
        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(NAME_PART)"), ctx),
                 QStringLiteral("report.final"));
        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(EXT_PART)"), ctx),
                 QStringLiteral("txt"));
        // 目錄取的是「路徑本身的目錄部分」，不對目前工作目錄解析——否則 Windows 上
        // 這種無磁碟代號的路徑會被補成 D:/home/eddy/docs（此測試曾在 Windows CI 抓到）。
        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(CURRENT_DIRECTORY)"), ctx),
                 QStringLiteral("/home/eddy/docs"));
    }

    // 未命名文件：路徑類變數展開為空字串，不得印出 "." 之類的殘值
    void untitledDocumentYieldsEmptyPathVars()
    {
        PrintContext ctx;   // filePath 為空
        for (const QString &v : {QStringLiteral("$(FULL_CURRENT_PATH)"),
                                 QStringLiteral("$(CURRENT_DIRECTORY)"),
                                 QStringLiteral("$(FILE_NAME)"),
                                 QStringLiteral("$(NAME_PART)"),
                                 QStringLiteral("$(EXT_PART)")}) {
            QVERIFY2(PrintFormatter::expand(v, ctx).isEmpty(), qPrintable(v));
        }
    }

    void expandsPageAndDateVariables()
    {
        PrintContext ctx;
        ctx.dateText = QStringLiteral("2026-07-29");
        ctx.timeText = QStringLiteral("14:30");
        ctx.pageNumber = 3;
        ctx.pageCount = 10;

        QCOMPARE(PrintFormatter::expand(
                     QStringLiteral("$(CURRENT_DATE) $(CURRENT_TIME) - $(CURRENT_PAGE)/$(NB_PAGES)"),
                     ctx),
                 QStringLiteral("2026-07-29 14:30 - 3/10"));
    }

    // 總頁數未知時展開為空，而不是印出 0 —— 紙上不該出現看似真值的假數字
    void unknownPageCountExpandsToEmpty()
    {
        PrintContext ctx;
        ctx.pageNumber = 2;
        ctx.pageCount = 0;
        QCOMPARE(PrintFormatter::expand(QStringLiteral("[$(CURRENT_PAGE)/$(NB_PAGES)]"), ctx),
                 QStringLiteral("[2/]"));
    }

    // 未知變數原樣保留：靜默吞掉會讓使用者以為打錯字的樣板生效了
    void unknownVariableIsPreserved()
    {
        PrintContext ctx;
        QCOMPARE(PrintFormatter::expand(QStringLiteral("a $(NO_SUCH_VAR) b"), ctx),
                 QStringLiteral("a $(NO_SUCH_VAR) b"));
    }

    // 替換結果不得被再次當成變數解析（檔名恰好含 $(...) 時的注入情境）
    void expansionIsSinglePass()
    {
        PrintContext ctx;
        ctx.filePath = QStringLiteral("/tmp/$(CURRENT_PAGE).txt");
        ctx.pageNumber = 7;
        // FILE_NAME 展開出的 "$(CURRENT_PAGE).txt" 必須原樣輸出，不可再被替換成 7
        QCOMPARE(PrintFormatter::expand(QStringLiteral("$(FILE_NAME)"), ctx),
                 QStringLiteral("$(CURRENT_PAGE).txt"));
    }

    void emptyTemplateYieldsEmpty()
    {
        QCOMPARE(PrintFormatter::expand(QString(), PrintContext{}), QString());
    }
};

QTEST_APPLESS_MAIN(TestPrintFormatter)
#include "test_printformatter.moc"
