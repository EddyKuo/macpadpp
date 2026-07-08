#include "ui/ShortcutMapperDialog.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace macpad::ui {

static QString shortcutsPath()
{
    return macpad::persistence::AppPaths::filePath(QStringLiteral("shortcuts.json"));
}

// 持久化鍵：優先用穩定且唯一的 objectName，避免不同命令共用相同顯示文字時互相覆蓋；無 objectName 時退回文字
static QString actionKey(const QAction *a)
{
    const QString name = a->objectName();
    return name.isEmpty() ? QString(a->text()).remove(QChar('&')) : name;
}

void ShortcutMapperDialog::applySavedOverrides(const QList<QAction *> &actions)
{
    const QJsonObject o = macpad::persistence::JsonFile::load(shortcutsPath());
    if (o.isEmpty())
        return;
    for (QAction *a : actions) {
        const QString key = actionKey(a);
        if (o.contains(key))
            a->setShortcut(QKeySequence(o.value(key).toString()));
    }
}

QString ShortcutMapperDialog::conflictingAction(const QList<QAction *> &actions,
                                                 const QKeySequence &seq, const QAction *except)
{
    if (seq.isEmpty())
        return QString();
    for (const QAction *a : actions) {
        if (a == except)
            continue;
        if (a->shortcut() == seq)
            return QString(a->text()).remove(QChar('&'));
    }
    return QString();
}

ShortcutMapperDialog::ShortcutMapperDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent), m_actions(actions)
{
    setWindowTitle(tr("Shortcut Mapper"));
    resize(560, 520);
    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Double-click a command to reassign its shortcut."), this));

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter commands..."));
    root->addWidget(m_filterEdit);

    m_table = new QTableWidget(m_actions.size(), 2, this);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);

    for (int i = 0; i < m_actions.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(m_actions[i]->text().remove(QChar('&'))));
        m_table->setItem(i, 1, new QTableWidgetItem(m_actions[i]->shortcut().toString()));
    }
    root->addWidget(m_table, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ShortcutMapperDialog::editRow);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &ShortcutMapperDialog::applyFilter);
}

void ShortcutMapperDialog::applyFilter(const QString &text)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const bool match = text.isEmpty()
            || m_table->item(i, 0)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(i, !match);
    }
}

void ShortcutMapperDialog::editRow(int row, int)
{
    if (row < 0 || row >= m_actions.size())
        return;
    QAction *a = m_actions[row];

    // 小型輸入對話框：QKeySequenceEdit
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Assign Shortcut"));
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(a->text().remove(QChar('&')), &dlg));
    auto *edit = new QKeySequenceEdit(a->shortcut(), &dlg);
    lay->addWidget(edit);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                     | QDialogButtonBox::Reset, &dlg);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box->button(QDialogButtonBox::Reset), &QPushButton::clicked, edit,
            &QKeySequenceEdit::clear);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QKeySequence seq = edit->keySequence();

    const QString conflict = conflictingAction(m_actions, seq, a);
    if (!conflict.isEmpty()) {
        const auto choice = QMessageBox::warning(
            this, tr("Shortcut Conflict"),
            tr("\"%1\" is already assigned to \"%2\". Assign it here anyway?")
                .arg(seq.toString(), conflict),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes)
            return;
    }

    a->setShortcut(seq);
    m_table->item(row, 1)->setText(seq.toString());
    save();
}

void ShortcutMapperDialog::save()
{
    QJsonObject o;
    for (QAction *a : m_actions) {
        const QString sc = a->shortcut().toString();
        if (!sc.isEmpty())
            o.insert(actionKey(a), sc);
    }
    macpad::persistence::JsonFile::save(shortcutsPath(), o);
}

}  // namespace macpad::ui
