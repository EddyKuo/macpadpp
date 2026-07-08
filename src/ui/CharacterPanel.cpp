#include "ui/CharacterPanel.h"

#include <QEvent>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QTableWidget>
#include <QTextCodec>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::ui {

namespace {

// HTML4 標準具名實體（含 ASCII 常用符號 + Latin-1 Supplement），無對應名稱者不列入表中。
const QHash<int, QString> &htmlNamedEntities()
{
    static const QHash<int, QString> table = {
        {34, QStringLiteral("quot")},   {38, QStringLiteral("amp")},    {60, QStringLiteral("lt")},
        {62, QStringLiteral("gt")},     {160, QStringLiteral("nbsp")},  {161, QStringLiteral("iexcl")},
        {162, QStringLiteral("cent")},  {163, QStringLiteral("pound")}, {164, QStringLiteral("curren")},
        {165, QStringLiteral("yen")},   {166, QStringLiteral("brvbar")},{167, QStringLiteral("sect")},
        {168, QStringLiteral("uml")},   {169, QStringLiteral("copy")},  {170, QStringLiteral("ordf")},
        {171, QStringLiteral("laquo")}, {172, QStringLiteral("not")},   {173, QStringLiteral("shy")},
        {174, QStringLiteral("reg")},   {175, QStringLiteral("macr")},  {176, QStringLiteral("deg")},
        {177, QStringLiteral("plusmn")},{178, QStringLiteral("sup2")},  {179, QStringLiteral("sup3")},
        {180, QStringLiteral("acute")}, {181, QStringLiteral("micro")}, {182, QStringLiteral("para")},
        {183, QStringLiteral("middot")},{184, QStringLiteral("cedil")}, {185, QStringLiteral("sup1")},
        {186, QStringLiteral("ordm")},  {187, QStringLiteral("raquo")}, {188, QStringLiteral("frac14")},
        {189, QStringLiteral("frac12")},{190, QStringLiteral("frac34")},{191, QStringLiteral("iquest")},
        {192, QStringLiteral("Agrave")},{193, QStringLiteral("Aacute")},{194, QStringLiteral("Acirc")},
        {195, QStringLiteral("Atilde")},{196, QStringLiteral("Auml")},  {197, QStringLiteral("Aring")},
        {198, QStringLiteral("AElig")}, {199, QStringLiteral("Ccedil")},{200, QStringLiteral("Egrave")},
        {201, QStringLiteral("Eacute")},{202, QStringLiteral("Ecirc")}, {203, QStringLiteral("Euml")},
        {204, QStringLiteral("Igrave")},{205, QStringLiteral("Iacute")},{206, QStringLiteral("Icirc")},
        {207, QStringLiteral("Iuml")},  {208, QStringLiteral("ETH")},   {209, QStringLiteral("Ntilde")},
        {210, QStringLiteral("Ograve")},{211, QStringLiteral("Oacute")},{212, QStringLiteral("Ocirc")},
        {213, QStringLiteral("Otilde")},{214, QStringLiteral("Ouml")},  {215, QStringLiteral("times")},
        {216, QStringLiteral("Oslash")},{217, QStringLiteral("Ugrave")},{218, QStringLiteral("Uacute")},
        {219, QStringLiteral("Ucirc")}, {220, QStringLiteral("Uuml")},  {221, QStringLiteral("Yacute")},
        {222, QStringLiteral("THORN")}, {223, QStringLiteral("szlig")}, {224, QStringLiteral("agrave")},
        {225, QStringLiteral("aacute")},{226, QStringLiteral("acirc")}, {227, QStringLiteral("atilde")},
        {228, QStringLiteral("auml")},  {229, QStringLiteral("aring")}, {230, QStringLiteral("aelig")},
        {231, QStringLiteral("ccedil")},{232, QStringLiteral("egrave")},{233, QStringLiteral("eacute")},
        {234, QStringLiteral("ecirc")}, {235, QStringLiteral("euml")},  {236, QStringLiteral("igrave")},
        {237, QStringLiteral("iacute")},{238, QStringLiteral("icirc")}, {239, QStringLiteral("iuml")},
        {240, QStringLiteral("eth")},   {241, QStringLiteral("ntilde")},{242, QStringLiteral("ograve")},
        {243, QStringLiteral("oacute")},{244, QStringLiteral("ocirc")}, {245, QStringLiteral("otilde")},
        {246, QStringLiteral("ouml")},  {247, QStringLiteral("divide")},{248, QStringLiteral("oslash")},
        {249, QStringLiteral("ugrave")},{250, QStringLiteral("uacute")},{251, QStringLiteral("ucirc")},
        {252, QStringLiteral("uuml")},  {253, QStringLiteral("yacute")},{254, QStringLiteral("thorn")},
        {255, QStringLiteral("yuml")},
    };
    return table;
}

enum Column {
    ColValue = 0,
    ColHex,
    ColCharacter,
    ColHtmlName,
    ColHtmlDecimal,
    ColHtmlHex,
    ColumnCount,
};

}  // namespace

CharacterPanel::CharacterPanel(QWidget *parent)
    : QDockWidget(tr("Character Panel"), parent)
{
    setObjectName(QStringLiteral("CharacterPanelDock"));

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_encodingLabel = new QLabel(container);
    m_encodingLabel->setContentsMargins(4, 2, 4, 2);
    setEncodingLabel(tr("Unicode"));
    layout->addWidget(m_encodingLabel);

    m_table = new QTableWidget(256, ColumnCount, container);
    m_table->setHorizontalHeaderLabels({tr("Value"), tr("Hex"), tr("Character"), tr("HTML Name"),
                                         tr("HTML Decimal"), tr("HTML Hexadecimal")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);

    const auto &namedEntities = htmlNamedEntities();
    for (int i = 0; i < 256; ++i) {
        auto *dec = new QTableWidgetItem(QString::number(i));
        // 僅將十六進位數字轉大寫，保留小寫 "0x" 前綴（避免變成 "0X"）
        const QString hexDigits = QString::number(i, 16).rightJustified(2, QChar('0')).toUpper();
        auto *hex = new QTableWidgetItem(QStringLiteral("0x%1").arg(hexDigits));
        // 0..31 為控制字元，顯示佔位符；32+ 顯示實際字元
        QString glyph = (i >= 32 && i != 127) ? QString(QChar(i)) : QStringLiteral("·");
        auto *chr = new QTableWidgetItem(glyph);
        // 無對應具名實體的字元，HTML Name 欄留空
        auto *htmlName = new QTableWidgetItem(namedEntities.value(i));
        auto *htmlDecimal = new QTableWidgetItem(QStringLiteral("&#%1;").arg(i));
        auto *htmlHex = new QTableWidgetItem(QStringLiteral("&#x%1;").arg(hexDigits));
        m_table->setItem(i, ColValue, dec);
        m_table->setItem(i, ColHex, hex);
        m_table->setItem(i, ColCharacter, chr);
        m_table->setItem(i, ColHtmlName, htmlName);
        m_table->setItem(i, ColHtmlDecimal, htmlDecimal);
        m_table->setItem(i, ColHtmlHex, htmlHex);
    }
    m_table->resizeColumnsToContents();

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        emitForCell(row, column);
    });

    // 讓鍵盤 Return/Enter 也能比照雙擊插入目前選取列/欄的字元
    m_table->installEventFilter(this);

    layout->addWidget(m_table);
    setWidget(container);
}

void CharacterPanel::setEncodingLabel(const QString &encoding)
{
    if (m_encodingLabel)
        m_encodingLabel->setText(tr("Encoding: %1").arg(encoding));
}

void CharacterPanel::setUnicodeMode(bool unicode)
{
    if (m_unicodeMode == unicode)
        return;
    m_unicodeMode = unicode;
    rebuildHighRangeColumn();
}

void CharacterPanel::setCodepage(const QString &codepageName)
{
    if (m_codepageName == codepageName)
        return;
    m_codepageName = codepageName;
    if (m_unicodeMode)
        rebuildHighRangeColumn();
}

void CharacterPanel::rebuildHighRangeColumn()
{
    if (!m_table)
        return;
    for (int i = 128; i < 256; ++i) {
        QString glyph;
        if (m_unicodeMode) {
            // Unicode 文件：128..255 視為系統預設 codepage 的單一位元組，映射為該 codepage 實際字元
            QTextCodec *codec = QTextCodec::codecForName(m_codepageName.toLatin1());
            if (codec) {
                const char byte = static_cast<char>(i);
                glyph = codec->toUnicode(&byte, 1);
            }
        }
        if (glyph.isEmpty())
            glyph = QString(QChar(i));  // ANSI/Latin-1 模式，或 codepage 不存在時的安全回退
        if (auto *item = m_table->item(i, ColCharacter))
            item->setText(glyph);
    }
}

void CharacterPanel::emitForCell(int row, int column)
{
    if (row < 0 || row >= 256)
        return;

    switch (column) {
    case ColHtmlName: {
        const QString name = htmlNamedEntities().value(row);
        if (!name.isEmpty()) {
            emit charChosen(QStringLiteral("&%1;").arg(name));
            return;
        }
        break;  // 無具名實體時，回退為插入字元本身
    }
    case ColHtmlDecimal:
        emit charChosen(QStringLiteral("&#%1;").arg(row));
        return;
    case ColHtmlHex:
        emit charChosen(QStringLiteral("&#x%1;")
                            .arg(QString::number(row, 16).rightJustified(2, QChar('0')).toUpper()));
        return;
    default:
        break;  // Value / Hex / Character 欄皆回退為插入字元本身
    }

    if (row >= 32 && row != 127)
        emit charChosen(QString(QChar(row)));
}

bool CharacterPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            emitForCell(m_table->currentRow(), m_table->currentColumn());
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

}  // namespace macpad::ui
