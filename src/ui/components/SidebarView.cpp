#include "ui/components/SidebarView.h"
#include <QVBoxLayout>
namespace litecode::ui::components {
SidebarView::SidebarView(QWidget* parent) : QWidget(parent), content_(new QVBoxLayout(this)) {
    setProperty("uiComponent", "sidebarView");
    content_->setContentsMargins(0, 0, 0, 0);
    content_->setSpacing(0);
}
} // namespace litecode::ui::components
