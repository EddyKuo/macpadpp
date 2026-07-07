#include "ui/StyleConfiguratorDialog.h"

#include "core/LexerFactory.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <Qsci/qscilexer.h>

namespace macpad::ui {

static void paintSwatch(QLabel *lbl, const QColor &c)
{
    lbl->setAutoFillBackground(true);
    lbl->setMinimumSize(48, 20);
    lbl->setFrameShape(QFrame::Box);
    QPalette p = lbl->palette();
    p.setColor(QPalette::Window, c.isValid() ? c : QColor(Qt::transparent));
    lbl->setPalette(p);
    lbl->setText(c.isValid() ? c.name().toUpper() : QStringLiteral("—"));
    lbl->setAlignment(Qt::AlignCenter);
}

StyleConfiguratorDialog::StyleConfiguratorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Style Configurator"));
    resize(560, 460);
    m_cfg = macpad::persistence::StyleStore::load();

    auto *root = new QVBoxLayout(this);

    // 全域字型
    auto *fontBox = new QGroupBox(tr("Global Font"), this);
    auto *fontForm = new QFormLayout(fontBox);
    m_font = new QFontComboBox(this);
    m_font->setFontFilters(QFontComboBox::MonospacedFonts);
    if (!m_cfg.fontFamily.isEmpty())
        m_font->setCurrentFont(QFont(m_cfg.fontFamily));
    m_size = new QSpinBox(this);
    m_size->setRange(0, 48);
    m_size->setSpecialValueText(tr("(default)"));
    m_size->setValue(m_cfg.fontSize);
    fontForm->addRow(tr("Family:"), m_font);
    fontForm->addRow(tr("Size:"), m_size);
    root->addWidget(fontBox);

    // 語言 + style 清單 + 顏色
    auto *mid = new QHBoxLayout();
    auto *left = new QVBoxLayout();
    m_language = new QComboBox(this);
    for (const auto &e : macpad::core::LexerFactory::languages()) {
        if (e.key.isEmpty())
            continue;  // 跳過 Plain Text
        m_language->addItem(e.displayName, e.key);
    }
    left->addWidget(new QLabel(tr("Language:"), this));
    left->addWidget(m_language);
    m_styles = new QListWidget(this);
    left->addWidget(m_styles, 1);
    mid->addLayout(left, 1);

    auto *right = new QVBoxLayout();
    auto *fgRow = new QHBoxLayout();
    m_fgSwatch = new QLabel(this);
    m_fgBtn = new QPushButton(tr("Foreground…"), this);
    fgRow->addWidget(m_fgSwatch);
    fgRow->addWidget(m_fgBtn);
    auto *bgRow = new QHBoxLayout();
    m_bgSwatch = new QLabel(this);
    m_bgBtn = new QPushButton(tr("Background…"), this);
    bgRow->addWidget(m_bgSwatch);
    bgRow->addWidget(m_bgBtn);
    m_bold = new QCheckBox(tr("Bold"), this);
    m_italic = new QCheckBox(tr("Italic"), this);
    right->addWidget(new QLabel(tr("Style colors:"), this));
    right->addLayout(fgRow);
    right->addLayout(bgRow);
    right->addWidget(m_bold);
    right->addWidget(m_italic);
    right->addStretch(1);
    mid->addLayout(right, 1);
    root->addLayout(mid, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &StyleConfiguratorDialog::apply);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_language, &QComboBox::currentIndexChanged, this,
            &StyleConfiguratorDialog::onLanguageChanged);
    connect(m_styles, &QListWidget::currentRowChanged, this,
            &StyleConfiguratorDialog::onStyleChanged);
    connect(m_fgBtn, &QPushButton::clicked, this, &StyleConfiguratorDialog::pickForeground);
    connect(m_bgBtn, &QPushButton::clicked, this, &StyleConfiguratorDialog::pickBackground);
    connect(m_bold, &QCheckBox::toggled, this, &StyleConfiguratorDialog::onBoldItalicChanged);
    connect(m_italic, &QCheckBox::toggled, this, &StyleConfiguratorDialog::onBoldItalicChanged);

    if (m_language->count() > 0)
        onLanguageChanged();
}

StyleConfiguratorDialog::~StyleConfiguratorDialog()
{
    delete m_lexer;
}

void StyleConfiguratorDialog::onLanguageChanged()
{
    m_langKey = m_language->currentData().toString();
    delete m_lexer;
    m_lexer = macpad::core::LexerFactory::createForLanguage(m_langKey, nullptr);
    m_langName = m_lexer ? QString::fromLatin1(m_lexer->language()) : m_langKey;

    m_styles->clear();
    if (m_lexer) {
        for (int i = 0; i < 128; ++i) {
            const QString desc = m_lexer->description(i);
            if (desc.isEmpty())
                continue;
            auto *item = new QListWidgetItem(QStringLiteral("%1  —  %2").arg(i).arg(desc));
            item->setData(Qt::UserRole, i);
            m_styles->addItem(item);
        }
    }
    if (m_styles->count() > 0)
        m_styles->setCurrentRow(0);
}

macpad::persistence::StyleOverride *StyleConfiguratorDialog::currentOverride(bool create)
{
    if (!m_styles->currentItem())
        return nullptr;
    const int style = m_styles->currentItem()->data(Qt::UserRole).toInt();
    auto &list = m_cfg.byLang[m_langName];
    for (auto &ov : list)
        if (ov.style == style)
            return &ov;
    if (!create)
        return nullptr;
    macpad::persistence::StyleOverride ov;
    ov.style = style;
    list.append(ov);
    return &m_cfg.byLang[m_langName].last();
}

void StyleConfiguratorDialog::refreshSwatches()
{
    if (!m_styles->currentItem() || !m_lexer)
        return;
    const int style = m_styles->currentItem()->data(Qt::UserRole).toInt();
    const macpad::persistence::StyleOverride *ov = currentOverride(false);
    const QColor fg = (ov && !ov->fg.isEmpty()) ? QColor(ov->fg) : m_lexer->color(style);
    const QColor bg = (ov && !ov->bg.isEmpty()) ? QColor(ov->bg) : m_lexer->paper(style);
    paintSwatch(m_fgSwatch, fg);
    paintSwatch(m_bgSwatch, bg);
    const QFont f = m_lexer->font(style);
    QSignalBlocker b1(m_bold), b2(m_italic);
    m_bold->setChecked(ov ? ov->bold : f.bold());
    m_italic->setChecked(ov ? ov->italic : f.italic());
}

void StyleConfiguratorDialog::onStyleChanged()
{
    refreshSwatches();
}

void StyleConfiguratorDialog::pickForeground()
{
    if (!m_styles->currentItem())
        return;
    const int style = m_styles->currentItem()->data(Qt::UserRole).toInt();
    const QColor cur = m_lexer ? m_lexer->color(style) : QColor(Qt::black);
    const QColor c = QColorDialog::getColor(cur, this, tr("Foreground Color"));
    if (!c.isValid())
        return;
    currentOverride(true)->fg = c.name();
    refreshSwatches();
}

void StyleConfiguratorDialog::pickBackground()
{
    if (!m_styles->currentItem())
        return;
    const int style = m_styles->currentItem()->data(Qt::UserRole).toInt();
    const QColor cur = m_lexer ? m_lexer->paper(style) : QColor(Qt::white);
    const QColor c = QColorDialog::getColor(cur, this, tr("Background Color"));
    if (!c.isValid())
        return;
    currentOverride(true)->bg = c.name();
    refreshSwatches();
}

void StyleConfiguratorDialog::onBoldItalicChanged()
{
    if (auto *ov = currentOverride(true)) {
        ov->bold = m_bold->isChecked();
        ov->italic = m_italic->isChecked();
    }
}

void StyleConfiguratorDialog::apply()
{
    m_cfg.fontFamily = m_font->currentFont().family();
    m_cfg.fontSize = m_size->value();
    macpad::persistence::StyleStore::save(m_cfg);
    accept();
}

}  // namespace macpad::ui
