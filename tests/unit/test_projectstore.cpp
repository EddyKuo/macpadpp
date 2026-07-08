// 單元測試：ProjectStore（projects.json）round-trip、缺檔預設、向後相容、kMaxProjects
#include <QtTest>
#include <QStandardPaths>
#include <QFile>

#include "persistence/AppPaths.h"
#include "persistence/ProjectStore.h"

using namespace macpad::persistence;

class TestProjectStore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        QFile::remove(AppPaths::filePath(QStringLiteral("projects.json")));
    }

    // 缺檔 → 空 workspace（不崩潰）
    void emptyWhenMissing()
    {
        const ProjectWorkspace ws = ProjectStore::load();
        QVERIFY(ws.projects.isEmpty());
    }

    // 巢狀樹（folder→file）完整 round-trip
    void roundtripNestedTree()
    {
        ProjectWorkspace in;
        Project p;
        p.name = QStringLiteral("Proj A");

        ProjectNode folder;
        folder.type = ProjectNodeType::Folder;
        folder.name = QStringLiteral("src");

        ProjectNode file;
        file.type = ProjectNodeType::File;
        file.name = QStringLiteral("main.cpp");
        file.path = QStringLiteral("/tmp/proj/src/main.cpp");
        folder.children.append(file);

        ProjectNode rootFile;
        rootFile.type = ProjectNodeType::File;
        rootFile.name = QStringLiteral("README.md");
        rootFile.path = QStringLiteral("/tmp/proj/README.md");

        p.roots.append(folder);
        p.roots.append(rootFile);
        in.projects.append(p);

        QVERIFY(ProjectStore::save(in));

        const ProjectWorkspace out = ProjectStore::load();
        QCOMPARE(out.projects.size(), 1);
        QCOMPARE(out.projects[0].name, QStringLiteral("Proj A"));
        QCOMPARE(out.projects[0].roots.size(), 2);

        const ProjectNode &f = out.projects[0].roots[0];
        QCOMPARE(f.type, ProjectNodeType::Folder);
        QCOMPARE(f.name, QStringLiteral("src"));
        QCOMPARE(f.children.size(), 1);
        QCOMPARE(f.children[0].type, ProjectNodeType::File);
        QCOMPARE(f.children[0].path, QStringLiteral("/tmp/proj/src/main.cpp"));

        const ProjectNode &rf = out.projects[0].roots[1];
        QCOMPARE(rf.type, ProjectNodeType::File);
        QCOMPARE(rf.path, QStringLiteral("/tmp/proj/README.md"));
    }

    // 多個 project 保序 round-trip
    void multipleProjects()
    {
        ProjectWorkspace in;
        for (int i = 0; i < ProjectStore::kMaxProjects; ++i) {
            Project p;
            p.name = QStringLiteral("P%1").arg(i);
            in.projects.append(p);
        }
        QVERIFY(ProjectStore::save(in));

        const ProjectWorkspace out = ProjectStore::load();
        QCOMPARE(out.projects.size(), ProjectStore::kMaxProjects);
        for (int i = 0; i < ProjectStore::kMaxProjects; ++i)
            QCOMPARE(out.projects[i].name, QStringLiteral("P%1").arg(i));
    }
};

QTEST_APPLESS_MAIN(TestProjectStore)
#include "test_projectstore.moc"
