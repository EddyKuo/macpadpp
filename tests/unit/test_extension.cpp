// 單元測試：擴充協定生命週期與 dogfood 掛載（FR-035）
#include <QtTest>
#include <QStringList>

#include "extension/IExtension.h"
#include "extension/ExtensionRegistry.h"
#include "extension/builtin/WordCountExtension.h"

using namespace macpad::extension;

// 假宿主：記錄協定呼叫，供斷言（不需真實 MainWindow）
class FakeHost : public IHostServices {
public:
    macpad::core::EditorWidget *activeEditor() override { return nullptr; }
    void addMenuAction(const QString &menu, const QString &text,
                       std::function<void()> cb) override
    {
        menus << menu;
        texts << text;
        callbacks.push_back(std::move(cb));
    }
    void showStatusMessage(const QString &msg, int) override { lastStatus = msg; }
    QWidget *hostWindow() override { return nullptr; }

    QStringList menus;
    QStringList texts;
    std::vector<std::function<void()>> callbacks;
    QString lastStatus;
};

class TestExtension : public QObject {
    Q_OBJECT
private slots:
    void wordCountCapabilities()
    {
        WordCountExtension ext;
        const auto caps = ext.capabilities();
        QCOMPARE(caps.id, QStringLiteral("builtin.wordcount"));
        QVERIFY(!caps.name.isEmpty());
        QVERIFY(!caps.version.isEmpty());
    }

    void registryLoadMountsThroughProtocol()
    {
        FakeHost host;
        {
            ExtensionRegistry registry(&host);
            registry.load(std::make_unique<WordCountExtension>());
            QCOMPARE(registry.count(), std::size_t(1));
            // dogfood：onLoad 應透過協定在 Edit 選單掛一個動作（FR-035 AC2）
            QVERIFY(host.menus.contains(QStringLiteral("Edit")));
            QVERIFY(host.texts.contains(QStringLiteral("Word Count")));
        }
        // registry 解構 → unloadAll，不崩潰
    }

    void unloadAllClears()
    {
        FakeHost host;
        ExtensionRegistry registry(&host);
        registry.load(std::make_unique<WordCountExtension>());
        QCOMPARE(registry.count(), std::size_t(1));
        registry.unloadAll();
        QCOMPARE(registry.count(), std::size_t(0));
    }

    void nullExtensionIgnored()
    {
        FakeHost host;
        ExtensionRegistry registry(&host);
        registry.load(nullptr);
        QCOMPARE(registry.count(), std::size_t(0));
    }
};

QTEST_APPLESS_MAIN(TestExtension)
#include "test_extension.moc"
