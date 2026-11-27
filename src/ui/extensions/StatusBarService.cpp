#include "ui/extensions/StatusBarService.h"
#include "ui/components/IconLabelButton.h"
#include "ui/components/Controls.h"
#include "ui/components/ComponentAccessibility.h"
#include "ui/components/CssColor.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTextDocument>
#include <QToolButton>
#include <QRegularExpression>
#include <algorithm>
#include <type_traits>
#include <QAction>

namespace litecode::ui {
namespace {
constexpr auto hiddenKey = "workbench/statusbar/hidden";
class TooltipDocument final : public QTextDocument {
  protected:
    QVariant loadResource(int, const QUrl&) override { return {}; }
};
QString tooltipText(const extensions::StatusBarTooltip& tooltip) {
    if (const auto* text = std::get_if<QString>(&tooltip)) return "<qt>" + text->toHtmlEscaped().replace('\n', "<br>") + "</qt>";
    TooltipDocument document;
    document.setMarkdown(std::get<extensions::MarkdownString>(tooltip).value, QTextDocument::MarkdownNoHTML);
    auto html = document.toHtml();
    // The receiving native tooltip has its own document. Remove image tags
    // rather than handing it local/network resource URLs from an extension.
    html.remove(QRegularExpression("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
    return html;
}
QColor themeColor(const QString& id, const ThemeTokens& theme) {
    if (id == "statusBarItem.errorBackground") return theme.error;
    if (id == "statusBarItem.warningBackground") return theme.warning;
    if (id == "statusBarItem.errorForeground" || id == "statusBarItem.warningForeground") return theme.onAccent;
    if (id == "statusBar.foreground" || id == "statusBarItem.foreground" || id == "foreground") return theme.foreground;
    if (id == "statusBar.background") return theme.panelSurface;
    if (id == "errorForeground") return theme.error;
    if (id == "descriptionForeground") return theme.mutedForeground;
    return {}; // Undefined theme colors inherit; never interpret IDs as CSS.
}
}
StatusBarService::StatusBarService(extensions::WorkbenchStatusBar& backend, QStatusBar& bar, QObject* parent)
    : QObject(parent), backend_(&backend), bar_(&bar) {
    components::installComponentAccessibility();
    const auto saved = QSettings().value(hiddenKey).toStringList();
    for (const auto& id : saved) hidden_.insert(id);
    left_ = new QWidget(&bar); right_ = new QWidget(&bar);
    left_->setObjectName("extensionStatusLeft"); right_->setObjectName("extensionStatusRight");
    leftLayout_ = new QHBoxLayout(left_); rightLayout_ = new QHBoxLayout(right_);
    for (auto* layout : {leftLayout_, rightLayout_}) { layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0); }
    leftLayout_->addStretch();
    bar.insertPermanentWidget(0, left_, 1); bar.insertPermanentWidget(1, right_);
    left_->hide(); right_->hide();
    connect(&backend, &extensions::WorkbenchStatusBar::changed, this, &StatusBarService::refresh);
    connect(&backend, &QObject::destroyed, this, [this] { refresh(); });
    bar.installEventFilter(this);
    qApp->installEventFilter(this);
    refresh();
}
StatusBarService::~StatusBarService() {
    if (bar_) { bar_->removeWidget(left_); bar_->removeWidget(right_); }
    delete left_.data(); delete right_.data();
}
void StatusBarService::refresh() {
    if (!bar_ || !left_ || !right_) return;
    const auto entries = backend_ ? backend_->entries() : QVector<extensions::StatusBarEntryInfo>{};
    QSet<QString> alive;
    for (const auto& entry : entries) alive.insert(entry.key);
    for (auto button = buttons_.begin(); button != buttons_.end();) {
        if (!alive.contains(button.key())) { delete button.value().data(); button = buttons_.erase(button); }
        else ++button;
    }
    const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    int leftIndex = 0, rightIndex = 0; bool leftVisible = false, rightVisible = false;
    for (const auto& entry : entries) {
        auto* button = static_cast<components::IconLabelButton*>(buttons_.value(entry.key).data());
        if (!button) {
            button = new components::IconLabelButton(left_);
            button->setObjectName("extensionStatusItem"); button->setFocusPolicy(Qt::StrongFocus);
            button->setProperty("uiComponent", "statusBarItem");
            button->setProperty("statusBarKey", entry.key);
            button->installEventFilter(this);
            connect(button, &QToolButton::clicked, this, [this, key = entry.key] { if (backend_) backend_->invoke(key); });
            buttons_.insert(entry.key, button);
        }
        auto text = entry.state.text; text.replace('\r', ' '); text.replace('\n', ' ');
        button->setIconLabel(text);
        button->setToolTip(entry.state.tooltip ? tooltipText(*entry.state.tooltip) : QString{});
        button->setAccessibleName(entry.state.accessibilityInformation ? entry.state.accessibilityInformation->label
            : components::IconLabelButton::stripIcons(entry.state.text));
        button->setAccessibleDescription(entry.name);
        button->setProperty("accessibilityRole", entry.state.accessibilityInformation
            ? entry.state.accessibilityInformation->role.value_or(QString{}) : QString{});
        button->setProperty("statusBarInteractive", entry.state.command.has_value());
        button->setCursor(entry.state.command ? Qt::PointingHandCursor : Qt::ArrowCursor);
        auto foreground = theme.foreground; QColor background(Qt::transparent);
        if (entry.state.color) std::visit([&](const auto& color) {
            using T = std::decay_t<decltype(color)>;
            QColor resolved;
            if constexpr (std::is_same_v<T, QString>) resolved = components::cssColor(color);
            else resolved = themeColor(color.id(), theme);
            if (resolved.isValid()) foreground = resolved;
        }, *entry.state.color);
        if (entry.state.backgroundColor) {
            background = themeColor(entry.state.backgroundColor->id(), theme);
            foreground = theme.onAccent;
        }
        button->setColors(foreground, background, entry.state.command ? theme.hover : background);
        button->style()->unpolish(button); button->style()->polish(button);
        leftLayout_->removeWidget(button); rightLayout_->removeWidget(button);
        const bool isLeft = entry.state.alignment == extensions::StatusBarAlignment::Left;
        (isLeft ? leftLayout_ : rightLayout_)->insertWidget(isLeft ? leftIndex++ : rightIndex++, button);
        const bool visible = !hidden_.contains(entry.id);
        button->setVisible(visible);
        if (isLeft) leftVisible |= visible; else rightVisible |= visible;
    }
    left_->setVisible(leftVisible); right_->setVisible(rightVisible);
}
void StatusBarService::showMenu(const QPoint& position) {
    if (!backend_ || !bar_) return;
    auto* menu = new components::Menu(bar_); menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto entries = backend_->entries(); QSet<QString> added;
    for (const auto& entry : entries) {
        if (added.contains(entry.id)) continue;
        added.insert(entry.id);
        auto* action = menu->addAction(entry.name); action->setCheckable(true); action->setChecked(!hidden_.contains(entry.id));
        connect(action, &QAction::toggled, this, [this, id = entry.id](bool visible) {
            if (visible) hidden_.remove(id); else hidden_.insert(id);
            QStringList ids; for (const auto& hidden : hidden_) ids.append(hidden);
            std::sort(ids.begin(), ids.end()); QSettings().setValue(hiddenKey, ids); refresh();
        });
    }
    if (menu->actions().isEmpty()) { delete menu; return; }
    menu->popup(position);
}
bool StatusBarService::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange) {
        if (watched == qApp) refresh();
    }
    if (event->type() == QEvent::ContextMenu && (watched == bar_ || watched->property("statusBarKey").isValid())) {
        showMenu(static_cast<QContextMenuEvent*>(event)->globalPos()); return true;
    }
    return QObject::eventFilter(watched, event);
}
}
