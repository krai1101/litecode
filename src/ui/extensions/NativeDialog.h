#pragma once
#include <QDialog>
#include <QPointer>
class QVBoxLayout;
namespace litecode::ui {
class NativeDialog final : public QDialog {
  public:
    NativeDialog(const QString& title, QWidget* owner, QWidget* anchor);
    ~NativeDialog() override;
    QVBoxLayout* contentLayout() const { return content_; }
    void openCentered();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void position();
    QPointer<QWidget> anchor_;
    QPointer<QWidget> shade_;
    QVBoxLayout* content_{};
};
} // namespace litecode::ui
