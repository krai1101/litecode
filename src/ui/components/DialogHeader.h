#pragma once

#include <QWidget>

class QDialog;

namespace litecode::ui {

class DialogHeader final : public QWidget {
  public:
    DialogHeader(const QString& title, QDialog& dialog, QWidget* parent = nullptr);
};

} // namespace litecode::ui
