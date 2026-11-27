#include "ui/components/IconLabelButton.h"
#include "ui/ThemedIcon.h"
#include "ui/Theme.h"
#include "ui/styles/ComponentStyleSheets.h"
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHash>
#include <QHideEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTimer>

namespace litecode::ui::components {
void IconLabelButton::setColors(const QColor& foreground, const QColor& background,
                                const QColor& hover) {
    setStyleSheet(ComponentStyleSheets::iconLabelButtonColors(foreground, background, hover));
}
namespace {
const QHash<QString, QString>& glyphs() {
    static const auto result = [] {
        // Ensure the resources unit is linked even in isolated widget tests.
        static_cast<void>(themedIcon(QStringLiteral(":/icons/refresh.svg"), false));
        QHash<QString, QString> map;
        QFile file(QStringLiteral(":/icons/codicon.css"));
        if (!file.open(QIODevice::ReadOnly))
            return map;
        const auto css = QString::fromUtf8(file.readAll());
        const QRegularExpression expression(QStringLiteral(
            R"rx(\.codicon-([A-Za-z0-9-]+):before\s*\{\s*content:\s*"\\([0-9a-fA-F]+)")rx"));
        auto matches = expression.globalMatch(css);
        while (matches.hasNext()) {
            const auto match = matches.next();
            bool valid = false;
            const auto code = match.captured(2).toUInt(&valid, 16);
            if (valid && code <= 0xffff)
                map.insert(match.captured(1), QString(QChar(static_cast<char16_t>(code))));
        }
        return map;
    }();
    return result;
}
QFont iconFont(int pixels) {
    static const QString family = [] {
        static_cast<void>(glyphs());
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/icons/codicon.ttf"));
        return QFontDatabase::applicationFontFamilies(id).value(0);
    }();
    QFont font(family);
    font.setPixelSize(pixels);
    return font;
}
} // namespace
IconLabelButton::IconLabelButton(QWidget* parent)
    : IconButton(parent), animation_(new QTimer(this)) {
    setIconSize(QSize(16, 16));
    clock_.start();
    animation_->setInterval(50); // Codicons: 30 steps in a 1.5 second rotation.
    connect(animation_, &QTimer::timeout, this, [this] { update(); });
    setContentsMargins(4, 0, 4, 0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
QIcon IconLabelButton::themeIcon(const QString& name, const QColor& color) {
    const auto glyph = glyphs().value(name);
    if (glyph.isEmpty())
        return {};
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(iconFont(28));
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, glyph);
    painter.end();
    return QIcon(pixmap);
}
QString IconLabelButton::stripIcons(const QString& label) {
    const QRegularExpression expression(
        QStringLiteral(R"rx((\s)?(\\)?\$\([A-Za-z0-9-]+(?:~[A-Za-z]+)?\)(\s)?)rx"));
    auto matches = expression.globalMatch(label);
    QString result;
    qsizetype offset = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += label.mid(offset, match.capturedStart() - offset);
        result += !match.captured(2).isEmpty()   ? match.captured(0)
                  : !match.captured(1).isEmpty() ? match.captured(1)
                                                 : match.captured(3);
        offset = match.capturedEnd();
    }
    return result + label.mid(offset);
}
void IconLabelButton::setIconLabel(const QString& label) {
    if (text() == label)
        return;
    setText(label); // Keep the original value available to accessibility/tests.
    runs_.clear();
    animated_ = false;
    const QRegularExpression expression(
        QStringLiteral(R"rx((\\)?\$\(([A-Za-z0-9-]+)(?:~([A-Za-z]+))?\))rx"));
    auto matches = expression.globalMatch(label);
    qsizetype offset = 0;
    QString aria;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto plain = label.mid(offset, match.capturedStart() - offset);
        if (!plain.isEmpty()) {
            runs_.append({plain, false, false, false});
            aria += plain;
        }
        if (!match.captured(1).isEmpty()) {
            const auto literal = match.captured(0).mid(1);
            runs_.append({literal, false, false, false});
            aria += literal;
        } else {
            const auto name = match.captured(2);
            const auto modifier = match.captured(3);
            const bool spin =
                modifier == "spin" && (name == "sync" || name == "loading" || name == "gear");
            runs_.append({glyphs().value(name), true, spin, modifier == "disabled"});
            animated_ = animated_ || spin;
            aria += " " + name + " ";
        }
        offset = match.capturedEnd();
    }
    if (offset < label.size()) {
        const auto tail = label.mid(offset);
        runs_.append({tail, false, false, false});
        aria += tail;
    }
    setAccessibleName(aria.trimmed());
    if (animated_ && isVisible())
        animation_->start();
    else
        animation_->stop();
    updateGeometry();
    update();
}
QSize IconLabelButton::sizeHint() const {
    const QFontMetrics metrics(font());
    qint64 width = contentsMargins().left() + contentsMargins().right();
    if (!icon().isNull())
        width += iconSize().width() + (runs_.isEmpty() ? 0 : 6);
    for (const auto& run : runs_)
        width += run.icon ? iconSize().width() : metrics.horizontalAdvance(run.text);
    return QSize(static_cast<int>(qMin<qint64>(width, 4096)),
                 qMax(iconSize().height(), metrics.height()) + 4);
}
QSize IconLabelButton::minimumSizeHint() const { return QSize(24, sizeHint().height()); }
void IconLabelButton::paintEvent(QPaintEvent*) {
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = {};
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
    // Qt suppresses styled hover panels for disabled tool buttons. Optional
    // visual feedback must not change the action's enabled state or text colour.
    if (!isEnabled() && underMouse() && property("uiDisabledHoverFeedback").toBool()) {
        const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.hover);
        painter.drawRoundedRect(QRectF(rect()), 8, 8);
        painter.restore();
    }
    const auto area = contentsRect();
    painter.setClipRect(area);
    int x = runs_.size() == 1 && runs_.first().icon && !contentsMargins().isNull()
                ? (width() - iconSize().width()) / 2
                : area.left();
    const QFontMetrics metrics(font());
    const auto color =
        palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
    if (!icon().isNull()) {
        const auto extent = iconSize();
        if (runs_.isEmpty() && !contentsMargins().isNull())
            x = (width() - extent.width()) / 2;
        const auto ratio = devicePixelRatioF();
        if (leadingKey_ != icon().cacheKey() || leadingColor_ != color ||
            leadingExtent_ != extent || leadingRatio_ != ratio) {
            // QIcon takes a logical size here. Passing pre-scaled pixels applies
            // display scaling twice and clips the resulting oversized image.
            leadingPixmap_ = icon().pixmap(extent, ratio);
            QPainter tint(&leadingPixmap_);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(leadingPixmap_.rect(), color);
            tint.end();
            leadingKey_ = icon().cacheKey();
            leadingColor_ = color;
            leadingExtent_ = extent;
            leadingRatio_ = ratio;
        }
        painter.drawPixmap(
            QRect(x, (height() - extent.height()) / 2, extent.width(), extent.height()),
            leadingPixmap_);
        x += extent.width() + (runs_.isEmpty() ? 0 : 6);
    }
    const auto glyphFont = iconFont(iconSize().height());
    for (const auto& run : runs_) {
        if (x > area.right())
            break;
        painter.save();
        painter.setPen(color);
        if (run.disabled)
            painter.setOpacity(0.5);
        if (run.icon) {
            painter.setFont(glyphFont);
            if (run.spin) {
                painter.translate(x + iconSize().width() / 2.0, height() / 2.0);
                painter.rotate((clock_.elapsed() / 50 % 30) * 12);
                painter.drawText(QRect(-iconSize().width() / 2, -iconSize().height() / 2,
                                       iconSize().width(), iconSize().height()),
                                 Qt::AlignCenter, run.text);
            } else
                painter.drawText(QRect(x, (height() - iconSize().height()) / 2, iconSize().width(),
                                       iconSize().height()),
                                 Qt::AlignCenter, run.text);
            x += iconSize().width();
        } else {
            painter.setFont(font());
            const auto visibleText =
                metrics.elidedText(run.text, Qt::ElideRight, qMax(0, area.right() - x + 1));
            painter.drawText(QPoint(x, (height() - metrics.height()) / 2 + metrics.ascent()),
                             visibleText);
            x += metrics.horizontalAdvance(run.text);
        }
        painter.restore();
    }
}
void IconLabelButton::showEvent(QShowEvent* event) {
    IconButton::showEvent(event);
    if (animated_)
        animation_->start();
}
void IconLabelButton::hideEvent(QHideEvent* event) {
    animation_->stop();
    IconButton::hideEvent(event);
}
} // namespace litecode::ui::components
