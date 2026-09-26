#pragma once

#include "ui/components/Controls.h"

#include <QSize>
#include <QWidget>

class QHBoxLayout;
class QIcon;

namespace litecode::ui::components {

class ActionBar final : public QWidget {
  public:
    explicit ActionBar(QWidget* parent = nullptr);

    IconButton* addIconButton(const QIcon& icon, const QString& tooltip, const QSize& buttonSize,
                              const QSize& iconSize, ButtonVariant variant = ButtonVariant::Ghost);
    void addSpacing(int spacing);

  private:
    QHBoxLayout* layout_{};
};

} // namespace litecode::ui::components
