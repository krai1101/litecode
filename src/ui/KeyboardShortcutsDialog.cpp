#include "ui/KeyboardShortcutsDialog.h"

#include "ui/commands/CommandRegistry.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include "ui/styles/SettingsDialogVisuals.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace litecode::ui {
namespace {

constexpr int CommandColumn = 0;
constexpr int KeybindingColumn = 1;
constexpr int SourceColumn = 2;

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString displaySequence(const QKeySequence& sequence) {
    return sequence.isEmpty() ? QObject::tr("Unassigned")
                              : sequence.toString(QKeySequence::NativeText);
}

} // namespace

KeyboardShortcutsDialog::KeyboardShortcutsDialog(CommandRegistry& commands, QWidget* parent)
    : QDialog(parent), commands_(commands) {
    setObjectName(QStringLiteral("keyboardShortcutsDialog"));
    setWindowTitle(tr("Keyboard Shortcuts"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(new DialogHeader(tr("Keyboard Shortcuts"), *this, this));

    auto* body = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 14, 20, 16);
    bodyLayout->setSpacing(10);

    search_ = new components::Input(body);
    search_->setObjectName(QStringLiteral("keyboardShortcutsSearch"));
    search_->setPlaceholderText(tr("Search keybindings"));
    search_->setClearButtonEnabled(true);
    bodyLayout->addWidget(search_);

    const auto entries = commands_.commandsFor(CommandSurface::ShortcutEditor);
    rows_.reserve(entries.size());
    table_ = new QTableWidget(entries.size(), 3, body);
    table_->setObjectName(QStringLiteral("keyboardShortcutsTable"));
    table_->setHorizontalHeaderLabels({tr("Command"), tr("Keybinding"), tr("Source")});
    for (int column = 0; column < table_->columnCount(); ++column)
        table_->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(CommandColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(KeybindingColumn, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(SourceColumn, QHeaderView::Fixed);
    table_->setColumnWidth(KeybindingColumn, 220);
    table_->setColumnWidth(SourceColumn, 90);
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);

    for (qsizetype index = 0; index < entries.size(); ++index) {
        const auto& entry = entries.at(index);
        const QString title = entry.paletteTitle.isEmpty() ? entry.title : entry.paletteTitle;
        rows_.push_back({entry.id, title, entry.shortcut, entry.defaultShortcut, entry.customized});
        table_->setItem(index, CommandColumn, readOnlyItem(title));
        table_->setItem(index, KeybindingColumn, readOnlyItem({}));
        table_->setItem(index, SourceColumn, readOnlyItem({}));
        table_->setRowHeight(index, 32);
        refreshRow(index);
    }
    bodyLayout->addWidget(table_, 1);

    message_ = new QLabel(body);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    message_->setObjectName(QStringLiteral("keyboardShortcutsMessage"));
    message_->setProperty("uiRole", QStringLiteral("shortcutMessage"));
    message_->setMinimumHeight(20);
    bodyLayout->addWidget(message_);

    auto* actions = new QWidget(body);
    actions->setProperty("uiComponent", QStringLiteral("actionBar"));
    auto* actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    changeButton_ = new components::Button(tr("Change Keybinding"), actions);
    removeButton_ = new components::Button(tr("Remove Keybinding"), actions);
    resetButton_ = new components::Button(tr("Reset to Default"), actions);
    changeButton_->setObjectName(QStringLiteral("shortcutChange"));
    removeButton_->setObjectName(QStringLiteral("shortcutRemove"));
    resetButton_->setObjectName(QStringLiteral("shortcutReset"));
    actionLayout->addWidget(changeButton_);
    actionLayout->addWidget(removeButton_);
    actionLayout->addWidget(resetButton_);
    actionLayout->addStretch(1);
    bodyLayout->addWidget(actions);
    layout->addWidget(body, 1);

    connect(search_, &QLineEdit::textChanged, this, &KeyboardShortcutsDialog::filterRows);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &KeyboardShortcutsDialog::refreshActions);
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { beginEditing(row); });
    connect(changeButton_, &QPushButton::clicked, this, [this] { beginEditing(selectedRow()); });
    connect(removeButton_, &QPushButton::clicked, this,
            &KeyboardShortcutsDialog::removeSelectedShortcut);
    connect(resetButton_, &QPushButton::clicked, this,
            &KeyboardShortcutsDialog::resetSelectedShortcut);

    if (!rows_.isEmpty())
        table_->selectRow(0);
    refreshActions();

    DialogChrome::configure(*this, DialogMode::Modeless);
    DialogChrome::fitToOwner(*this, QSize(820, 600), QSize(560, 380));
    for (int row = 0; row < rows_.size(); ++row)
        refreshRow(row);
    search_->setFocus();
}

void KeyboardShortcutsDialog::refreshTheme() {
    applySettingsDialogVisuals(*this);
    for (int row = 0; row < rows_.size(); ++row)
        refreshRow(row);
}

void KeyboardShortcutsDialog::reloadFromRegistry() {
    const QString selectedId = selectedRow() >= 0 ? rows_.at(selectedRow()).id : QString{};
    const QSignalBlocker blocker(table_);
    rows_.clear();
    table_->setRowCount(0);
    const auto entries = commands_.commandsFor(CommandSurface::ShortcutEditor);
    for (const auto& entry : entries) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QString title = entry.paletteTitle.isEmpty() ? entry.title : entry.paletteTitle;
        rows_.push_back({entry.id, title, entry.shortcut, entry.defaultShortcut, entry.customized});
        table_->setItem(row, CommandColumn, readOnlyItem(title));
        table_->setItem(row, KeybindingColumn, readOnlyItem({}));
        table_->setItem(row, SourceColumn, readOnlyItem({}));
        table_->setRowHeight(row, 32);
        refreshRow(row);
        if (entry.id == selectedId)
            table_->selectRow(row);
    }
    filterRows(search_->text());
}

int KeyboardShortcutsDialog::selectedRow() const {
    const auto selected = table_->selectionModel()->selectedRows();
    return selected.isEmpty() ? -1 : selected.constFirst().row();
}

void KeyboardShortcutsDialog::beginEditing(int row) {
    if (row < 0 || row >= rows_.size())
        return;
    clearMessage();
    table_->selectRow(row);
    if (table_->cellWidget(row, KeybindingColumn) != nullptr)
        return;
    for (int otherRow = 0; otherRow < rows_.size(); ++otherRow) {
        if (QWidget* existing = table_->cellWidget(otherRow, KeybindingColumn)) {
            disconnect(existing, nullptr, this, nullptr);
            table_->removeCellWidget(otherRow, KeybindingColumn);
            existing->deleteLater();
            refreshRow(otherRow);
        }
    }

    auto* editor = new QKeySequenceEdit(rows_.at(row).sequence, table_);
    editor->setObjectName(QStringLiteral("shortcutKeybindingEditor"));
    editor->setProperty("uiComponent", QStringLiteral("input"));
    editor->setClearButtonEnabled(true);
    editor->setMaximumSequenceLength(2);
    editor->setPalette(palette());
    for (QWidget* child : editor->findChildren<QWidget*>())
        child->setPalette(palette());
    table_->setCellWidget(row, KeybindingColumn, editor);
    editor->setFocus(Qt::ShortcutFocusReason);
    connect(editor, &QKeySequenceEdit::editingFinished, this,
            [this, row, editor] { applySequence(row, editor->keySequence()); });
}

void KeyboardShortcutsDialog::applySequence(int row, const QKeySequence& sequence) {
    if (row < 0 || row >= rows_.size())
        return;
    const auto conflict = commands_.shortcutConflict(rows_.at(row).id, sequence);
    if (conflict.has_value()) {
        showConflict(conflict->title);
        return;
    }
    if (!commands_.setShortcut(rows_.at(row).id, sequence))
        return;

    if (QWidget* editor = table_->cellWidget(row, KeybindingColumn)) {
        table_->removeCellWidget(row, KeybindingColumn);
        editor->deleteLater();
    }
    rows_[row].sequence = sequence;
    rows_[row].customized = true;
    refreshRow(row);
    refreshActions();
    clearMessage();
    emit shortcutChanged(rows_.at(row).id, sequence);
}

void KeyboardShortcutsDialog::removeSelectedShortcut() {
    const int row = selectedRow();
    if (row >= 0)
        applySequence(row, {});
}

void KeyboardShortcutsDialog::resetSelectedShortcut() {
    const int row = selectedRow();
    if (row < 0)
        return;
    const auto conflict =
        commands_.shortcutConflict(rows_.at(row).id, rows_.at(row).defaultSequence);
    if (conflict.has_value()) {
        showConflict(conflict->title);
        return;
    }
    if (!commands_.resetShortcut(rows_.at(row).id))
        return;
    rows_[row].sequence = rows_.at(row).defaultSequence;
    rows_[row].customized = false;
    refreshRow(row);
    refreshActions();
    clearMessage();
    emit shortcutReset(rows_.at(row).id);
}

void KeyboardShortcutsDialog::refreshRow(int row) {
    const Row& binding = rows_.at(row);
    table_->item(row, KeybindingColumn)->setText(displaySequence(binding.sequence));
    if (binding.sequence.isEmpty()) {
        table_->item(row, KeybindingColumn)
            ->setForeground(palette().color(QPalette::PlaceholderText));
    } else {
        table_->item(row, KeybindingColumn)->setData(Qt::ForegroundRole, {});
    }
    table_->item(row, SourceColumn)->setText(binding.customized ? tr("User") : tr("Default"));
    table_->item(row, CommandColumn)->setData(Qt::UserRole, binding.id);
    table_->item(row, CommandColumn)->setToolTip(binding.id);
    table_->item(row, KeybindingColumn)->setToolTip(tr("Double-click to change keybinding"));
}

void KeyboardShortcutsDialog::refreshActions() {
    const int row = selectedRow();
    const bool selected = row >= 0;
    changeButton_->setEnabled(selected);
    removeButton_->setEnabled(selected && !rows_.at(row).sequence.isEmpty());
    resetButton_->setEnabled(selected && rows_.at(row).customized);
}

void KeyboardShortcutsDialog::filterRows(const QString& query) {
    const QString needle = query.trimmed();
    int firstVisible = -1;
    for (int row = 0; row < rows_.size(); ++row) {
        const Row& binding = rows_.at(row);
        const bool matches =
            needle.isEmpty() || binding.title.contains(needle, Qt::CaseInsensitive) ||
            binding.id.contains(needle, Qt::CaseInsensitive) ||
            displaySequence(binding.sequence).contains(needle, Qt::CaseInsensitive) ||
            binding.sequence.toString(QKeySequence::PortableText)
                .contains(needle, Qt::CaseInsensitive);
        table_->setRowHidden(row, !matches);
        if (matches && firstVisible < 0)
            firstVisible = row;
    }
    const int selected = selectedRow();
    if (selected < 0 || table_->isRowHidden(selected)) {
        table_->clearSelection();
        if (firstVisible >= 0)
            table_->selectRow(firstVisible);
    }
    refreshActions();
}

void KeyboardShortcutsDialog::showConflict(const QString& commandTitle) {
    message_->setProperty("uiState", QStringLiteral("error"));
    message_->setText(tr("This keybinding is already assigned to %1.").arg(commandTitle));
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
}

void KeyboardShortcutsDialog::clearMessage() {
    message_->setProperty("uiState", QStringLiteral("default"));
    message_->clear();
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
}

} // namespace litecode::ui
