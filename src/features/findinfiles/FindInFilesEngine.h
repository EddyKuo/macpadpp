#pragma once

// FindInFilesEngine — 目錄搜尋核心（FR-013）
// 純邏輯、可單元測試；於 worker thread 執行（CON-008/NFR-005），以 cancel 旗標支援取消。

#include <atomic>

#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::features {

struct FindMatch {
    QString filePath;
    int line;        // 1-based
    int column;      // 1-based（匹配起始）
    QString lineText;
};

struct FindInFilesOptions {
    QString pattern;
    bool regex = false;
    bool caseSensitive = false;
    bool wholeWord = false;
    QStringList fileFilters;   // 如 {"*.cpp","*.h"}；空 = 全部
    bool recursive = true;
    int maxFileBytes = 20 * 1024 * 1024;  // 單檔上限，避免吃爆記憶體
    bool includeHidden = false;  // FR-045：是否納入隱藏檔/隱藏目錄
    // FR-045：排除規則（Notepad++ 風格）。例：
    //   "!*.min.js"    → 排除檔名符合萬用字元的檔案
    //   "!+\\node_modules" 或 "!node_modules\\" → 排除整個子目錄
    QStringList excludeFilters;
};

class FindInFilesEngine {
public:
    // 同步搜尋（呼叫端負責放到 worker thread）。cancel 若非 null 且被設 true 則儘快中止。
    static QVector<FindMatch> search(const QString &rootDir,
                                     const FindInFilesOptions &opts,
                                     std::atomic<bool> *cancel = nullptr);

    // 搜尋單一檔案內容（供測試/重用）
    static QVector<FindMatch> searchInText(const QString &filePath,
                                           const QString &content,
                                           const FindInFilesOptions &opts);

    // 在指定的一組明確檔案路徑中搜尋（不遞迴走目錄，opts.recursive/fileFilters 不適用）。
    // 供「Find in Projects」等以外部清單（如 ProjectPanelDock::filePathsForProject）
    // 限定搜尋範圍的情境使用。opts.excludeFilters/includeHidden 仍套用（以每個檔案的檔名判斷）。
    static QVector<FindMatch> searchInFiles(const QStringList &filePaths,
                                            const FindInFilesOptions &opts,
                                            std::atomic<bool> *cancel = nullptr);

    struct ReplaceResult {
        int filesChanged = 0;
        int replacements = 0;
    };
    // 在目錄中取代並寫回檔案（FR-013 取代）。cancel 可中止。
    static ReplaceResult replaceInFiles(const QString &rootDir,
                                        const FindInFilesOptions &opts,
                                        const QString &replacement,
                                        std::atomic<bool> *cancel = nullptr);

    // 對單一內容取代，回傳新內容與取代次數（供測試）
    static QString replaceInText(const QString &content, const FindInFilesOptions &opts,
                                 const QString &replacement, int *count);

    // FR-045：判斷檔案是否應被排除。relativePath 為相對 rootDir 的路徑（以 '/' 分隔）。
    static bool isExcluded(const QString &relativePath, const QString &fileName,
                           const QStringList &excludeFilters);
};

}  // namespace macpad::features
