#pragma once
class QWidget;
namespace litecode::extensions { class WorkbenchOutput; }
namespace litecode::ui {
class CommandRegistry;
void registerLogLevelCommands(CommandRegistry& commands, extensions::WorkbenchOutput& output, QWidget* parent);
} // namespace litecode::ui
