#include "ui/extensions/LogLevelCommands.h"
#include "extensions/WorkbenchOutput.h"
#include "ui/QuickOpenDialog.h"
#include "ui/commands/CommandRegistry.h"
#include <QPointer>
#include <QWidget>

namespace litecode::ui {
void registerLogLevelCommands(CommandRegistry& commands, extensions::WorkbenchOutput& output, QWidget* parent) {
    const QPointer<extensions::WorkbenchOutput> backend(&output);
    const QPointer<QWidget> window(parent);
    commands.registerCommand({"workbench.action.setLogLevel", QObject::tr("Set Log Level..."),
        QObject::tr("Developer: Set Log Level..."), {}, CommandSurface::Palette,
        [backend, window] {
            if (!backend || !window) return;
            QVector<QuickPickItem> targets{{"$global", QObject::tr("Default"), QObject::tr("Default extension log level")}};
            for (const auto& channel : backend->channels())
                if (channel.log) targets.append({channel.id, channel.name, {}});
            QuickOpenDialog targetPicker(QObject::tr("Set Log Level"), QObject::tr("Select a log"), std::move(targets), "$global", window);
            if (targetPicker.exec() != QDialog::Accepted || !backend || !window) return;
            const auto id = targetPicker.selectedItemId();
            const auto channel = backend->channel(id);
            if (id != "$global" && !channel) return;
            const auto current = id == "$global" ? backend->globalLogLevel() : channel->level;
            QVector<QuickPickItem> levels{{"1", QObject::tr("Trace"), {}}, {"2", QObject::tr("Debug"), {}},
                {"3", QObject::tr("Info"), {}}, {"4", QObject::tr("Warning"), {}}, {"5", QObject::tr("Error"), {}}, {"0", QObject::tr("Off"), {}}};
            QuickOpenDialog levelPicker(QObject::tr("Set Log Level"), QObject::tr("Select a log level"),
                std::move(levels), QString::number(static_cast<int>(current)), window);
            if (levelPicker.exec() != QDialog::Accepted || !backend) return;
            bool valid = false;
            const auto level = levelPicker.selectedItemId().toInt(&valid);
            if (!valid || level < 0 || level > 5) return;
            if (id == "$global") backend->setGlobalLogLevel(static_cast<extensions::LogLevel>(level));
            else backend->setLogLevel(id, static_cast<extensions::LogLevel>(level));
        }});
}
} // namespace litecode::ui
