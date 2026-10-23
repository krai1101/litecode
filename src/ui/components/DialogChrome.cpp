#include "ui/components/DialogChrome.h"

#include "ui/PopupPositioner.h"
#include "ui/components/Controls.h"
#include "ui/styles/SettingsDialogVisuals.h"

#include <QDialog>
#include <QEvent>
#include <QObject>
#include <QPushButton>

namespace litecode::ui {
namespace {

class DialogPlacementFilter final : public QObject {
  public:
    explicit DialogPlacementFilter(QDialog& dialog) : QObject(&dialog), dialog_(dialog) {}

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == &dialog_ && event->type() == QEvent::Show) {
            if (QWidget* owner = dialog_.parentWidget()) {
                const QPoint desired =
                    owner->mapToGlobal(QPoint((owner->width() - dialog_.width()) / 2,
                                              (owner->height() - dialog_.height()) / 2));
                dialog_.move(PopupPositioner::clampTopLeft(dialog_.size(), owner, desired, 12));
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    QDialog& dialog_;
};

} // namespace

void DialogChrome::configure(QDialog& dialog, DialogMode mode) {
    const bool modal = mode == DialogMode::Modal;
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setWindowModality(modal ? Qt::WindowModal : Qt::NonModal);
    dialog.setModal(modal);
    dialog.setAttribute(Qt::WA_StyledBackground, true);
    dialog.setProperty("litecodeModal", modal);
    applySettingsDialogVisuals(dialog);
    components::adoptDialogControls(dialog);
    dialog.installEventFilter(new DialogPlacementFilter(dialog));
}

void DialogChrome::fitToOwner(QDialog& dialog, const QSize& preferred, const QSize& minimum) {
    dialog.resize(PopupPositioner::boundedDialogSize(dialog.parentWidget(), preferred, minimum));
}

void DialogChrome::markPrimary(QPushButton* button) {
    if (button)
        components::setButtonVariant(*button, components::ButtonVariant::Primary);
}

} // namespace litecode::ui
