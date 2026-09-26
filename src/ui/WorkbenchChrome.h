#pragma once

#include <QIcon>

class QDockWidget;
class QLabel;
class QStyle;
class QWidget;

namespace litecode::ui {

[[nodiscard]] QStyle* sharedWorkbenchStyle();
[[nodiscard]] QIcon workbenchActivityIcon(const QString& normal, const QString& active);
[[nodiscard]] QWidget* createWorkbenchDockHeader(QDockWidget* dock, const QString& title,
                                                 QLabel** titleLabel = nullptr,
                                                 bool closable = true);

} // namespace litecode::ui
