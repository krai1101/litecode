#pragma once

#include <QString>

class QHBoxLayout;
class QWidget;

namespace litecode::ui::components {

void rebuildEditorBreadcrumb(QHBoxLayout& layout, QWidget& parent, const QString& displayPath,
                             const QString& fullPath, bool darkTheme, bool deleted = false);

} // namespace litecode::ui::components
