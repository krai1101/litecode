#include "NativeImageView.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyle>
#include <QThreadPool>
#include <QtConcurrentRun>
namespace litecode::ui {
namespace {
QThreadPool* imagePool() {
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(2);
        return true;
    }();
    Q_UNUSED(configured);
    return &pool;
}
} // namespace
NativeImageView::NativeImageView(const QString& path, const QString& description, int extent,
                                 QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(extent);
    setMaximumWidth(extent);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(description.isEmpty() ? tr("Attached image") : description);
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher] {
        image_ = QPixmap::fromImage(watcher->result());
        loading_ = false;
        if (image_.isNull())
            setAccessibleDescription(tr("Image unavailable"));
        watcher->deleteLater();
        update();
    });
    const int pixels = qRound(extent * devicePixelRatioF());
    watcher->setFuture(QtConcurrent::run(imagePool(), [path, pixels] {
        const QFileInfo file(path);
        if (!file.isFile() || file.size() > 32 * 1024 * 1024)
            return QImage{};
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 100000000)
            return QImage{};
        reader.setScaledSize(size.scaled(pixels, pixels, Qt::KeepAspectRatio));
        return reader.read();
    }));
}
void NativeImageView::setOverlay(QWidget* action) {
    overlay_ = action;
    action->setProperty("nativeImageAction", true);
    action->style()->unpolish(action);
    action->style()->polish(action);
    action->move(qMax(0, width() - action->width()), 0);
    action->raise();
}
void NativeImageView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (overlay_ && overlay_->parentWidget() == this)
        overlay_->move(qMax(0, width() - overlay_->width()), 0);
}
void NativeImageView::paintEvent(QPaintEvent*) {
    const auto tokens = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
    QPainterPath clip;
    clip.addRoundedRect(bounds, 9, 9);
    painter.setClipPath(clip);
    painter.fillPath(clip, tokens.sidebarSurface);
    if (!image_.isNull()) {
        const auto size = image_.size().scaled(rect().size(), Qt::KeepAspectRatio);
        painter.drawPixmap(QRect((width() - size.width()) / 2, (height() - size.height()) / 2,
                                 size.width(), size.height()),
                           image_);
    } else if (!loading_) {
        painter.setPen(tokens.mutedForeground);
        painter.drawText(rect().adjusted(5, 5, -5, -5), Qt::AlignCenter | Qt::TextWordWrap,
                         tr("Image unavailable"));
    }
    painter.setClipping(false);
    painter.setPen(tokens.border);
    painter.drawPath(clip);
}
} // namespace litecode::ui
