#pragma once
#include <QColor>
#include <QString>
namespace litecode::ui::components {
// Parse extension-facing CSS color values without inserting raw text into QSS.
QColor cssColor(const QString& value);
}
