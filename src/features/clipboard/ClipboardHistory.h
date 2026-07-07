#pragma once

// ClipboardHistory — 剪貼簿歷史模型（FR-029）
// 純邏輯、可單元測試：新增（去重置頂）、上限裁切。

#include <QString>
#include <QStringList>

namespace macpad::features {

class ClipboardHistory {
public:
    explicit ClipboardHistory(int maxItems = 30) : m_max(maxItems) {}

    void add(const QString &text);   // 空字串忽略；重複則置頂；超過上限裁切
    const QStringList &items() const { return m_items; }
    void clear() { m_items.clear(); }
    int maxItems() const { return m_max; }

private:
    QStringList m_items;
    int m_max;
};

}  // namespace macpad::features
