#pragma once
#include <QPointer>
#include <QVector>
#include <QWidget>
namespace litecode::ui {
// Hover metadata keeps its layout space, so revealing actions never moves text.
class NativeHoverGroup final : public QWidget {
  public:
    explicit NativeHoverGroup(QWidget* parent = nullptr);
    ~NativeHoverGroup() override;
    void addActions(QWidget* actions);

  protected:
    bool event(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void refresh();
    QVector<QPointer<QWidget>> actions_;
    bool hovered_{};
};
} // namespace litecode::ui
