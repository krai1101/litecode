#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QSignalSpy>
#include <QTest>

namespace litecode::tests {
namespace {

litecode::ui::CommandSpec command(const QString& id, const QString& title,
                                  const QKeySequence& shortcut) {
    return {id, title, title, shortcut, litecode::ui::CommandSurface::ShortcutEditor, [] {}};
}

} // namespace

class CommandRegistryTest final : public QObject {
    Q_OBJECT

  private slots:
    void distinguishesDefaultFromExplicitUnassignment();
    void shortcutEditorOmitsCommandsWithoutDefaults();
    void rejectsAmbiguousBindings();
    void resetRestoresDefaultAndCustomizationState();
    void externalOverridesReloadWithoutConflictingShortcuts();
};

void CommandRegistryTest::distinguishesDefaultFromExplicitUnassignment() {
    litecode::ui::CommandRegistry registry;
    const QKeySequence defaultShortcut(QStringLiteral("Ctrl+O"));
    QAction* defaultAction = registry.registerCommand(
        command(QStringLiteral("default"), QStringLiteral("Default"), defaultShortcut));
    QAction* unassignedAction = registry.registerCommand(
        command(QStringLiteral("unassigned"), QStringLiteral("Unassigned"), defaultShortcut),
        std::optional<QKeySequence>(QKeySequence{}));

    QVERIFY(defaultAction);
    QVERIFY(unassignedAction);
    QCOMPARE(defaultAction->shortcut(), defaultShortcut);
    QVERIFY(unassignedAction->shortcut().isEmpty());

    QAction* conflictingOverride = registry.registerCommand(
        command(QStringLiteral("conflicting"), QStringLiteral("Conflicting"),
                QKeySequence(QStringLiteral("Ctrl+3"))),
        std::optional<QKeySequence>(defaultShortcut));
    QVERIFY(conflictingOverride);
    QVERIFY(conflictingOverride->shortcut().isEmpty());

    const auto entries = registry.commandsFor(litecode::ui::CommandSurface::ShortcutEditor);
    QCOMPARE(entries.size(), 3);
    QVERIFY(!entries.at(0).customized);
    QVERIFY(entries.at(1).customized);
    QVERIFY(entries.at(2).customized);
}

void CommandRegistryTest::shortcutEditorOmitsCommandsWithoutDefaults() {
    litecode::ui::CommandRegistry registry;
    registry.registerCommand(
        command(QStringLiteral("withoutDefault"), QStringLiteral("Without Default"), {}));
    registry.registerCommand(command(QStringLiteral("withDefault"), QStringLiteral("With Default"),
                                     QKeySequence(QStringLiteral("Ctrl+D"))));

    const auto entries = registry.commandsFor(litecode::ui::CommandSurface::ShortcutEditor);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.constFirst().id, QStringLiteral("withDefault"));
}

void CommandRegistryTest::rejectsAmbiguousBindings() {
    litecode::ui::CommandRegistry registry;
    const QKeySequence firstShortcut(QStringLiteral("Ctrl+1"));
    const QKeySequence secondShortcut(QStringLiteral("Ctrl+2"));
    registry.registerCommand(
        command(QStringLiteral("first"), QStringLiteral("First"), firstShortcut));
    registry.registerCommand(
        command(QStringLiteral("second"), QStringLiteral("Second"), secondShortcut));

    const auto conflict = registry.shortcutConflict(QStringLiteral("second"), firstShortcut);
    QVERIFY(conflict.has_value());
    QCOMPARE(conflict->id, QStringLiteral("first"));
    QVERIFY(!registry.setShortcut(QStringLiteral("second"), firstShortcut));
    QCOMPARE(registry.action(QStringLiteral("second"))->shortcut(), secondShortcut);

    QVERIFY(registry.setShortcut(QStringLiteral("first"), {}));
    QVERIFY(registry.setShortcut(QStringLiteral("second"), firstShortcut));
    QCOMPARE(registry.action(QStringLiteral("second"))->shortcut(), firstShortcut);

    registry.registerCommand(command(QStringLiteral("chord"), QStringLiteral("Chord"),
                                     QKeySequence(QStringLiteral("Ctrl+K, Ctrl+O"))));
    registry.registerCommand(command(QStringLiteral("prefix"), QStringLiteral("Prefix"),
                                     QKeySequence(QStringLiteral("Alt+K"))));
    QVERIFY(
        !registry.setShortcut(QStringLiteral("prefix"), QKeySequence(QStringLiteral("Ctrl+K"))));

    QAction* duplicate =
        registry.registerCommand(command(QStringLiteral("duplicate"), QStringLiteral("Duplicate"),
                                         QKeySequence(QStringLiteral("Alt+D"))),
                                 std::optional<QKeySequence>(firstShortcut));
    QVERIFY(duplicate);
    QVERIFY(duplicate->shortcut().isEmpty());
}

void CommandRegistryTest::resetRestoresDefaultAndCustomizationState() {
    litecode::ui::CommandRegistry registry;
    const QKeySequence defaultShortcut(QStringLiteral("Ctrl+S"));
    registry.registerCommand(
        command(QStringLiteral("save"), QStringLiteral("Save"), defaultShortcut));
    QSignalSpy changed(&registry, &litecode::ui::CommandRegistry::commandChanged);

    QVERIFY(registry.setShortcut(QStringLiteral("save"), QKeySequence(QStringLiteral("Alt+S"))));
    QVERIFY(
        registry.commandsFor(litecode::ui::CommandSurface::ShortcutEditor).constFirst().customized);
    QVERIFY(registry.resetShortcut(QStringLiteral("save")));
    const auto entry =
        registry.commandsFor(litecode::ui::CommandSurface::ShortcutEditor).constFirst();
    QCOMPARE(entry.shortcut, defaultShortcut);
    QVERIFY(!entry.customized);
    QCOMPARE(changed.size(), 2);
}

void CommandRegistryTest::externalOverridesReloadWithoutConflictingShortcuts() {
    litecode::ui::CommandRegistry registry;
    registry.registerCommand(command(QStringLiteral("first"), QStringLiteral("First"),
                                     QKeySequence(QStringLiteral("Ctrl+1"))));
    registry.registerCommand(command(QStringLiteral("second"), QStringLiteral("Second"),
                                     QKeySequence(QStringLiteral("Ctrl+2"))));
    registry.reloadShortcuts({{QStringLiteral("first"), QStringLiteral("Alt+9")},
                              {QStringLiteral("second"), QStringLiteral("Ctrl+1")}});
    QCOMPARE(registry.action(QStringLiteral("first"))->shortcut(),
             QKeySequence(QStringLiteral("Alt+9")));
    QCOMPARE(registry.action(QStringLiteral("second"))->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+1")));
    registry.reloadShortcuts({});
    QCOMPARE(registry.action(QStringLiteral("first"))->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+1")));
    QCOMPARE(registry.action(QStringLiteral("second"))->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+2")));
}

} // namespace litecode::tests

QTEST_MAIN(litecode::tests::CommandRegistryTest)
#include "tst_command_registry.moc"
