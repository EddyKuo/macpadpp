#include "persistence/ProjectStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace macpad::persistence {

static QString projectsPath() { return AppPaths::filePath(QStringLiteral("projects.json")); }

static QJsonObject nodeToJson(const ProjectNode &node)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"),
             node.type == ProjectNodeType::File ? QStringLiteral("file") : QStringLiteral("folder"));
    o.insert(QStringLiteral("name"), node.name);
    o.insert(QStringLiteral("path"), node.path);
    QJsonArray children;
    for (const ProjectNode &c : node.children)
        children.append(nodeToJson(c));
    o.insert(QStringLiteral("children"), children);
    return o;
}

static ProjectNode jsonToNode(const QJsonObject &o)
{
    ProjectNode node;
    node.type = (o.value(QStringLiteral("type")).toString() == QStringLiteral("file"))
                    ? ProjectNodeType::File
                    : ProjectNodeType::Folder;
    node.name = o.value(QStringLiteral("name")).toString();
    node.path = o.value(QStringLiteral("path")).toString();
    const QJsonArray children = o.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &v : children)
        node.children.push_back(jsonToNode(v.toObject()));
    return node;
}

static QJsonObject projectToJson(const Project &p)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), p.name);
    QJsonArray roots;
    for (const ProjectNode &n : p.roots)
        roots.append(nodeToJson(n));
    o.insert(QStringLiteral("roots"), roots);
    return o;
}

static Project jsonToProject(const QJsonObject &o)
{
    Project p;
    p.name = o.value(QStringLiteral("name")).toString();
    const QJsonArray roots = o.value(QStringLiteral("roots")).toArray();
    for (const QJsonValue &v : roots)
        p.roots.push_back(jsonToNode(v.toObject()));
    return p;
}

ProjectWorkspace ProjectStore::load()
{
    ProjectWorkspace ws;
    const QJsonObject root = JsonFile::load(projectsPath());
    if (root.isEmpty())
        return ws;
    const QJsonArray projects = root.value(QStringLiteral("projects")).toArray();
    for (const QJsonValue &v : projects)
        ws.projects.push_back(jsonToProject(v.toObject()));
    return ws;
}

bool ProjectStore::save(const ProjectWorkspace &workspace)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), ProjectStore::kSchemaVersion);
    QJsonArray projects;
    for (const Project &p : workspace.projects)
        projects.append(projectToJson(p));
    root.insert(QStringLiteral("projects"), projects);
    return JsonFile::save(projectsPath(), root);
}

}  // namespace macpad::persistence
