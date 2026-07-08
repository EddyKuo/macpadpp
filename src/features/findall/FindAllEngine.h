#pragma once

// FindAllEngine — 單一文件內「Find All」搜尋核心（FR-058）
// 純邏輯、可單元測試；對單一文件內容做搜尋，回傳所有符合位置。

#include <QString>
#include <QVector>

namespace macpad::features {

struct FindAllMatch {
    int docId = -1;
    QString title;
    int line = 0;      // 1-based
    int column = 0;    // 1-based（匹配起始）
    QString lineText;
};

class FindAllEngine {
public:
    // 在單一文件內容中搜尋所有符合 pattern 的位置。
    // docId/title 僅供標示來源文件，直接回填到結果中。
    static QVector<FindAllMatch> searchInText(int docId, const QString &title,
                                              const QString &content, const QString &pattern,
                                              bool regex, bool cs, bool wholeWord);
};

}  // namespace macpad::features
