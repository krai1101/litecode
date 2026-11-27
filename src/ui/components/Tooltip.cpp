#include "ui/components/Tooltip.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>

namespace litecode::ui::components {
namespace {
enum class TooltipEdge { None, Top, Bottom, Left, Right };
constexpr int pointerDepth = 5;

class TooltipLabel final : public QLabel {
  public:
    explicit TooltipLabel(QWidget* owner)
        : QLabel(owner, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                            Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setObjectName("workbenchTooltip");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setContentsMargins(9, 4, 9, 4);
    }

    void setPointer(TooltipEdge edge, int offset = 0) {
        edge_ = edge;
        offset_ = offset;
        setContentsMargins(9 + (edge == TooltipEdge::Left ? pointerDepth : 0),
                           4 + (edge == TooltipEdge::Top ? pointerDepth : 0),
                           9 + (edge == TooltipEdge::Right ? pointerDepth : 0),
                           4 + (edge == TooltipEdge::Bottom ? pointerDepth : 0));
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            auto border = palette().color(QPalette::ToolTipText);
            border.setAlphaF(0.16f);
            painter.setPen(border);
            painter.setBrush(palette().color(QPalette::ToolTipBase));
            const QRectF box = QRectF(rect()).adjusted(
                .5 + (edge_ == TooltipEdge::Left ? pointerDepth : 0),
                .5 + (edge_ == TooltipEdge::Top ? pointerDepth : 0),
                -.5 - (edge_ == TooltipEdge::Right ? pointerDepth : 0),
                -.5 - (edge_ == TooltipEdge::Bottom ? pointerDepth : 0));
            QPainterPath outline;
            if (edge_ == TooltipEdge::None) {
                outline.addRoundedRect(box, 8, 8);
            } else {
                outline.addRect(box);
                QPolygonF pointer;
                if (edge_ == TooltipEdge::Top || edge_ == TooltipEdge::Bottom) {
                    const qreal x = qBound(box.left() + 10, qreal(offset_), box.right() - 10);
                    const qreal y = edge_ == TooltipEdge::Top ? box.top() : box.bottom();
                    const qreal direction = edge_ == TooltipEdge::Top ? -1 : 1;
                    pointer = {{x - 4, y - direction}, {x, y + direction * pointerDepth},
                               {x + 4, y - direction}};
                } else {
                    const qreal x = edge_ == TooltipEdge::Left ? box.left() : box.right();
                    const qreal y = qBound(box.top() + 9, qreal(offset_), box.bottom() - 9);
                    const qreal direction = edge_ == TooltipEdge::Left ? -1 : 1;
                    pointer = {{x - direction, y - 4}, {x + direction * pointerDepth, y},
                               {x - direction, y + 4}};
                }
                QPainterPath notch;
                notch.addPolygon(pointer);
                notch.closeSubpath();
                outline = outline.united(notch);
            }
            painter.drawPath(outline);
        }
        QLabel::paintEvent(event);
    }

  private:
    TooltipEdge edge_{TooltipEdge::None};
    int offset_{};
};

struct TooltipContent {
    QString text;
    QRect anchor;
};

TooltipContent contentFor(QWidget& widget, const QPoint& local) {
    if (auto* tabs = qobject_cast<QTabBar*>(&widget)) {
        const int index = tabs->tabAt(local);
        if (index >= 0 && !tabs->tabToolTip(index).isEmpty())
            return {tabs->tabToolTip(index), tabs->tabRect(index)};
    }
    if (auto* view = qobject_cast<QAbstractItemView*>(widget.parentWidget());
        view && view->viewport() == &widget) {
        const auto index = view->indexAt(local);
        const auto text = index.data(Qt::ToolTipRole).toString();
        if (!text.isEmpty())
            return {text, view->visualRect(index).intersected(widget.rect())};
    }
    return {widget.toolTip(), widget.rect()};
}

class TooltipController final : public QObject {
  public:
    explicit TooltipController(QApplication& application) : QObject(&application) {
        setObjectName("workbenchTooltips");
        dismissTimer_.setSingleShot(true);
        connect(&dismissTimer_, &QTimer::timeout, this, [this] { hide(); });
        application.installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            auto* anchor = qobject_cast<QWidget*>(watched);
            const auto* help = dynamic_cast<QHelpEvent*>(event);
            if (!anchor || !help || anchor->property("customTooltip").toBool())
                return false;
            const auto content = contentFor(*anchor, help->pos());
            if (content.text.isEmpty())
                return false;
            show(*anchor, content, help->globalPos());
            return true;
        }
        if (!tip_ || !tip_->isVisible())
            return false;
        switch (event->type()) {
        case QEvent::MouseMove:
            if (!anchorBounds_.contains(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()))
                hide();
            break;
        case QEvent::Leave:
            if (watched == anchor_ && !anchorBounds_.contains(QCursor::pos()))
                hide();
            break;
        case QEvent::Hide:
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowDeactivate:
            if (anchor_ && (watched == anchor_ || watched == anchor_->window()))
                hide();
            break;
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::ApplicationStateChange:
            hide();
            break;
        default:
            break;
        }
        return false;
    }

  private:
    void hide() {
        dismissTimer_.stop();
        if (tip_)
            tip_->hide();
        anchor_ = nullptr;
    }

    void show(QWidget& anchor, const TooltipContent& content, const QPoint& pointer) {
        auto* screen = QGuiApplication::screenAt(pointer);
        if (!screen)
            screen = anchor.screen();
        if (!screen)
            return;
        const QRect available = screen->availableGeometry().adjusted(6, 6, -6, -6);
        if (available.isEmpty())
            return;
        if (!tip_ || tip_->parentWidget() != anchor.window()) {
            delete tip_.data();
            tip_ = new TooltipLabel(anchor.window());
        }
        const auto palette = qApp->palette();
        tip_->setPalette(palette);
        const QString textStyle =
            QStringLiteral("background: transparent; color: %1;")
                .arg(palette.color(QPalette::ToolTipText).name());
        if (tip_->styleSheet() != textStyle)
            tip_->setStyleSheet(textStyle);
        tip_->setPointer(TooltipEdge::None);
        tip_->setText(content.text);
        tip_->setWordWrap(false);
        const auto natural = tip_->sizeHint();
        const int width = qMin(natural.width(), qMin(400, available.width()));
        const bool wrap = width < natural.width();
        tip_->setWordWrap(wrap);
        const int height = qBound(1, wrap ? tip_->heightForWidth(width) : natural.height(),
                                  available.height());
        anchorBounds_ = QRect(anchor.mapToGlobal(content.anchor.topLeft()), content.anchor.size());
        const int maxX = available.right() - width + 1;
        const int maxY = available.bottom() - height + 1;
        int x = qBound(available.left(), anchorBounds_.center().x() - width / 2, maxX);
        const int below = anchorBounds_.bottom() + 7;
        const int above = anchorBounds_.top() - height - 6;
        const bool opensBelow = below <= maxY;
        int y = qBound(available.top(), opensBelow ? below : above, maxY);
        auto edge = opensBelow ? TooltipEdge::Top : TooltipEdge::Bottom;
        const auto* toolbar = qobject_cast<QToolBar*>(anchor.parentWidget());
        if (toolbar && toolbar->orientation() == Qt::Vertical) {
            const int right = anchorBounds_.right() + 4;
            const int left = anchorBounds_.left() - width - 3;
            if (right <= maxX || left >= available.left()) {
                x = right <= maxX ? right : left;
                y = qBound(available.top(), anchorBounds_.center().y() - height / 2, maxY);
                edge = right <= maxX ? TooltipEdge::Left : TooltipEdge::Right;
            }
        }
        anchor_ = &anchor;
        QRect bounds(x, y, width, height);
        if (qobject_cast<QAbstractButton*>(&anchor)) {
            QRect pointed = bounds;
            switch (edge) {
            case TooltipEdge::Top: pointed.adjust(0, -pointerDepth, 0, 0); break;
            case TooltipEdge::Bottom: pointed.adjust(0, 0, 0, pointerDepth); break;
            case TooltipEdge::Left: pointed.adjust(-pointerDepth, 0, 0, 0); break;
            case TooltipEdge::Right: pointed.adjust(0, 0, pointerDepth, 0); break;
            case TooltipEdge::None: break;
            }
            const bool horizontal = edge == TooltipEdge::Left || edge == TooltipEdge::Right;
            // A side pointer can enter the activity button's transparent margin.
            if (available.contains(pointed) &&
                (horizontal || !pointed.intersects(anchorBounds_))) {
                const QPoint offset = anchorBounds_.center() - pointed.topLeft();
                tip_->setPointer(edge, horizontal ? offset.y() : offset.x());
                bounds = pointed;
            }
        }
        tip_->setGeometry(bounds);
        tip_->show();
        const int duration = anchor.toolTipDuration();
        dismissTimer_.start(duration > 0 ? duration
                                         : int(qMin(qsizetype{10000}, 4000 + 40 * content.text.size())));
    }

    QPointer<TooltipLabel> tip_;
    QPointer<QWidget> anchor_;
    QRect anchorBounds_;
    QTimer dismissTimer_;
};
} // namespace

void installTooltips(QApplication& application) {
    if (!application.findChild<QObject*>("workbenchTooltips", Qt::FindDirectChildrenOnly))
        new TooltipController(application);
}
} // namespace litecode::ui::components
