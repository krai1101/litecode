#pragma once
#include <QIcon>
#include <QVector>
#include <QWidget>
class QTabBar;
class QStackedWidget;
class QScrollArea;
class QScrollBar;
class QHBoxLayout;
namespace litecode::ui::components {
class IconLabelButton;
enum class TabOverflow { Scroll, Menu };
// Shared tab/page host. Editor tabs scroll in pixels; fixed panel tabs move
// into an overflow menu. Neither path depends on Qt's private scroll buttons.
class WorkbenchTabs final : public QWidget {
    Q_OBJECT
  public:
    explicit WorkbenchTabs(TabOverflow overflow, QWidget* parent = nullptr, QTabBar* bar = nullptr);
    QTabBar* tabBar() const { return bar_; }
    QScrollBar* horizontalScrollBar() const;
    int addTab(QWidget* page, const QString& text);
    int addTab(QWidget* page, const QIcon& icon, const QString& text);
    void removeTab(int index);
    int count() const { return pages_.size(); }
    QWidget* widget(int index) const;
    QWidget* currentWidget() const;
    int currentIndex() const;
    int indexOf(QWidget* page) const { return pages_.indexOf(page); }
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* page) { setCurrentIndex(indexOf(page)); }
    void setTabText(int index, const QString& text);
    QString tabText(int index) const;
    QIcon tabIcon(int index) const;
    void setTabToolTip(int index, const QString& text);
    QString tabToolTip(int index) const;
    void setTabsClosable(bool value);
    bool tabsClosable() const;
    void setMovable(bool value);
    bool isMovable() const;
    void setDocumentMode(bool value);
    void setIconSize(const QSize& size);
    void setCornerWidget(QWidget* widget, Qt::Corner corner = Qt::TopRightCorner);
    void refreshTabs();
  signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void scheduleRefresh();
    void revealCurrent();
    TabOverflow overflow_;
    QTabBar* bar_{};
    QStackedWidget* stack_{};
    QWidget* header_{};
    QWidget* tabArea_{};
    QWidget* corner_{};
    QScrollArea* scroll_{};
    QHBoxLayout* headerLayout_{};
    IconLabelButton* more_{};
    QVector<QWidget*> pages_;
    bool refreshing_{}, refreshPending_{};
};
} // namespace litecode::ui::components
