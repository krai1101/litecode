#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QAbstractButton;
class QAction;
class QObject;

namespace litecode::ui {

[[nodiscard]] QIcon themedIcon(const QString& resourcePath, bool dark);
[[nodiscard]] QIcon tintedIcon(const QString& resourcePath, const QColor& color,
                               const QColor& activeColor);
void setThemedIcon(QAbstractButton* button, const QString& resourcePath);
void setThemedIcon(QAction* action, const QString& resourcePath);
void refreshThemedIcons(QObject* root, bool dark);

} // namespace litecode::ui
