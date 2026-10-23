#include "ui/components/ActionBar.h"

#include <QHBoxLayout>
#include <QIcon>

namespace litecode::ui::components {

ActionBar::ActionBar(QWidget* parent) : QWidget(parent) {
    setProperty("uiComponent", QStringLiteral("actionBar"));
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

IconButton* ActionBar::addIconButton(const QIcon& icon, const QString& tooltip,
                                     const QSize& buttonSize, const QSize& iconSize,
                                     ButtonVariant variant) {
    auto* button = new IconButton(this, variant);
    button->setIcon(icon);
    button->setIconSize(iconSize);
    button->setFixedSize(buttonSize);
    button->setToolTip(tooltip);
    layout_->addWidget(button);
    return button;
}

void ActionBar::addSpacing(int spacing) { layout_->addSpacing(spacing); }

} // namespace litecode::ui::components
