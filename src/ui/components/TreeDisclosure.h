#pragma once
#include <functional>

class QColor;
class QModelIndex;
class QPainter;
class QRect;
class QTreeView;

namespace litecode::ui::components {
void enableTreeRowDisclosure(QTreeView& tree, std::function<bool(const QModelIndex&)> expandable);
void paintTreeRowHighlight(QPainter& painter, const QRect& row, const QColor& color);
void paintTreeDisclosure(QPainter& painter, const QRect& branch, bool expanded, bool dark);
} // namespace litecode::ui::components
