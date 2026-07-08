#pragma once

// ProjectStore — projects.json：Project Panel 工作區模型（Notepad++ 風格「Project」）
// 一個 workspace 內可有多個 project（Notepad++ 最多 3 個面板，這裡以「多個 project」模擬），
// 每個 project 是一棵 folder/file 節點樹，節點僅存磁碟路徑參照（不複製檔案內容）。
// 檔案損毀時 load 回傳空 workspace（不崩潰），缺欄位一律回退預設值（向後相容）。

#include <QString>
#include <QVector>

namespace macpad::persistence {

enum class ProjectNodeType {
    Folder,  // 虛擬分組節點（Notepad++「virtual folder」）或磁碟目錄參照
    File     // 磁碟檔案參照
};

struct ProjectNode {
    ProjectNodeType type = ProjectNodeType::Folder;
    QString name;              // 顯示名稱
    QString path;              // 磁碟路徑（File 節點必填；Folder 節點若為磁碟目錄參照則填，虛擬分組留空）
    QVector<ProjectNode> children;  // 僅 Folder 節點使用
};

struct Project {
    QString name;
    QVector<ProjectNode> roots;  // 頂層節點（folder/file 皆可）
};

struct ProjectWorkspace {
    QVector<Project> projects;
};

class ProjectStore {
public:
    static ProjectWorkspace load();
    static bool save(const ProjectWorkspace &workspace);

    static constexpr int kSchemaVersion = 1;
    // Notepad++ 相容：最多 3 個 project 面板（此實作為單一 workspace 內最多 3 個 project）
    static constexpr int kMaxProjects = 3;
};

}  // namespace macpad::persistence
