#include "ui/ColorPicker.h"

#include <QColorDialog>

#include "persistence/SettingsStore.h"

namespace macpad::ui {

namespace {
// QColorDialog 固定提供 16 個自訂色格（2 列 × 8）
int customColorCount()
{
    return QColorDialog::customCount();
}
}  // namespace

void ColorPicker::restoreCustomColors()
{
    const QStringList saved = macpad::persistence::SettingsStore::load().customColors;
    const int n = qMin(saved.size(), customColorCount());
    for (int i = 0; i < n; ++i) {
        const QColor c(saved.at(i));
        if (c.isValid())
            QColorDialog::setCustomColor(i, c);
    }
}

void ColorPicker::persistCustomColors()
{
    QStringList out;
    const int n = customColorCount();
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out << QColorDialog::customColor(i).name(QColor::HexRgb);

    auto s = macpad::persistence::SettingsStore::load();
    if (s.customColors == out)
        return;   // 沒變就不寫檔，避免每次開關取色器都動到設定檔
    s.customColors = out;
    macpad::persistence::SettingsStore::save(s);
}

QColor ColorPicker::getColor(const QColor &initial, QWidget *parent, const QString &title)
{
    const QColor chosen = QColorDialog::getColor(initial, parent, title);
    persistCustomColors();   // 即使使用者取消，自訂色格的編輯仍要保留
    return chosen;
}

}  // namespace macpad::ui
