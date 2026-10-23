#pragma once

#include <QSize>

class QDialog;
class QPushButton;

namespace litecode::ui {

enum class DialogMode {
    Modal,
    Modeless,
};

// Shared dialog shell behavior: window chrome, palette, placement and action variants.
class DialogChrome final {
  public:
    static void configure(QDialog& dialog, DialogMode mode = DialogMode::Modal);
    static void fitToOwner(QDialog& dialog, const QSize& preferred,
                           const QSize& minimum = QSize(320, 240));
    static void markPrimary(QPushButton* button);
};

} // namespace litecode::ui
