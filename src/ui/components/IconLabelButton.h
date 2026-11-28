#pragma once
#include "ui/components/Controls.h"
#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QVector>
class QTimer;
class QColor;
namespace litecode::ui::components {
// Shared native rendering of VS Code $(theme-icon) labels. No HTML evaluation.
class IconLabelButton final : public IconButton {
    Q_OBJECT
  public:
    explicit IconLabelButton(QWidget* parent = nullptr);
    void setIconLabel(const QString& label);
    void setColors(const QColor& foreground, const QColor& background, const QColor& hover);
    static QIcon themeIcon(const QString& name, const QColor& color);
    static QString stripIcons(const QString& label);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    struct Run {
        QString text;
        bool icon{}, spin{}, disabled{};
    };
    QVector<Run> runs_;
    QTimer* animation_{};
    QElapsedTimer clock_;
    bool animated_{};
    QPixmap leadingPixmap_;
    QColor leadingColor_;
    qint64 leadingKey_{};
    QSize leadingExtent_;
    qreal leadingRatio_{};
};
} // namespace litecode::ui::components
