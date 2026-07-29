// 單元測試：PluginStore 擴充啟用/停用狀態持久化（複刻 Notepad++ Plugins Admin）
#include <QtTest>
#include <QStandardPaths>

#include "persistence/PluginStore.h"

using macpad::persistence::PluginStore;

class TestPluginStore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // 隔離設定目錄，避免污染使用者的實際 plugins.json
        QStandardPaths::setTestModeEnabled(true);
        PluginStore::setDisabledIds({});
    }

    void cleanup() { PluginStore::setDisabledIds({}); }

    // 預設全部啟用：沒有設定檔時不得誤判任何擴充為停用
    void defaultsToAllEnabled()
    {
        QVERIFY(PluginStore::disabledIds().isEmpty());
        QVERIFY(PluginStore::isEnabled(QStringLiteral("builtin.wordcount")));
        QVERIFY(PluginStore::isEnabled(QStringLiteral("anything.at.all")));
    }

    void disabledIdsRoundTrip()
    {
        const QSet<QString> want = {QStringLiteral("builtin.wordcount"),
                                    QStringLiteral("builtin.markdownpreview")};
        QVERIFY(PluginStore::setDisabledIds(want));
        QCOMPARE(PluginStore::disabledIds(), want);

        QVERIFY(!PluginStore::isEnabled(QStringLiteral("builtin.wordcount")));
        // 未列入者仍為啟用——這正是「只記停用項」的重點：
        // 日後新增的內建擴充不會因為設定檔沒跟上而被誤停用。
        QVERIFY(PluginStore::isEnabled(QStringLiteral("builtin.future")));
    }

    // 重新啟用（清空）必須真的寫回，而不是留著舊集合
    void reEnableClearsState()
    {
        QVERIFY(PluginStore::setDisabledIds({QStringLiteral("builtin.wordcount")}));
        QVERIFY(!PluginStore::isEnabled(QStringLiteral("builtin.wordcount")));

        QVERIFY(PluginStore::setDisabledIds({}));
        QVERIFY(PluginStore::disabledIds().isEmpty());
        QVERIFY(PluginStore::isEnabled(QStringLiteral("builtin.wordcount")));
    }

    // 空字串/空白項不得被當成有效 id 存進集合（否則會產生一個永遠停用不了的幽靈項）
    void ignoresBlankIds()
    {
        QVERIFY(PluginStore::setDisabledIds({QStringLiteral("  "), QString(),
                                             QStringLiteral("builtin.wordcount")}));
        const QSet<QString> got = PluginStore::disabledIds();
        QCOMPARE(got.size(), 1);
        QVERIFY(got.contains(QStringLiteral("builtin.wordcount")));
    }
};

QTEST_GUILESS_MAIN(TestPluginStore)
#include "test_pluginstore.moc"
