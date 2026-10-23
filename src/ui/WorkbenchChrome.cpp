#include "ui/WorkbenchChrome.h"

#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QToolButton>

namespace litecode::ui {
namespace {

class WorkbenchStyle final : public QProxyStyle {
  public:
    explicit WorkbenchStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override {
        if (hint == QStyle::SH_ComboBox_Popup && widget != nullptr &&
            widget->property("modernComboBox").toBool())
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override {
        if (element == QStyle::PE_IndicatorDockWidgetResizeHandle) {
            // Keep Qt's one-pixel dock resize hit target, but do not let the native style paint
            // its full-height/full-width guide through the workbench.
            return;
        }
        if (element == QStyle::PE_FrameTabBarBase && widget != nullptr &&
            widget->objectName() == QStringLiteral("editorTabBar")) {
            // Native Windows/Fusion styles draw a dark base line across the complete editor tab
            // strip. LiteCode supplies its own neutral strip edge in QSS so the dark accent stays
            // confined to the selected filename tab.
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override {
        if (metric == QStyle::PM_SubMenuOverlap)
            return 0;
        if (metric == QStyle::PM_DockWidgetSeparatorExtent) {
            // Dock divider hit testing is implemented by MainWindow's transparent resizer.
            // A zero native extent prevents Qt from reserving and exposing layout grid lines.
            return 0;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

} // namespace

QStyle* sharedWorkbenchStyle() {
    static QStyle* style = [] {
        QStyle* baseStyle = QStyleFactory::create(QApplication::style()->objectName());
        if (baseStyle == nullptr) {
            baseStyle = QStyleFactory::create(QStringLiteral("Fusion"));
        }
        auto* proxy = new WorkbenchStyle(baseStyle);
        proxy->setParent(qApp);
        return proxy;
    }();
    return style;
}

QIcon workbenchActivityIcon(const QString& normal, const QString& active) {
    QIcon icon;
    icon.addFile(normal, {}, QIcon::Normal, QIcon::Off);
    icon.addFile(active, {}, QIcon::Normal, QIcon::On);
    return icon;
}

QWidget* createWorkbenchDockHeader(QDockWidget* dock, const QString& title, QLabel** titleLabel,
                                   bool closable) {
    auto* header = new QWidget(dock);
    header->setObjectName(QStringLiteral("dockHeader"));
    header->setFixedHeight(ThemeMetrics::navigationBarHeight);
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(12, 0, 5, 0);
    layout->setSpacing(2);
    auto* label = new QLabel(title, header);
    label->setObjectName(QStringLiteral("dockHeaderTitle"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setMinimumHeight(ThemeMetrics::navigationBarHeight);
    layout->addWidget(label);
    layout->addStretch();
    if (closable) {
        auto* close = new components::IconButton(header);
        close->setObjectName(QStringLiteral("dockHeaderButton"));
        setThemedIcon(close, QStringLiteral(":/icons/close.svg"));
        close->setIconSize(QSize(17, 17));
        close->setToolTip(QObject::tr("Close"));
        QObject::connect(close, &QToolButton::clicked, dock, &QDockWidget::hide);
        layout->addWidget(close);
    }
    if (titleLabel != nullptr) {
        *titleLabel = label;
    }
    return header;
}

} // namespace litecode::ui
