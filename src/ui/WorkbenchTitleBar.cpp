#include "ui/WorkbenchTitleBar.h"

#include "ui/components/ActionBar.h"

#include "ui/Theme.h"
#include "ui/ThemedIcon.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace litecode::ui {

WorkbenchTitleBar::WorkbenchTitleBar(QMenuBar* menuBar, QWidget* window)
    : QWidget(window), window_(window), menuBar_(menuBar) {
    setObjectName(QStringLiteral("workbenchTitleBar"));
    setFixedHeight(ThemeMetrics::navigationBarHeight);
    setAttribute(Qt::WA_StyledBackground, false);

    menuBar_->setParent(this);
    menuBar_->setObjectName(QStringLiteral("workbenchMenuBar"));
    menuBar_->setNativeMenuBar(false);
    menuBar_->setFixedHeight(ThemeMetrics::navigationBarHeight);

    auto* appMark = new QLabel(this);
    appMark->setObjectName(QStringLiteral("appMark"));
    appMark->setPixmap(QIcon(QStringLiteral(":/icons/litecode.svg")).pixmap(QSize(16, 16)));
    appMark->setAlignment(Qt::AlignCenter);
    appMark->setFixedSize(35, ThemeMetrics::navigationBarHeight);

    auto* controls = new components::ActionBar(this);
    controls_ = controls;
    controls_->setObjectName(QStringLiteral("titleControls"));
    controls_->setFixedHeight(ThemeMetrics::navigationBarHeight);

    const auto addLayoutButton = [this, controls](const QString& icon, const QString& tooltip) {
        auto* button = controls->addIconButton(
            QIcon(), tooltip, QSize(32, ThemeMetrics::navigationBarHeight), QSize(16, 16));
        button->setAutoRaise(true);
        button->setObjectName(QStringLiteral("titleLayoutButton"));
        button->setAccessibleName(tooltip);
        button->setProperty("titleBarIconResource", icon);
        updateButtonIcon(button);
        return button;
    };
    auto* primary = addLayoutButton(QStringLiteral(":/icons/layout-sidebar-left.svg"),
                                    tr("Toggle Primary Side Bar"));
    primary->setObjectName(QStringLiteral("togglePrimarySidebar"));
    connect(primary, &QToolButton::clicked, this,
            &WorkbenchTitleBar::togglePrimarySidebarRequested);
    connect(addLayoutButton(QStringLiteral(":/icons/layout-panel.svg"), tr("Toggle Panel")),
            &QToolButton::clicked, this, &WorkbenchTitleBar::toggleBottomPanelRequested);
    auto* secondary = addLayoutButton(QStringLiteral(":/icons/layout-sidebar-right.svg"),
                                      tr("Toggle Secondary Side Bar"));
    secondary->setObjectName(QStringLiteral("toggleSecondarySidebar"));
    connect(secondary, &QToolButton::clicked, this,
            &WorkbenchTitleBar::toggleSecondarySidebarRequested);
    controls->addSpacing(8);

    const auto addWindowButton = [this, controls](const QString& icon, const QString& name,
                                                  const QString& tooltip) {
        auto* button = controls->addIconButton(
            QIcon(), tooltip, QSize(46, ThemeMetrics::navigationBarHeight), QSize(16, 16));
        button->setAutoRaise(true);
        button->setObjectName(name);
        button->setAccessibleName(tooltip);
        button->setProperty("titleBarIconResource", icon);
        updateButtonIcon(button);
        return button;
    };
    auto* minimize = addWindowButton(QStringLiteral(":/icons/window-minimize.svg"),
                                     QStringLiteral("windowMinimize"), tr("Minimize"));
    maximize_ = addWindowButton(QStringLiteral(":/icons/window-maximize.svg"),
                                QStringLiteral("windowMaximize"), tr("Maximize"));
    close_ = addWindowButton(QStringLiteral(":/icons/window-close.svg"),
                             QStringLiteral("windowClose"), tr("Close"));
    connect(minimize, &QToolButton::clicked, window_, &QWidget::showMinimized);
    connect(maximize_, &QToolButton::clicked, this, &WorkbenchTitleBar::toggleMaximizeRestore);
    connect(close_, &QToolButton::clicked, window_, &QWidget::close);
    controls_->setFixedWidth(controls->sizeHint().width());
    setDarkTheme(qApp->property("litecodeDarkTheme").toBool());
    qApp->installEventFilter(this);
}

WorkbenchTitleBar::~WorkbenchTitleBar() { qApp->removeEventFilter(this); }

bool WorkbenchTitleBar::eventFilter(QObject* watched, QEvent* event) {
    auto* popup = QApplication::activePopupWidget();
    if (event->type() != QEvent::MouseButtonPress || !popup || !window_->isVisible() ||
        !close_->isVisible() || !close_->isEnabled())
        return QWidget::eventFilter(watched, event);
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const auto position = mouse->globalPosition().toPoint();
    if (mouse->button() != Qt::LeftButton ||
        !QRect(close_->mapToGlobal(QPoint{}), close_->size()).contains(position) ||
        QRect(popup->mapToGlobal(QPoint{}), popup->size()).contains(position))
        return QWidget::eventFilter(watched, event);
    bool owned = false;
    for (auto* owner = popup->parentWidget(); owner; owner = owner->parentWidget())
        if (owner == window_) {
            owned = true;
            break;
        }
    if (!owned && QApplication::activeWindow() != window_)
        return QWidget::eventFilter(watched, event);

    // Qt grabs outside mouse clicks for popups. Handle a caption Close click
    // before the popup consumes it, canceling menus without activating an item.
    const QPointer<QWidget> target(window_);
    while (auto* active = QApplication::activePopupWidget()) {
        active->close();
        if (QApplication::activePopupWidget() == active)
            return true;
    }
    if (target)
        QTimer::singleShot(0, target.data(), [target] {
            if (target)
                target->close(); // Keep the normal unsaved-document close checks.
        });
    return true;
}

bool WorkbenchTitleBar::containsInteractivePoint(const QPoint& point) const {
    return menuBar_->geometry().contains(point) || controls_->geometry().contains(point);
}

void WorkbenchTitleBar::setDarkTheme(bool dark) {
    const ThemeTokens tokens = Theme::tokens(dark);

    QPalette menuPalette = menuBar_->palette();
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        menuPalette.setColor(group, QPalette::Window, tokens.appSurface);
        menuPalette.setColor(group, QPalette::Button, tokens.appSurface);
        menuPalette.setColor(group, QPalette::Base, tokens.appSurface);
        menuPalette.setColor(group, QPalette::WindowText, tokens.foreground);
        menuPalette.setColor(group, QPalette::Text, tokens.foreground);
        menuPalette.setColor(group, QPalette::ButtonText, tokens.foreground);
        menuPalette.setColor(group, QPalette::Highlight, tokens.hover);
        menuPalette.setColor(group, QPalette::HighlightedText, tokens.foreground);
    }
    menuPalette.setColor(QPalette::Disabled, QPalette::WindowText, tokens.mutedForeground);
    menuPalette.setColor(QPalette::Disabled, QPalette::Text, tokens.mutedForeground);
    menuPalette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens.mutedForeground);
    menuBar_->setPalette(menuPalette);

    if (controls_ != nullptr) {
        for (QToolButton* button : controls_->findChildren<QToolButton*>())
            updateButtonIcon(button);
    }

    menuBar_->update();
    update();
}

void WorkbenchTitleBar::updateWindowState() {
    maximize_->setProperty("titleBarIconResource",
                           window_->isMaximized() ? QStringLiteral(":/icons/window-restore.svg")
                                                  : QStringLiteral(":/icons/window-maximize.svg"));
    updateButtonIcon(maximize_);
    maximize_->setToolTip(window_->isMaximized() ? tr("Restore") : tr("Maximize"));
    maximize_->setAccessibleName(maximize_->toolTip());
}

void WorkbenchTitleBar::updateButtonIcon(QToolButton* button) {
    const QString resource = button->property("titleBarIconResource").toString();
    const ThemeTokens tokens = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    const QColor foreground = tokens.titleBarForeground;
    const QColor active =
        button->objectName() == QStringLiteral("windowClose") ? tokens.onAccent : foreground;
    button->setIcon(tintedIcon(resource, foreground, active));
}

void WorkbenchTitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChrome();
}

void WorkbenchTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window_->windowHandle() != nullptr) {
        window_->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WorkbenchTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleMaximizeRestore();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WorkbenchTitleBar::toggleMaximizeRestore() {
    const bool maximized =
        window_->isMaximized() || window_->windowState().testFlag(Qt::WindowMaximized);
#ifdef Q_OS_WIN
    // Drive the custom caption button through the native window-state operation. Combining
    // setWindowState() and showNormal() can briefly retain WindowMinimized on a frameless Qt
    // window, which makes Restore behave like Minimize on Windows.
    const HWND handle = reinterpret_cast<HWND>(window_->winId());
    ShowWindow(handle, maximized ? SW_RESTORE : SW_MAXIMIZE);
    if (maximized)
        SetForegroundWindow(handle);
#else
    if (maximized) {
        window_->showNormal();
        window_->raise();
        window_->activateWindow();
    } else {
        window_->showMaximized();
    }
#endif
}

void WorkbenchTitleBar::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    const auto palette = Theme::workbenchPalette(qApp->property("litecodeDarkTheme").toBool());
    QPainter painter(this);
    painter.fillRect(rect(), palette.titleSurface);
    painter.setPen(palette.titleSeparator);
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void WorkbenchTitleBar::layoutChrome() {
    const int menuWidth = menuBar_->sizeHint().width();
    menuBar_->setGeometry(35, 0, menuWidth, height());
    const int controlsLeft = width() - controls_->width();
    controls_->setGeometry(controlsLeft, 0, controls_->width(), height());
}

} // namespace litecode::ui
