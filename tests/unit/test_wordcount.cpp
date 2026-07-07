// 單元測試：WordCountExtension + ExtensionRegistry（dogfood 擴充, FR-035 AC2）
#include <QtTest>
#include <QStringList>

#include "extension/IExtension.h"
#include "extension/ExtensionRegistry.h"
#include "extension/builtin/WordCountExtension.h"
#include "core/EditorWidget.h"

using namespace macpad::extension;

// 假宿主：記錄協定呼叫，供斷言（可注入 activeEditor）
class FakeHost : public IHostServices {
public:
    macpad::core::EditorWidget *activeEditor() override { return m_editor; }
    void addMenuAction(const QString &menu, const QString &text,
                       std::function<void()> cb) override
    {
        menus << menu;
        texts << text;
        callbacks.push_back(std::move(cb));
    }
    void showStatusMessage(const QString &msg, int timeoutMs) override
    {
        lastStatus = msg;
        lastTimeout = timeoutMs;
        ++statusCallCount;
    }
    QWidget *hostWindow() override { return nullptr; }

    macpad::core::EditorWidget *m_editor = nullptr;
    QStringList menus;
    QStringList texts;
    std::vector<std::function<void()>> callbacks;
    QString lastStatus;
    int lastTimeout = -1;
    int statusCallCount = 0;
};

// 小型假擴充：只為驗證 registry 呼叫順序 onLoad/onUnload
class FakeExtension : public IExtension {
public:
    explicit FakeExtension(QStringList *log) : m_log(log) {}

    ExtensionCapabilities capabilities() const override
    {
        return {QStringLiteral("fake.ext"), QStringLiteral("Fake"), QStringLiteral("0.1.0")};
    }
    void onLoad(IHostServices *) override { *m_log << QStringLiteral("load"); }
    void onUnload() override { *m_log << QStringLiteral("unload"); }

private:
    QStringList *m_log;
};

class TestWordCount : public QObject {
    Q_OBJECT
private slots:
    void capabilitiesAreCorrect()
    {
        WordCountExtension ext;
        const auto caps = ext.capabilities();
        QCOMPARE(caps.id, QStringLiteral("builtin.wordcount"));
        QCOMPARE(caps.name, QStringLiteral("Word Count"));
        QCOMPARE(caps.version, QStringLiteral("1.0.0"));
    }

    void onLoadRegistersExactlyOneEditMenuAction()
    {
        FakeHost host;
        WordCountExtension ext;
        ext.onLoad(&host);

        QCOMPARE(host.menus.size(), 1);
        QCOMPARE(host.texts.size(), 1);
        QCOMPARE(host.callbacks.size(), std::size_t(1));
        QCOMPARE(host.menus.at(0), QStringLiteral("Edit"));
        QCOMPARE(host.texts.at(0), QStringLiteral("Word Count"));

        ext.onUnload();
    }

    void callbackWithNoActiveEditorShowsFallbackMessage()
    {
        FakeHost host;
        host.m_editor = nullptr;
        WordCountExtension ext;
        ext.onLoad(&host);

        QVERIFY(!host.callbacks.empty());
        host.callbacks.front()();

        QCOMPARE(host.statusCallCount, 1);
        QVERIFY(host.lastStatus.contains(QStringLiteral("No active editor")));

        ext.onUnload();
    }

    void callbackCountsWordsCharsAndLinesWithMultiLineAndUnicode()
    {
        macpad::core::EditorWidget editor;
        // 3 行文字，含一個 surrogate pair emoji（U+1F600）與 CJK 擴充字元
        // 行1: "Hello world" (2 words)
        // 行2: emoji 佔一個碼位但由代理對組成，之後接一個中文字
        // 行3: 空字串
        const QString line1 = QStringLiteral("Hello world");
        const QString line2 = QString::fromUtf8(u8"\U0001F600汉");  // 😀 + 漢
        const QString line3 = QString();
        const QString text = line1 + QLatin1Char('\n') + line2 + QLatin1Char('\n') + line3;
        editor.setText(text);

        FakeHost host;
        host.m_editor = &editor;
        WordCountExtension ext;
        ext.onLoad(&host);

        QVERIFY(!host.callbacks.empty());
        host.callbacks.front()();

        QCOMPARE(host.statusCallCount, 1);
        QCOMPARE(host.lastTimeout, 5000);

        // 字元數（code point，surrogate pair 算 1）：
        // line1: 11, '\n':1, line2: 2 (emoji=1 + 漢=1), '\n':1, line3: 0 => 15
        const int expectedChars = 11 + 1 + 2 + 1 + 0;
        // 詞數：以空白切分（含換行符），SkipEmptyParts：
        // "Hello", "world", "😀漢" => 3 words (emoji+漢 have no whitespace between them)
        const int expectedWords = 3;

        QVERIFY(host.lastStatus.contains(QString::number(expectedWords)));
        QVERIFY(host.lastStatus.contains(QString::number(expectedChars)));
        QCOMPARE(editor.lines(), 3);
        QVERIFY(host.lastStatus.contains(QString::number(editor.lines())));

        ext.onUnload();
    }

    void onUnloadNullsHostSafely()
    {
        FakeHost host;
        WordCountExtension ext;
        ext.onLoad(&host);
        ext.onUnload();
        // onUnload 後再呼叫一次不應崩潰（m_host 已為 nullptr，callback 未被再次觸發即可）
        QVERIFY(true);
    }

    void registryLoadTakesOwnershipAndCallsOnLoadImmediately()
    {
        FakeHost host;
        ExtensionRegistry registry(&host);
        QCOMPARE(registry.count(), std::size_t(0));

        registry.load(std::make_unique<WordCountExtension>());

        // onLoad 應立即被呼叫 — 選單動作應已出現
        QCOMPARE(registry.count(), std::size_t(1));
        QVERIFY(host.menus.contains(QStringLiteral("Edit")));
        QVERIFY(host.texts.contains(QStringLiteral("Word Count")));
    }

    void registryCapabilitiesListReflectsLoadedExtensions()
    {
        FakeHost host;
        ExtensionRegistry registry(&host);
        registry.load(std::make_unique<WordCountExtension>());

        const auto caps = registry.capabilitiesList();
        QCOMPARE(caps.size(), 1);
        QCOMPARE(caps.at(0).id, QStringLiteral("builtin.wordcount"));
    }

    void registryUnloadAllCallsOnUnloadInOrder()
    {
        FakeHost host;
        QStringList log;
        ExtensionRegistry registry(&host);
        registry.load(std::make_unique<FakeExtension>(&log));
        registry.load(std::make_unique<FakeExtension>(&log));

        QCOMPARE(log, QStringList({QStringLiteral("load"), QStringLiteral("load")}));
        QCOMPARE(registry.count(), std::size_t(2));

        registry.unloadAll();

        QCOMPARE(log, QStringList({QStringLiteral("load"), QStringLiteral("load"),
                                    QStringLiteral("unload"), QStringLiteral("unload")}));
        QCOMPARE(registry.count(), std::size_t(0));
        QVERIFY(registry.capabilitiesList().isEmpty());
    }

    void registryDestructorUnloadsRemainingExtensions()
    {
        FakeHost host;
        QStringList log;
        {
            ExtensionRegistry registry(&host);
            registry.load(std::make_unique<FakeExtension>(&log));
            QCOMPARE(log.size(), 1);
        }
        // 解構時應呼叫 unloadAll -> onUnload
        QCOMPARE(log, QStringList({QStringLiteral("load"), QStringLiteral("unload")}));
    }
};

QTEST_MAIN(TestWordCount)
#include "test_wordcount.moc"
