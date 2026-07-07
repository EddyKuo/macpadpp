#include "ui/CharacterPanel.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::ui {

CharacterPanel::CharacterPanel(QWidget *parent)
    : QDockWidget(tr("Character Panel"), parent)
{
    setObjectName(QStringLiteral("CharacterPanelDock"));

    m_table = new QTableWidget(256, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Value"), tr("Hex"), tr("Character")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);

    for (int i = 0; i < 256; ++i) {
        auto *dec = new QTableWidgetItem(QString::number(i));
        // 僅將十六進位數字轉大寫，保留小寫 "0x" 前綴（避免變成 "0X"）
        auto *hex = new QTableWidgetItem(QStringLiteral("0x%1")
                                             .arg(QString::number(i, 16).rightJustified(2, QChar('0')).toUpper()));
        // 0..31 為控制字元，顯示佔位符；32+ 顯示實際字元
        QString glyph = (i >= 32 && i != 127) ? QString(QChar(i)) : QStringLiteral("·");
        auto *chr = new QTableWidgetItem(glyph);
        m_table->setItem(i, 0, dec);
        m_table->setItem(i, 1, hex);
        m_table->setItem(i, 2, chr);
    }
    m_table->resizeColumnsToContents();

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 32 && row != 127)
            emit charChosen(QString(QChar(row)));
    });

    setWidget(m_table);
}

}  // namespace macpad::ui
