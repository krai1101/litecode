#pragma once
#include "extensions/NativeViewApi.h"
#include <functional>
class QMenu;
class QWidget;
namespace litecode::ui {
QMenu* createNativeMenu(const extensions::NativeControl& control, QWidget* parent,
                        std::function<void(const QString&)> activated);
void showNativeMenu(QMenu& menu, QWidget& anchor, bool alignRight = false);
} // namespace litecode::ui
