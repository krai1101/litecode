#include "ui/commands/CommandRegistry.h"

#include <QAction>

#include <utility>

namespace litecode::ui {
namespace {

bool keySequencesOverlap(const QKeySequence& left, const QKeySequence& right) {
    if (left.isEmpty() || right.isEmpty())
        return false;
    const qsizetype sharedLength = qMin(left.count(), right.count());
    for (qsizetype index = 0; index < sharedLength; ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

} // namespace

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

QAction* CommandRegistry::registerCommand(CommandSpec spec,
                                          std::optional<QKeySequence> currentShortcut) {
    if (spec.id.trimmed().isEmpty() || spec.title.trimmed().isEmpty() || !spec.invoke ||
        entries_.contains(spec.id)) {
        return nullptr;
    }

    auto* commandAction = new QAction(spec.title, this);
    commandAction->setObjectName(spec.id);
    QKeySequence effectiveShortcut = currentShortcut.value_or(spec.defaultShortcut);
    if (shortcutConflict(spec.id, effectiveShortcut).has_value())
        effectiveShortcut = {};
    commandAction->setShortcut(effectiveShortcut);
    const QString id = spec.id;
    connect(commandAction, &QAction::triggered, this, [this, id] { (void)invoke(id); });

    order_.push_back(id);
    entries_.insert(id, Entry{std::move(spec), commandAction, currentShortcut.has_value()});
    return commandAction;
}

QAction* CommandRegistry::action(const QString& id) const {
    const auto entry = entries_.constFind(id);
    return entry == entries_.cend() ? nullptr : entry->action;
}

QVector<CommandSnapshot> CommandRegistry::commandsFor(CommandSurface surface) const {
    QVector<CommandSnapshot> result;
    result.reserve(order_.size());
    for (const QString& id : order_) {
        const auto entry = entries_.constFind(id);
        if (entry == entries_.cend() || !entry->spec.surfaces.testFlag(surface)) {
            continue;
        }
        if (surface == CommandSurface::ShortcutEditor && entry->action->shortcut().isEmpty() &&
            !entry->customized) {
            continue;
        }
        result.push_back({entry->spec.id, entry->spec.title, entry->spec.paletteTitle,
                          entry->action->shortcut(), entry->spec.defaultShortcut, entry->customized,
                          entry->action->isEnabled(), entry->spec.surfaces});
    }
    return result;
}

bool CommandRegistry::invoke(const QString& id) {
    const auto entry = entries_.find(id);
    if (entry == entries_.end() || !entry->action->isEnabled()) {
        return false;
    }
    entry->spec.invoke();
    emit commandInvoked(id);
    return true;
}

bool CommandRegistry::setShortcut(const QString& id, const QKeySequence& shortcut) {
    const auto entry = entries_.find(id);
    if (entry == entries_.end() || shortcutConflict(id, shortcut).has_value()) {
        return false;
    }
    entry->action->setShortcut(shortcut);
    entry->customized = true;
    emit commandChanged(id);
    return true;
}

bool CommandRegistry::resetShortcut(const QString& id) {
    const auto entry = entries_.find(id);
    if (entry == entries_.end() || shortcutConflict(id, entry->spec.defaultShortcut).has_value()) {
        return false;
    }
    entry->action->setShortcut(entry->spec.defaultShortcut);
    entry->customized = false;
    emit commandChanged(id);
    return true;
}

void CommandRegistry::reloadShortcuts(const QHash<QString, QString>& overrides) {
    QVector<QKeySequence> accepted;
    accepted.reserve(order_.size());
    for (const QString& id : order_) {
        auto entry = entries_.find(id);
        const bool customized = overrides.contains(id);
        QKeySequence shortcut = entry->spec.defaultShortcut;
        if (customized) {
            const QString stored = overrides.value(id);
            shortcut = QKeySequence::fromString(stored, QKeySequence::PortableText);
            if (!stored.isEmpty() && shortcut.isEmpty())
                shortcut = QKeySequence::fromString(stored, QKeySequence::NativeText);
        }
        for (const QKeySequence& previous : accepted) {
            if (keySequencesOverlap(shortcut, previous)) {
                shortcut = {};
                break;
            }
        }
        accepted.push_back(shortcut);
        if (entry->action->shortcut() != shortcut || entry->customized != customized) {
            entry->action->setShortcut(shortcut);
            entry->customized = customized;
            emit commandChanged(id);
        }
    }
}

std::optional<CommandSnapshot>
CommandRegistry::shortcutConflict(const QString& id, const QKeySequence& shortcut) const {
    if (shortcut.isEmpty())
        return std::nullopt;
    for (const QString& candidateId : order_) {
        if (candidateId == id)
            continue;
        const auto candidate = entries_.constFind(candidateId);
        if (candidate != entries_.cend() &&
            keySequencesOverlap(candidate->action->shortcut(), shortcut)) {
            return CommandSnapshot{candidate->spec.id,
                                   candidate->spec.title,
                                   candidate->spec.paletteTitle,
                                   candidate->action->shortcut(),
                                   candidate->spec.defaultShortcut,
                                   candidate->customized,
                                   candidate->action->isEnabled(),
                                   candidate->spec.surfaces};
        }
    }
    return std::nullopt;
}

bool CommandRegistry::setEnabled(const QString& id, bool enabled) {
    const auto entry = entries_.find(id);
    if (entry == entries_.end()) {
        return false;
    }
    if (entry->action->isEnabled() == enabled) {
        return true;
    }
    entry->action->setEnabled(enabled);
    emit commandChanged(id);
    return true;
}

} // namespace litecode::ui
