#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QWidgetAction>
namespace litecode::ui::components {
QAction* Menu::addDescriptiveAction(const QString& text) {
    if (!text.contains('\n') && !text.contains("$("))
        return addAction(text);
    auto* action = new QWidgetAction(this);
    action->setText(text);
    auto* row = new QWidget(this);
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(2);
    auto* label = new IconLabelButton(row);
    label->setIconLabel(text.section('\n', 0, 0));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(label);
    auto* description = new QLabel(text.section('\n', 1), row);
    description->setTextFormat(Qt::PlainText);
    description->setProperty("nativeAppearance", "muted");
    description->setVisible(text.contains('\n'));
    layout->addWidget(description);
    action->setDefaultWidget(row);
    addAction(action);
    return action;
}
} // namespace litecode::ui::components
