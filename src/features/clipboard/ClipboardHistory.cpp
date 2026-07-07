#include "features/clipboard/ClipboardHistory.h"

namespace macpad::features {

void ClipboardHistory::add(const QString &text)
{
    if (text.isEmpty())
        return;
    m_items.removeAll(text);   // 去重
    m_items.prepend(text);     // 置頂（最新在前）
    while (m_items.size() > m_max)
        m_items.removeLast();
}

}  // namespace macpad::features
