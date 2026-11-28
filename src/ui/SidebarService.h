#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <array>

class QAction;
class QDockWidget;
class QMainWindow;
class QStackedWidget;
class QTabBar;
class QToolBar;
class QWidget;

namespace litecode::ui {
class SidebarDragController;

// Native mapping of the workbench's SidebarPart / AuxiliaryBarPart. Containers
// are content, not independently docked windows. Built-ins and extensions use
// this same registry and the same two physical layout parts.
class SidebarService final : public QObject {
    Q_OBJECT
  public:
    enum class Location { Primary, Secondary };
    explicit SidebarService(QMainWindow& window, QToolBar* activityBar,
                            QDockWidget* primary = nullptr);
    void shutdown();
    void addContainer(const QString& id, const QString& title, QWidget* content, QAction* action,
                      Location location, QWidget* titleActions = nullptr, bool bindAction = true,
                      bool available = true);
    void removeContainer(const QString& id);
    bool contains(const QString& id) const;
    bool isAvailable(const QString& id) const;
    bool canMove(const QString& id) const;
    void setFixedLocation(const QString& id, Location location);
    void open(const QString& id, bool focus = false);
    void move(const QString& id, Location location);
    void setAvailable(const QString& id, bool available);
    void toggle(Location location);
    void setVisible(Location location, bool visible);
    bool resizeFromDrag(QDockWidget& dock, int requestedWidth, int initialWidth);
    QString activeContainer(Location location) const;
    QDockWidget* dock(Location location) const;
    Location location(const QString& id) const;
    bool containerVisible(const QString& id) const;
    QByteArray saveState() const;
    void restoreState(const QByteArray& state);
    void applyRestoredState();
    void migrateLegacyDockState();
  signals:
    void changed();
    void containerVisibilityChanged(const QString& id, bool visible);
    void activeContainerChanged(const QString& id);

  private:
    struct Entry {
        QString title;
        QPointer<QWidget> content;
        QPointer<QWidget> titleActions;
        QPointer<QAction> action;
        Location location{Location::Primary};
        bool available{true};
        bool reportedVisible{};
        bool fixed{};
        QList<QMetaObject::Connection> connections;
    };
    struct Host {
        QDockWidget* dock{};
        QStackedWidget* pages{};
        QWidget* actions{};
        QTabBar* tabs{};
        QWidget* empty{};
        QString active;
        QString desiredActive;
        bool desiredVisible{};
        int collapsedWidth{};
    };
    Host& host(Location location);
    const Host& host(Location location) const;
    void refresh();
    void restoreCollapsedWidth(Location location);
    void selectFallback(Location location);
    void showContextMenu(Location location, const QPoint& position);
    void migrateLegacyContainer(const QString& id);
    QMainWindow& window_;
    QPointer<QToolBar> activityBar_;
    QWidget* parking_{};
    std::array<Host, 2> hosts_;
    QHash<QString, Entry> entries_;
    QStringList order_;
    QJsonObject savedLocations_;
    bool updating_{};
    bool stopped_{};
    bool legacyRestoreEnabled_{};
    SidebarDragController* dragController_{};
};
} // namespace litecode::ui
