#include "ui/extensions/NativeDialog.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include <QEvent>
#include <QLabel>
#include <QShowEvent>
#include <QHideEvent>
#include <QVBoxLayout>
namespace litecode::ui {
NativeDialog::NativeDialog(const QString& title, QWidget* owner, QWidget* anchor)
    : QDialog(owner, Qt::Dialog | Qt::FramelessWindowHint), anchor_(anchor) {
    setWindowTitle(title);
    // The extension's pane is shaded below. Keep the workbench itself usable,
    // including its caption Close button, just like an extension-owned dialog.
    setWindowModality(Qt::NonModal);
    setProperty("nativeDialog", true);
    setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(12);
    auto* header = new QHBoxLayout;
    auto* label = new QLabel(title, this);
    label->setTextFormat(Qt::PlainText);
    label->setProperty("nativeAppearance", "heading");
    header->addWidget(label, 1);
    auto* close = new components::IconLabelButton(this);
    close->setIconLabel("$(close)");
    close->setAccessibleName(tr("Close"));
    close->setFixedSize(24, 24);
    header->addWidget(close);
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    layout->addLayout(header);
    content_ = new QVBoxLayout;
    content_->setContentsMargins(0, 0, 0, 0);
    content_->setSpacing(12);
    layout->addLayout(content_);
    if (anchor_) {
        shade_ = new QWidget(anchor_);
        shade_->setAutoFillBackground(true);
        shade_->setFocusPolicy(Qt::NoFocus);
        shade_->hide();
        anchor_->installEventFilter(this);
    }
}
NativeDialog::~NativeDialog() { delete shade_.data(); }
void NativeDialog::position() {
    if (!anchor_)
        return;
    if (shade_)
        shade_->setGeometry(anchor_->rect());
    setFixedWidth(qMin(520, qMax(180, anchor_->width() - 32)));
    adjustSize();
    move(anchor_->mapToGlobal(QPoint((anchor_->width() - width()) / 2,
                                     qMax(0, (anchor_->height() - height()) / 2))));
}
void NativeDialog::openCentered() {
    position();
    show();
}
void NativeDialog::showEvent(QShowEvent* event) {
    if (anchor_ && shade_) {
        auto palette = anchor_->palette();
        auto color = palette.color(QPalette::Window);
        color.setAlpha(210);
        palette.setColor(QPalette::Window, color);
        shade_->setPalette(palette);
        shade_->setGeometry(anchor_->rect());
        shade_->show();
        shade_->raise();
    }
    QDialog::showEvent(event);
}
void NativeDialog::hideEvent(QHideEvent* event) {
    if (shade_)
        shade_->hide();
    QDialog::hideEvent(event);
}
bool NativeDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == anchor_ && isVisible() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        position();
    return QDialog::eventFilter(watched, event);
}
} // namespace litecode::ui
