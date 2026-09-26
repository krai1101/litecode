#pragma once

#include <QObject>
#include <QPointer>

class QAbstractScrollArea;
class QEvent;
class QScrollBar;
class QTimer;
class QWidget;

namespace litecode::ui {

// Keeps scrollbar geometry stable while optionally revealing the thumb continuously.
// The controller is owned by the scroll area it decorates.
class TransientScrollBars final : public QObject {
    Q_OBJECT

  public:
    static void install(QAbstractScrollArea* area, bool alwaysVisible = false);
    static void installIn(QWidget* widget, bool alwaysVisible = false);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    explicit TransientScrollBars(QAbstractScrollArea* area, bool alwaysVisible);
    void reveal();
    void scheduleHide();
    void setActive(bool active);
    void refresh(QScrollBar* bar);

    QPointer<QAbstractScrollArea> area_;
    QPointer<QScrollBar> vertical_;
    QPointer<QScrollBar> horizontal_;
    QTimer* hideTimer_{};
    bool alwaysVisible_{false};
    bool pointerOverBar_{false};
};

} // namespace litecode::ui
