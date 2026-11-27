#include "ui/components/PanelHeader.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTextBlock>
namespace litecode::ui::components {
namespace {
class ActiveToolbarStack final : public QStackedWidget {
  public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override {
        return currentWidget() ? currentWidget()->sizeHint() : QSize();
    }
    QSize minimumSizeHint() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QSize();
    }
};
class PanelFilterInput final : public Input {
  public:
    using Input::Input;
    QSize sizeHint() const override {
        auto size = Input::sizeHint();
        size.setWidth(210);
        return size;
    }
};
} // namespace
PanelHeader::PanelHeader(QWidget* parent) : QWidget(parent) {
    setObjectName("panelHeader");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 6, 0);
    row->setSpacing(2);
    tools_ = new ActiveToolbarStack(this);
    empty_ = new QWidget(tools_);
    tools_->addWidget(empty_);
    row->addWidget(tools_, 1);
    maximize_ = new IconLabelButton(this);
    maximize_->setFixedSize(24, 24);
    maximize_->setObjectName("panelMaximize");
    row->addWidget(maximize_);
    setMaximized(false);
    auto* close = new IconLabelButton(this);
    close->setObjectName("panelClose");
    close->setIconLabel("$(close)");
    close->setFixedSize(24, 24);
    close->setToolTip(tr("Close Panel"));
    close->setAccessibleName(close->toolTip());
    row->addWidget(close);
    connect(close, &QToolButton::clicked, this, &PanelHeader::closeRequested);
    connect(maximize_, &QToolButton::clicked, this, &PanelHeader::maximizeRequested);
}
void PanelHeader::setTools(QWidget* tools) {
    if (tools && tools_->indexOf(tools) < 0)
        tools_->addWidget(tools);
    tools_->setCurrentWidget(tools ? tools : empty_);
    tools_->updateGeometry();
    updateGeometry();
}
void PanelHeader::setMaximized(bool maximized) {
    maximize_->setIconLabel(maximized ? "$(screen-normal)" : "$(screen-full)");
    maximize_->setToolTip(maximized ? tr("Restore Panel Size") : tr("Maximize Panel Size"));
    maximize_->setAccessibleName(maximize_->toolTip());
}
void filterPanelText(QPlainTextEdit& text, const QString& filter) {
    QStringList include, exclude;
    for (auto term : filter.split(',', Qt::SkipEmptyParts)) {
        term = term.trimmed();
        if (term.startsWith('!')) {
            if (term.size() > 1)
                exclude.append(term.mid(1));
        } else if (!term.isEmpty())
            include.append(term);
    }
    for (auto block = text.document()->begin(); block.isValid(); block = block.next()) {
        const auto value = block.text();
        bool visible = include.isEmpty();
        for (const auto& term : include)
            visible |= value.contains(term, Qt::CaseInsensitive);
        for (const auto& term : exclude)
            visible &= !value.contains(term, Qt::CaseInsensitive);
        block.setVisible(visible);
        block.setLineCount(visible ? 1 : 0);
    }
    text.document()->markContentsDirty(0, text.document()->characterCount());
    text.viewport()->update();
}
QWidget* createTextPanelTools(QPlainTextEdit& text, const QString& name, QWidget* parent) {
    auto* tools = new QWidget(parent);
    auto* row = new QHBoxLayout(tools);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto* filter = new PanelFilterInput(tools);
    filter->setObjectName(name + "Filter");
    filter->setAccessibleName(QObject::tr("Filter output"));
    filter->setPlaceholderText(QObject::tr("Filter (e.g. text, !exclude)"));
    filter->setMinimumWidth(80);
    filter->setMaximumWidth(250);
    filter->setFixedHeight(24);
    row->addWidget(filter, 1);
    auto* clear = new IconLabelButton(tools);
    clear->setObjectName(name + "Clear");
    clear->setIconLabel("$(clear-all)");
    clear->setFixedSize(24, 24);
    clear->setToolTip(QObject::tr("Clear Output"));
    clear->setAccessibleName(clear->toolTip());
    row->addWidget(clear);
    QObject::connect(clear, &QToolButton::clicked, &text, &QPlainTextEdit::clear);
    QObject::connect(filter, &QLineEdit::textChanged, &text,
                     [&text, filter] { filterPanelText(text, filter->text()); });
    QObject::connect(&text, &QPlainTextEdit::textChanged, filter, [&text, filter] {
        if (!filter->text().isEmpty())
            filterPanelText(text, filter->text());
    });
    return tools;
}
} // namespace litecode::ui::components
