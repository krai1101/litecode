#pragma once

#include <QObject>
#include <QString>

namespace litecode::ui {

enum class SidebarView {
    Explorer,
    Search,
};

enum class BottomPanelView {
    Terminal,
};

class WorkbenchController final : public QObject {
    Q_OBJECT

  public:
    explicit WorkbenchController(QObject* parent = nullptr);

    [[nodiscard]] QString workspacePath() const;
    [[nodiscard]] bool hasWorkspace() const;
    [[nodiscard]] SidebarView sidebarView() const;
    [[nodiscard]] BottomPanelView bottomPanelView() const;
    [[nodiscard]] bool primarySidebarVisible() const;
    [[nodiscard]] bool bottomPanelVisible() const;

    void setWorkspacePath(const QString& workspacePath);
    void showSidebar(SidebarView view);
    void setPrimarySidebarVisible(bool visible);
    void togglePrimarySidebar();
    void showBottomPanel(BottomPanelView view);
    void setBottomPanelVisible(bool visible);
    void toggleBottomPanel();

  signals:
    void workspaceChanged(const QString& workspacePath, const QString& displayName);
    void sidebarChanged(litecode::ui::SidebarView view, bool visible);
    void bottomPanelChanged(litecode::ui::BottomPanelView view, bool visible);

  private:
    QString workspacePath_;
    SidebarView sidebarView_{SidebarView::Explorer};
    BottomPanelView bottomPanelView_{BottomPanelView::Terminal};
    bool primarySidebarVisible_{true};
    bool bottomPanelVisible_{true};
};

} // namespace litecode::ui

Q_DECLARE_METATYPE(litecode::ui::SidebarView)
Q_DECLARE_METATYPE(litecode::ui::BottomPanelView)
