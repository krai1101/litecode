#pragma once
#include <QMargins>
#include <QWidget>
class QVBoxLayout;
namespace litecode::ui::components {
// Native counterpart of a shared workbench view body. Lists stay edge-to-edge;
// controls and messages use the same compact inset in every sidebar view.
class SidebarView : public QWidget {
    Q_OBJECT
  public:
    explicit SidebarView(QWidget* parent = nullptr);
    QVBoxLayout* contentLayout() const { return content_; }
    static constexpr int HorizontalInset = 8;
    static constexpr int VerticalInset = 4;
    static QMargins controlMargins() {
        return {HorizontalInset, VerticalInset, HorizontalInset, VerticalInset};
    }
    static QMargins toolbarMargins() { return {HorizontalInset, 0, HorizontalInset, 0}; }

  private:
    QVBoxLayout* content_{};
};
} // namespace litecode::ui::components
