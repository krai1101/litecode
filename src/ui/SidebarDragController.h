#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <memory>

class QMainWindow;
class QMimeData;
class QToolBar;
class QWidget;

namespace litecode::ui {
class SidebarService;

// Workbench-only container dragging. Extension content and external file/text
// drops retain their own handlers; only this window's drag payload is consumed.
class SidebarDragController final : public QObject {
    Q_OBJECT
  public:
    SidebarDragController(SidebarService& sidebars, QMainWindow& window, QToolBar* activityBar,
                          QObject* parent);
    ~SidebarDragController() override;
    std::unique_ptr<QMimeData> createMimeData(const QString& container) const;
    void shutdown();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QString sourceAt(QWidget* widget, const QPoint& position) const;
    QString readContainer(const QMimeData* mime) const;
    int destinationAt(const QPoint& globalPosition) const;
    void showPreview(int destination);
    void clearPreview();
    void startDrag(const QString& container, QWidget* source);
    SidebarService& sidebars_;
    QMainWindow& window_;
    QPointer<QToolBar> activityBar_;
    QPointer<QWidget> preview_;
    QPointer<QWidget> pressSource_;
    QPoint pressPosition_;
    QString pressContainer_;
    QString instance_;
    int previewDestination_{-1};
    bool dragging_{};
    bool stopped_{};
};
} // namespace litecode::ui
