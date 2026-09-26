#include "ui/components/EditorBreadcrumb.h"

#include "ui/ThemedIcon.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>

namespace litecode::ui::components {

void rebuildEditorBreadcrumb(QHBoxLayout& layout, QWidget& parent, const QString& displayPath,
                             const QString& fullPath, bool darkTheme, bool deleted) {
    while (QLayoutItem* item = layout.takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QStringList segments = displayPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype index = 0; index < segments.size(); ++index) {
        auto* segment = new QLabel(segments.at(index), &parent);
        segment->setObjectName(deleted ? QStringLiteral("deletedBreadcrumbSegment")
                                       : QStringLiteral("breadcrumbSegment"));
        if (deleted) {
            QFont font = segment->font();
            font.setStrikeOut(true);
            segment->setFont(font);
        }
        segment->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout.addWidget(segment);
        if (index + 1 < segments.size()) {
            auto* separator = new QLabel(&parent);
            separator->setObjectName(QStringLiteral("breadcrumbSeparator"));
            separator->setFixedSize(12, 18);
            separator->setPixmap(themedIcon(QStringLiteral(":/icons/chevron-right.svg"), darkTheme)
                                     .pixmap(QSize(12, 12)));
            separator->setAlignment(Qt::AlignCenter);
            layout.addWidget(separator);
        }
    }
    layout.addStretch(1);
    parent.setToolTip(fullPath);
}

} // namespace litecode::ui::components
