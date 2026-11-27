#pragma once
#include <QScrollArea>
#include <QTimer>
#include <QVariantAnimation>
namespace litecode::ui {
namespace components {
class IconLabelButton;
}
// A native transcript scroll surface. User navigation is distinct from layout
// and restoration, so loading history never recursively requests another page.
class NativeTranscriptView final : public QScrollArea {
    Q_OBJECT
  public:
    explicit NativeTranscriptView(QWidget* parent = nullptr);
    void restorePosition(int position, bool followEnd);
    void preserveAnchor(const QString& id, int offset);
    bool followingLatest() const { return following_; }
    quint64 navigationEpoch() const { return navigationEpoch_; }
    static bool forwardWheelAtEdge(QAbstractScrollArea* source, QWheelEvent* event);
  signals:
    void boundaryReached(bool atStart);
    void endRequested();

  protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void updateNavigation();
    void notifyNavigation(int position, int direction);
    components::IconLabelButton* latest_{};
    QTimer follow_;
    QVariantAnimation wheelMotion_;
    qreal wheelTarget_{};
    int wheelDirection_{};
    QString anchorId_;
    int anchorOffset_{};
    quint64 boundarySerial_{};
    quint64 navigationEpoch_{};
    bool following_{true};
};
} // namespace litecode::ui
