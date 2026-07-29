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
#include <QTabWidget>
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

QStringList ShortcutMapperDialog::categoryOrder()
{
    // 對應 Notepad++ Shortcut Mapper 的分頁。刻意不含「Scintilla Commands」——
    // 本專案未把 Scintilla 層命令暴露成 QAction，擺一個永遠空的分頁只會誤導。
    return {QStringLiteral("Main Menu"), QStringLiteral("Macros"),
            QStringLiteral("Run Commands"), QStringLiteral("Plugin Commands")};
}

// 分類鍵 → 顯示名稱。必須以「字面量」呼叫 tr()，lupdate 才擷取得到——
// 若寫成 tr(qPrintable(key)) 可以編譯，但翻譯檔裡永遠不會有這些字串，
// 於是無論介面語言為何，分頁標題都只會顯示英文。
static QString categoryDisplayName(const QString &key)
{
    if (key == QLatin1String("Main Menu"))
        return ShortcutMapperDialog::tr("Main Menu");
    if (key == QLatin1String("Macros"))
        return ShortcutMapperDialog::tr("Macros");
    if (key == QLatin1String("Run Commands"))
        return ShortcutMapperDialog::tr("Run Commands");
    if (key == QLatin1String("Plugin Commands"))
        return ShortcutMapperDialog::tr("Plugin Commands");
    return key;
}

QString ShortcutMapperDialog::categoryFor(const QAction *action)
{
    if (!action)
        return QStringLiteral("Main Menu");
    const QString c = action->property(kCategoryProperty).toString().trimmed();
    return c.isEmpty() ? QStringLiteral("Main Menu") : c;
}

void ShortcutMapperDialog::setCategory(QAction *action, const QString &category)
{
    if (action)
        action->setProperty(kCategoryProperty, category);
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

    // 依來源分頁呈現（複刻 Notepad++）。每張表的列以 UserRole 存回 m_actions 的索引，
    // 因此列順序/篩選都不會弄錯對應的 action。
    auto *tabs = new QTabWidget(this);
    for (const QString &cat : categoryOrder()) {
        QList<int> indices;
        for (int i = 0; i < m_actions.size(); ++i) {
            if (categoryFor(m_actions[i]) == cat)
                indices << i;
        }
        if (indices.isEmpty())
            continue;   // 空分頁不顯示

        auto *table = new QTableWidget(indices.size(), 2, tabs);
        table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->setVisible(false);

        for (int row = 0; row < indices.size(); ++row) {
            const int idx = indices[row];
            auto *nameItem = new QTableWidgetItem(m_actions[idx]->text().remove(QChar('&')));
            nameItem->setData(Qt::UserRole, idx);
            table->setItem(row, 0, nameItem);
            table->setItem(row, 1, new QTableWidgetItem(m_actions[idx]->shortcut().toString()));
        }
        connect(table, &QTableWidget::cellDoubleClicked, this,
                [this, table](int row, int) { editRowIn(table, row); });
        m_tables << table;
        tabs->addTab(table, categoryDisplayName(cat));
    }
    root->addWidget(tabs, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &ShortcutMapperDialog::applyFilter);
}

void ShortcutMapperDialog::applyFilter(const QString &text)
{
    for (QTableWidget *table : std::as_const(m_tables)) {
        for (int i = 0; i < table->rowCount(); ++i) {
            const bool match = text.isEmpty()
                || table->item(i, 0)->text().contains(text, Qt::CaseInsensitive);
            table->setRowHidden(i, !match);
        }
    }
}

void ShortcutMapperDialog::editRowIn(QTableWidget *table, int row)
{
    if (!table || row < 0 || row >= table->rowCount())
        return;
    // 列 → m_actions 索引由 UserRole 取得（不可用列號直接當索引：分頁後兩者已不同）
    QTableWidgetItem *nameItem = table->item(row, 0);
    if (!nameItem)
        return;
    const int idx = nameItem->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_actions.size())
        return;
    QAction *a = m_actions[idx];   // 用 idx 而非 row——分頁後列號已不等於索引

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
    table->item(row, 1)->setText(seq.toString());
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
