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
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <Qsci/qscilexer.h>

#include <iterator>

namespace macpad::ui {

// Global Styles（③b）：語言下拉選單中的特殊項目 key，代表選中的是編輯器全域樣式而非某語言的 lexer。
static const QString kGlobalStylesKey = QStringLiteral("__global_styles__");

// Global Styles 清單列的順序 ↔ GlobalStyles 欄位的對應（member pointer），
// 供 onLanguageChanged()/currentGlobalField() 依目前選取列索引找到對應欄位。
static QString macpad::persistence::GlobalStyles::* const kGlobalFields[] = {
    &macpad::persistence::GlobalStyles::indentGuide,
    &macpad::persistence::GlobalStyles::caretLineBg,
    &macpad::persistence::GlobalStyles::selectionBg,
    &macpad::persistence::GlobalStyles::whitespaceFg,
    &macpad::persistence::GlobalStyles::marginBg,
    &macpad::persistence::GlobalStyles::marginFg,
    &macpad::persistence::GlobalStyles::currentLineNumber,
    &macpad::persistence::GlobalStyles::edgeColor,
    &macpad::persistence::GlobalStyles::bookmarkMargin,
    &macpad::persistence::GlobalStyles::foldMargin,
    &macpad::persistence::GlobalStyles::caretColor,
    &macpad::persistence::GlobalStyles::markColor,
};

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

    // Global override：單一 fg/bg 套用到所有語言（開啟時疊在各語言個別覆寫之上）
    auto *overrideBox = new QGroupBox(tr("Global Override"), this);
    auto *overrideLayout = new QHBoxLayout(overrideBox);
    m_enableGlobalFg = new QCheckBox(tr("Foreground:"), this);
    m_globalFgSwatch = new QLabel(this);
    m_globalFgBtn = new QPushButton(tr("…"), this);
    m_enableGlobalBg = new QCheckBox(tr("Background:"), this);
    m_globalBgSwatch = new QLabel(this);
    m_globalBgBtn = new QPushButton(tr("…"), this);
    overrideLayout->addWidget(m_enableGlobalFg);
    overrideLayout->addWidget(m_globalFgSwatch);
    overrideLayout->addWidget(m_globalFgBtn);
    overrideLayout->addSpacing(16);
    overrideLayout->addWidget(m_enableGlobalBg);
    overrideLayout->addWidget(m_globalBgSwatch);
    overrideLayout->addWidget(m_globalBgBtn);
    overrideLayout->addStretch(1);
    root->addWidget(overrideBox);

    // 語言 + style 清單 + 顏色
    auto *mid = new QHBoxLayout();
    auto *left = new QVBoxLayout();
    m_language = new QComboBox(this);
    m_language->addItem(tr("Global Styles"), kGlobalStylesKey);
    for (const auto &e : macpad::core::LexerFactory::languages()) {
        if (e.key.isEmpty())
            continue;  // 跳過 Plain Text
        m_language->addItem(e.displayName, e.key);
    }
    left->addWidget(new QLabel(tr("Language:"), this));
    left->addWidget(m_language);
    m_styles = new QListWidget(this);
    left->addWidget(m_styles, 1);
    left->addWidget(new QLabel(tr("User ext.:"), this));
    m_userExt = new QLineEdit(this);
    m_userExt->setPlaceholderText(tr("e.g. foo bar baz"));
    left->addWidget(m_userExt);
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
    m_underline = new QCheckBox(tr("Underline"), this);
    right->addWidget(new QLabel(tr("Style colors:"), this));
    right->addLayout(fgRow);
    right->addLayout(bgRow);
    right->addWidget(m_bold);
    right->addWidget(m_italic);
    right->addWidget(m_underline);
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
    connect(m_underline, &QCheckBox::toggled, this, &StyleConfiguratorDialog::onBoldItalicChanged);
    connect(m_userExt, &QLineEdit::editingFinished, this,
            &StyleConfiguratorDialog::onExtensionsEdited);
    connect(m_enableGlobalFg, &QCheckBox::toggled, this,
            &StyleConfiguratorDialog::onGlobalOverrideToggled);
    connect(m_enableGlobalBg, &QCheckBox::toggled, this,
            &StyleConfiguratorDialog::onGlobalOverrideToggled);
    connect(m_globalFgBtn, &QPushButton::clicked, this, &StyleConfiguratorDialog::pickGlobalFg);
    connect(m_globalBgBtn, &QPushButton::clicked, this, &StyleConfiguratorDialog::pickGlobalBg);

    m_enableGlobalFg->setChecked(m_cfg.global.enableGlobalFg);
    m_enableGlobalBg->setChecked(m_cfg.global.enableGlobalBg);
    refreshGlobalOverrideSwatches();

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
    m_lexer = nullptr;
    m_langName.clear();
    m_styles->clear();

    const bool isGlobal = (m_langKey == kGlobalStylesKey);
    // Global Styles 項目每列只有單一顏色（無獨立背景色/粗體/斜體/底線），故停用對應控制項。
    m_bgBtn->setEnabled(!isGlobal);
    m_bold->setEnabled(!isGlobal);
    m_italic->setEnabled(!isGlobal);
    m_underline->setEnabled(!isGlobal);
    m_userExt->setEnabled(!isGlobal);

    if (isGlobal) {
        QSignalBlocker be(m_userExt);
        m_userExt->clear();
        static const QStringList names = {
            tr("Indent Guide"),      tr("Current Line Background"), tr("Selection Background"),
            tr("Whitespace"),        tr("Margin Background"),       tr("Margin Foreground"),
            tr("Current Line Number"), tr("Edge Colour"),           tr("Bookmark Margin"),
            tr("Fold Margin"),       tr("Caret Colour"),            tr("Mark Colour"),
        };
        for (int i = 0; i < names.size(); ++i) {
            auto *item = new QListWidgetItem(names[i]);
            item->setData(Qt::UserRole, i);
            m_styles->addItem(item);
        }
        if (m_styles->count() > 0)
            m_styles->setCurrentRow(0);
        return;
    }

    m_lexer = macpad::core::LexerFactory::createForLanguage(m_langKey, nullptr);
    m_langName = m_lexer ? QString::fromLatin1(m_lexer->language()) : m_langKey;

    {
        QSignalBlocker be(m_userExt);
        m_userExt->setText(m_cfg.userExtensions.value(m_langName));
    }

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
    if (!m_styles->currentItem())
        return;
    if (m_langKey == kGlobalStylesKey) {
        refreshGlobalSwatch();
        return;
    }
    if (!m_lexer)
        return;
    const int style = m_styles->currentItem()->data(Qt::UserRole).toInt();
    const macpad::persistence::StyleOverride *ov = currentOverride(false);
    const QColor fg = (ov && !ov->fg.isEmpty()) ? QColor(ov->fg) : m_lexer->color(style);
    const QColor bg = (ov && !ov->bg.isEmpty()) ? QColor(ov->bg) : m_lexer->paper(style);
    paintSwatch(m_fgSwatch, fg);
    paintSwatch(m_bgSwatch, bg);
    const QFont f = m_lexer->font(style);
    QSignalBlocker b1(m_bold), b2(m_italic), b3(m_underline);
    m_bold->setChecked(ov ? ov->bold : f.bold());
    m_italic->setChecked(ov ? ov->italic : f.italic());
    m_underline->setChecked(ov ? ov->underline : f.underline());
}

void StyleConfiguratorDialog::refreshGlobalSwatch()
{
    // Global Styles 模式：每列只有單一顏色，寫入 m_cfg.global 對應欄位；背景/粗體/斜體控制項已停用。
    const int idx = m_styles->currentItem()->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= int(std::size(kGlobalFields)))
        return;
    const QString value = m_cfg.global.*kGlobalFields[idx];
    const QColor fg = value.isEmpty() ? QColor() : QColor(value);
    paintSwatch(m_fgSwatch, fg);
    paintSwatch(m_bgSwatch, QColor());
    QSignalBlocker b1(m_bold), b2(m_italic), b3(m_underline);
    m_bold->setChecked(false);
    m_italic->setChecked(false);
    m_underline->setChecked(false);
}

void StyleConfiguratorDialog::onStyleChanged()
{
    refreshSwatches();
}

void StyleConfiguratorDialog::pickForeground()
{
    if (!m_styles->currentItem())
        return;
    if (m_langKey == kGlobalStylesKey) {
        const int idx = m_styles->currentItem()->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= int(std::size(kGlobalFields)))
            return;
        const QString cur = m_cfg.global.*kGlobalFields[idx];
        const QColor c = QColorDialog::getColor(cur.isEmpty() ? QColor(Qt::black) : QColor(cur),
                                                 this, tr("Color"));
        if (!c.isValid())
            return;
        m_cfg.global.*kGlobalFields[idx] = c.name();
        refreshSwatches();
        return;
    }
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
    if (!m_styles->currentItem() || m_langKey == kGlobalStylesKey)
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
    if (m_langKey == kGlobalStylesKey)
        return;
    if (auto *ov = currentOverride(true)) {
        ov->bold = m_bold->isChecked();
        ov->italic = m_italic->isChecked();
        ov->underline = m_underline->isChecked();
    }
}

void StyleConfiguratorDialog::onExtensionsEdited()
{
    if (m_langKey == kGlobalStylesKey || m_langName.isEmpty())
        return;
    const QString text = m_userExt->text().trimmed();
    if (text.isEmpty())
        m_cfg.userExtensions.remove(m_langName);
    else
        m_cfg.userExtensions.insert(m_langName, text);
}

void StyleConfiguratorDialog::refreshGlobalOverrideSwatches()
{
    paintSwatch(m_globalFgSwatch,
                m_cfg.global.globalFg.isEmpty() ? QColor() : QColor(m_cfg.global.globalFg));
    paintSwatch(m_globalBgSwatch,
                m_cfg.global.globalBg.isEmpty() ? QColor() : QColor(m_cfg.global.globalBg));
    m_globalFgBtn->setEnabled(m_cfg.global.enableGlobalFg);
    m_globalBgBtn->setEnabled(m_cfg.global.enableGlobalBg);
}

void StyleConfiguratorDialog::pickGlobalFg()
{
    const QColor cur = m_cfg.global.globalFg.isEmpty() ? QColor(Qt::black)
                                                         : QColor(m_cfg.global.globalFg);
    const QColor c = QColorDialog::getColor(cur, this, tr("Global Foreground Color"));
    if (!c.isValid())
        return;
    m_cfg.global.globalFg = c.name();
    refreshGlobalOverrideSwatches();
}

void StyleConfiguratorDialog::pickGlobalBg()
{
    const QColor cur = m_cfg.global.globalBg.isEmpty() ? QColor(Qt::white)
                                                         : QColor(m_cfg.global.globalBg);
    const QColor c = QColorDialog::getColor(cur, this, tr("Global Background Color"));
    if (!c.isValid())
        return;
    m_cfg.global.globalBg = c.name();
    refreshGlobalOverrideSwatches();
}

void StyleConfiguratorDialog::onGlobalOverrideToggled()
{
    m_cfg.global.enableGlobalFg = m_enableGlobalFg->isChecked();
    m_cfg.global.enableGlobalBg = m_enableGlobalBg->isChecked();
    refreshGlobalOverrideSwatches();
}

void StyleConfiguratorDialog::apply()
{
    m_cfg.fontFamily = m_font->currentFont().family();
    m_cfg.fontSize = m_size->value();
    macpad::persistence::StyleStore::save(m_cfg);
    accept();
}

}  // namespace macpad::ui
