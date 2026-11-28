#pragma once
#include "extensions/WorkbenchNativeViews.h"
#include <QFont>
#include <QHash>
#include <QPointer>
#include <optional>
class QMainWindow;
class QToolBar;
class QDockWidget;
class QAction;
class QVBoxLayout;
class QMenu;
namespace litecode::ui {
class NativeViewWidget;
class SidebarService;
class NativeViewService final : public QObject {
    Q_OBJECT
  public:
    NativeViewService(extensions::WorkbenchNativeViews& views, QMainWindow& window,
                      QToolBar* activityBar, QObject* parent = nullptr, QMenu* viewMenu = nullptr,
                      SidebarService* sidebars = nullptr);
    ~NativeViewService() override;
    void openView(const QString& viewId);
    void setCodeFont(const QFont& font);
  signals:
    void layoutChanged();

  private:
    void synchronize();
    void detach();
    struct Container {
        QPointer<QWidget> body;
        QPointer<QAction> action;
        QVBoxLayout* layout{};
    };
    extensions::WorkbenchNativeViews& views_;
    QMainWindow& window_;
    QPointer<SidebarService> sidebars_;
    QPointer<QMenu> viewMenu_;
    QHash<QString, Container> containers_;
    QHash<QString, QPointer<NativeViewWidget>> widgets_;
    QHash<QString, QString> memberships_;
    std::optional<QFont> codeFont_;
    bool attached_{true};
};
} // namespace litecode::ui
