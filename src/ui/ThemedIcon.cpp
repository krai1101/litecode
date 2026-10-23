#include "ui/ThemedIcon.h"

#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QPainter>
#include <QVariant>

#include <array>

static bool initializeUiResources() {
    Q_INIT_RESOURCE(resources);
    return true;
}

static const bool UiResourcesInitialized = initializeUiResources();

namespace litecode::ui {
namespace {

constexpr auto iconResourceProperty = "litecodeIconResource";

bool currentThemeIsDark() {
    const QVariant property = qApp->property("litecodeDarkTheme");
    return property.isValid() ? property.toBool() : true;
}

void tintPixmap(QIcon& destination, const QIcon& source, int extent, const QColor& color,
                QIcon::Mode mode = QIcon::Normal) {
    QPixmap pixmap = source.pixmap(QSize(extent, extent));
    if (pixmap.isNull()) {
        return;
    }
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();
    destination.addPixmap(pixmap, mode);
}

} // namespace

QIcon themedIcon(const QString& resourcePath, bool dark) {
    (void)UiResourcesInitialized;
    const QIcon source(resourcePath);
    if (source.isNull()) {
        return {};
    }
    QIcon result;
    const QColor color = Theme::tokens(dark).iconForeground;
    for (const int extent : std::array{14, 16, 17, 18, 20, 24, 32}) {
        tintPixmap(result, source, extent, color);
    }
    return result;
}

QIcon tintedIcon(const QString& resourcePath, const QColor& color, const QColor& activeColor) {
    (void)UiResourcesInitialized;
    const QIcon source(resourcePath);
    if (source.isNull())
        return {};
    QIcon result;
    for (const int extent : std::array{14, 16, 17, 18, 20, 24, 32}) {
        tintPixmap(result, source, extent, color);
        tintPixmap(result, source, extent, activeColor, QIcon::Active);
    }
    return result;
}

void setThemedIcon(QAbstractButton* button, const QString& resourcePath) {
    button->setProperty(iconResourceProperty, resourcePath);
    button->setIcon(themedIcon(resourcePath, currentThemeIsDark()));
}

void setThemedIcon(QAction* action, const QString& resourcePath) {
    action->setProperty(iconResourceProperty, resourcePath);
    action->setIcon(themedIcon(resourcePath, currentThemeIsDark()));
}

void refreshThemedIcons(QObject* root, bool dark) {
    const auto refresh = [dark](QObject* object) {
        const QString resource = object->property(iconResourceProperty).toString();
        if (resource.isEmpty()) {
            return;
        }
        const QIcon icon = themedIcon(resource, dark);
        if (auto* button = qobject_cast<QAbstractButton*>(object)) {
            button->setIcon(icon);
        } else if (auto* action = qobject_cast<QAction*>(object)) {
            action->setIcon(icon);
        }
    };
    refresh(root);
    for (QObject* child : root->findChildren<QObject*>()) {
        refresh(child);
    }
}

} // namespace litecode::ui
