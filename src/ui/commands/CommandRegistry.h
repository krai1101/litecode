#pragma once

#include <QFlags>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

class QAction;

namespace litecode::ui {

enum class CommandSurface : quint8 {
    None = 0,
    Menu = 1 << 0,
    Palette = 1 << 1,
    ShortcutEditor = 1 << 2,
};
Q_DECLARE_FLAGS(CommandSurfaces, CommandSurface)

struct CommandSpec final {
    QString id;
    QString title;
    QString paletteTitle;
    QKeySequence defaultShortcut;
    CommandSurfaces surfaces;
    std::function<void()> invoke;
};

struct CommandSnapshot final {
    QString id;
    QString title;
    QString paletteTitle;
    QKeySequence shortcut;
    QKeySequence defaultShortcut;
    bool customized{false};
    bool enabled{true};
    CommandSurfaces surfaces;
};

class CommandRegistry final : public QObject {
    Q_OBJECT

  public:
    explicit CommandRegistry(QObject* parent = nullptr);

    QAction* registerCommand(CommandSpec spec,
                             std::optional<QKeySequence> currentShortcut = std::nullopt);
    [[nodiscard]] QAction* action(const QString& id) const;
    [[nodiscard]] QVector<CommandSnapshot> commandsFor(CommandSurface surface) const;
    [[nodiscard]] bool invoke(const QString& id);
    [[nodiscard]] bool setShortcut(const QString& id, const QKeySequence& shortcut);
    [[nodiscard]] bool resetShortcut(const QString& id);
    void reloadShortcuts(const QHash<QString, QString>& overrides);
    [[nodiscard]] std::optional<CommandSnapshot>
    shortcutConflict(const QString& id, const QKeySequence& shortcut) const;
    [[nodiscard]] bool setEnabled(const QString& id, bool enabled);

  signals:
    void commandInvoked(const QString& id);
    void commandChanged(const QString& id);

  private:
    struct Entry final {
        CommandSpec spec;
        QAction* action{};
        bool customized{};
    };

    QHash<QString, Entry> entries_;
    QVector<QString> order_;
};

} // namespace litecode::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(litecode::ui::CommandSurfaces)
