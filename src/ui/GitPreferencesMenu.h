#pragma once
#include "core/GitPreferences.h"
#include <functional>
class QWidget;
namespace litecode::ui::components {
class Menu;
}
namespace litecode::ui {
void addGitPreferencesMenu(components::Menu& menu, core::GitPreferences& preferences,
                           const std::function<void()>& changed, QWidget* owner);
}
