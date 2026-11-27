#include "ui/debug/DebugInspectorWidget.h"
#include "ui/debug/DebugInspectorDelegates.h"
#include "ui/components/ElidingLabel.h"
#include <QDir>
#include "debug/DebugSession.h"
#include "ui/ThemedIcon.h"
#include "ui/Theme.h"
#include "ui/components/Controls.h"
#include "ui/components/TreeDisclosure.h"
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace litecode::ui {
namespace {
bool revealSkippedFrames(QTreeWidgetItem* item) {
    if (!item || !item->data(0, debug_inspector::ShowSkippedFramesRole).toBool())
        return false;
    auto* parent = item->parent();
    const int row = parent->indexOfChild(item);
    auto frames = item->takeChildren();
    delete parent->takeChild(row);
    parent->insertChildren(row, frames);
    return true;
}

void populateVariables(QTreeWidgetItem* parent, const QJsonArray& values) {
    for (const auto& value : values) {
        const auto object = value.toObject();
        const QString name = object.value("name").toString();
        const QString text = object.value("value").toString();
        auto* item = new QTreeWidgetItem(parent, {name + QStringLiteral(" = ") + text});
        item->setData(0, debug_inspector::VariableNameRole, name);
        item->setData(0, debug_inspector::VariableValueRole, text);
        const int reference = object.value("variablesReference").toInt();
        item->setData(0, debug_inspector::ReferenceRole, reference);
        if (reference > 0)
            new QTreeWidgetItem(item, {QObject::tr("Loading…")});
    }
}
} // namespace

DebugInspectorWidget::DebugInspectorWidget(debug::DebugSession& session, QWidget* parent)
    : QWidget(parent), session_(session) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* sections = new QSplitter(Qt::Vertical, this);
    sections_ = sections;
    sections->setObjectName("debugSections");
    sections->setHandleWidth(1);
    sections->setChildrenCollapsible(false);
    layout->addWidget(sections);
    const auto makeTree = [sections](const char* name, bool columns = false) {
        auto* tree = new DebugInspectorTree(sections);
        tree->setObjectName(QString::fromLatin1(name));
        tree->setHeaderHidden(true);
        tree->setFrameShape(QFrame::NoFrame);
        tree->setRootIsDecorated(columns);
        if (columns) {
            tree->setColumnCount(2);
            tree->setColumnWidth(0, 115);
        }
        return tree;
    };
    const auto addSection = [this, sections](const QString& title, QTreeWidget* tree,
                                              int weight, bool addAction = false) {
        auto* section = new QWidget(sections);
        auto* column = new QVBoxLayout(section);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        auto* bar = new QWidget(section);
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(4, 0, 8, 0);
        row->setSpacing(0);
        auto* header = new components::IconButton(bar);
        header->setObjectName("debugSectionHeader");
        header->setText(title);
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setIconSize({16, 16});
        header->setAutoRaise(true);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setThemedIcon(header, ":/icons/chevron-down.svg");
        row->addWidget(header, 1);
        if (tree == variables_) {
            auto* collapse = new components::IconButton(bar);
            collapse->setObjectName("debugCollapseVariablesButton");
            collapse->setIconSize({16, 16});
            collapse->setAccessibleName(tr("Collapse All Variables"));
            setThemedIcon(collapse, ":/icons/collapse.svg");
            row->addWidget(collapse);
            connect(collapse, &QToolButton::clicked, tree, &QTreeView::collapseAll);
        }
        if (addAction) {
            watchHeader_ = header;
            auto* add = new components::IconButton(bar);
            add->setObjectName("debugAddWatchButton");
            add->setToolTip(tr("Add Expression"));
            setThemedIcon(add, ":/icons/add.svg");
            row->addWidget(add);
            connect(add, &QToolButton::clicked, this, &DebugInspectorWidget::addWatchExpression);
        }
        column->addWidget(bar);
        auto* body = new QWidget(section);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(14, 0, 8, 0);
        bodyLayout->addWidget(tree);
        column->addWidget(body, 1);
        const bool breakpointSection = tree == breakpoints_;
        if (breakpointSection) {
            breakpointHeader_ = bar;
            breakpointBody_ = body;
        } else {
            inspectionSections_.append(section);
        }
        sections->addWidget(section);
        sections->setStretchFactor(sections->count() - 1, weight);
        connect(header, &QToolButton::clicked, this,
                [this, header, body, section, bar, breakpointSection, open = true]() mutable {
            open = !open;
            body->setVisible(open);
            section->setMaximumHeight(open ? QWIDGETSIZE_MAX : bar->sizeHint().height());
            setThemedIcon(header, open ? ":/icons/chevron-down.svg"
                                       : ":/icons/chevron-right.svg");
            if (breakpointSection)
                updateBreakpointHeight();
        });
    };
    variables_ = makeTree("debugVariables", true);
    variables_->setColumnCount(1);
    variables_->setIndentation(16);

    watch_ = makeTree("debugWatch", true);
    callStack_ = makeTree("debugCallStack", true);
    callStack_->setColumnCount(3);
    callStack_->setRootIsDecorated(false);
    callStack_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    callStack_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    callStack_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    breakpoints_ = makeTree("debugBreakpoints");
    breakpoints_->setColumnCount(3);
    breakpoints_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    breakpoints_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    breakpoints_->header()->resizeSection(1, 110);
    breakpoints_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    breakpoints_->header()->resizeSection(2, 34);
    installDebugInspectorDelegates(variables_, callStack_, breakpoints_);
    addSection(tr("Variables"), variables_, 28);
    addSection(tr("Watch"), watch_, 22, true);
    addSection(tr("Call Stack"), callStack_, 30);
    addSection(tr("Breakpoints"), breakpoints_, 20);
    sections->setSizes({280, 220, 300, 200});

    connect(&session_, &debug::DebugSession::inspectionReady, this,
            &DebugInspectorWidget::handleInspection);
    connect(&session_, &debug::DebugSession::capabilitiesChanged, this,
            [this](const QJsonObject& capabilities) {
        exceptionFilterDefinitions_ = capabilities.value("exceptionBreakpointFilters").toArray();
        enabledExceptionFilters_.clear();
        for (const auto& value : exceptionFilterDefinitions_) {
            const auto filter = value.toObject();
            if (filter.value("default").toBool())
                enabledExceptionFilters_.insert(filter.value("filter").toString());
        }
        refreshBreakpointRows();
        session_.setExceptionBreakpoints(enabledExceptionFilters_.values());
    });
    connect(&session_, &debug::DebugSession::referencesInvalidated, this,
            &DebugInspectorWidget::clearInspection);
    connect(&session_, &debug::DebugSession::stateChanged, this,
            [this](debug::DebugSession::State state) {
        if (state != debug::DebugSession::State::Stopped) {
            pauseDescription_.clear();
            clearInspection();
        }
    });
    connect(&session_, &debug::DebugSession::stoppedAt, this,
            [this](const QJsonObject& details) {
        pauseDescription_ = details.value("description").toString();
        if (pauseDescription_.isEmpty()) {
            const auto reason = details.value("reason").toString();
            pauseDescription_ = reason.isEmpty() ? tr("Paused") : tr("Paused on %1").arg(reason);
        }
    });
    connect(variables_, &QTreeWidget::itemExpanded, this, &DebugInspectorWidget::requestVariables);
    connect(watch_, &QTreeWidget::itemExpanded, this, &DebugInspectorWidget::requestVariables);
    for (auto* tree : {variables_, watch_, callStack_}) {
        components::enableTreeRowDisclosure(*tree, [tree](const QModelIndex& index) {
            return !index.data(debug_inspector::ShowSkippedFramesRole).toBool() &&
                   tree->model()->hasChildren(index);
        });
    }
    connect(callStack_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (!revealSkippedFrames(item))
            selectFrame(item);
    });
    connect(callStack_, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem* item) {
        revealSkippedFrames(item);
    });
    connect(callStack_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, debug_inspector::PathRole).toString();
        if (!path.isEmpty())
            emit openLocationRequested(path, item->data(0, debug_inspector::LineRole).toInt());
    });
    connect(breakpoints_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, debug_inspector::PathRole).toString();
        if (!path.isEmpty())
            emit openLocationRequested(path, item->data(0, debug_inspector::LineRole).toInt());
    });
    connect(breakpoints_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int) {
        const QString filter = item->data(0, debug_inspector::ExceptionFilterRole).toString();
        if (filter.isEmpty())
            return;
        if (item->checkState(0) == Qt::Checked)
            enabledExceptionFilters_.insert(filter);
        else
            enabledExceptionFilters_.remove(filter);
        session_.setExceptionBreakpoints(enabledExceptionFilters_.values());
    });
    breakpoints_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(breakpoints_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = breakpoints_->itemAt(pos);
        if (!item || item->data(0, debug_inspector::PathRole).toString().isEmpty()) return;
        components::Menu menu(this);
        auto* remove = menu.addAction(tr("Remove Breakpoint"));
        if (menu.exec(breakpoints_->viewport()->mapToGlobal(pos)) == remove)
            emit removeBreakpointRequested(item->data(0, debug_inspector::PathRole).toString(),
                                           item->data(0, debug_inspector::LineRole).toInt());
    });
    watch_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(watch_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = watch_->itemAt(pos);
        const auto expression = item && !item->parent() && item != watchDraft_
            ? item->text(0) : QString{};
        components::Menu menu(this);
        auto* add = menu.addAction(tr("Add Expression"));
        auto* remove = expression.isEmpty() ? nullptr : menu.addAction(tr("Remove Expression"));
        auto* selected = menu.exec(watch_->viewport()->mapToGlobal(pos));
        if (selected == add) addWatchExpression();
        if (remove && selected == remove) {
            watchExpressions_.removeOne(expression);
            refreshWatch();
        }
    });
    updateTreePalettes();
}

void DebugInspectorWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange)
        updateTreePalettes();
}
void DebugInspectorWidget::updateTreePalettes() {
    if (!breakpoints_) return;
    const QColor background =
        Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).sidebarSurface;
    for (auto* tree : {variables_, watch_, callStack_, breakpoints_}) {
        auto palette = tree->palette();
        if (palette.color(QPalette::Base) == background) continue;
        palette.setColor(QPalette::Base, background);
        tree->setPalette(palette);
        tree->viewport()->setPalette(palette);
    }
}

void DebugInspectorWidget::setBreakpoints(const QMap<QString, QSet<int>>& breakpoints) {
    sourceBreakpoints_ = breakpoints;
    refreshBreakpointRows();
}
void DebugInspectorWidget::setInspectionVisible(bool visible) {
    if (inspectionVisible_ == visible)
        return;
    inspectionVisible_ = visible;
    if (!visible && sections_->isVisible())
        inspectionSizes_ = sections_->sizes();
    for (auto* section : inspectionSections_)
        section->setVisible(visible);
    breakpoints_->setSizePolicy(QSizePolicy::Expanding,
                                visible ? QSizePolicy::Expanding : QSizePolicy::Ignored);
    updateBreakpointHeight();
    if (visible && !inspectionSizes_.isEmpty())
        sections_->setSizes(inspectionSizes_);
}

void DebugInspectorWidget::updateBreakpointHeight() {
    if (inspectionVisible_) {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        return;
    }
    const int rows = breakpointBody_->isHidden() ? 0 : qMin(8, breakpoints_->topLevelItemCount());
    const int rowHeight = qMax(breakpoints_->fontMetrics().height(), breakpoints_->sizeHintForRow(0));
    setFixedHeight(breakpointHeader_->sizeHint().height() + rows * rowHeight);
}

void DebugInspectorWidget::refreshBreakpointRows() {
    QSignalBlocker blocker(breakpoints_);
    breakpoints_->clear();
    for (const auto& value : exceptionFilterDefinitions_) {
        const auto filter = value.toObject();
        const QString id = filter.value("filter").toString();
        if (id.isEmpty()) continue;
        auto* item = new QTreeWidgetItem(breakpoints_,
                                         {filter.value("label").toString(id)});
        item->setFirstColumnSpanned(true);
        item->setData(0, debug_inspector::ExceptionFilterRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, enabledExceptionFilters_.contains(id) ? Qt::Checked
                                                                     : Qt::Unchecked);
        item->setToolTip(0, filter.value("description").toString());
    }
    for (auto it = sourceBreakpoints_.cbegin(); it != sourceBreakpoints_.cend(); ++it) {
        auto lines = it.value().values();
        std::sort(lines.begin(), lines.end());
        for (int line : lines) {
            auto* item = new QTreeWidgetItem(breakpoints_,
                {QFileInfo(it.key()).fileName(), QFileInfo(it.key()).path(),
                 QString::number(line)});
            item->setIcon(0, QIcon(QStringLiteral(":/litecode/editor/icons/debug-breakpoint.svg")));
            item->setData(0, debug_inspector::PathRole, it.key());
            item->setData(0, debug_inspector::LineRole, line);
            item->setToolTip(0, it.key());
            const auto directory = QFileInfo(it.key()).path();
            auto display = workspace_.isEmpty() ? directory : QDir(workspace_).relativeFilePath(directory);
            if (display == ".") display.clear();
            item->setText(1, display);
            const auto colors = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
            item->setForeground(1, colors.mutedForeground);
            item->setToolTip(1, it.key());
            item->setTextAlignment(2, Qt::AlignCenter);
        }
    }
    updateBreakpointHeight();
}
void DebugInspectorWidget::clearWorkspace() {
    finishWatchExpression(false);
    watchExpressions_.clear();
    clearInspection();
    breakpoints_->clear();
    exceptionFilterDefinitions_ = {};
    enabledExceptionFilters_.clear();
    sourceBreakpoints_.clear();
    updateBreakpointHeight();
}
void DebugInspectorWidget::clearInspection() {
    variableRequests_.clear();
    watchRequests_.clear();
    scopeRequest_ = 0;
    selectedFrame_ = -1;
    emit activeFrameChanged({}, 0);
    variables_->clear();
    callStack_->clear();
    refreshWatch();
}
void DebugInspectorWidget::selectFrame(QTreeWidgetItem* frame) {
    if (!frame || session_.state() != debug::DebugSession::State::Stopped) return;
    const auto reference = frame->data(0, debug_inspector::ReferenceRole);
    if (!reference.isValid() || reference.toInt() < 0) return;
    selectedFrame_ = reference.toInt();
    emit activeFrameChanged(frame->data(0, debug_inspector::PathRole).toString(),
                            frame->data(0, debug_inspector::LineRole).toInt());
    scopeRequest_ = session_.scopes(selectedFrame_);
    refreshWatch();
}
void DebugInspectorWidget::refreshWatch() {
    watchRequests_.clear();
    for (auto it = variableRequests_.begin(); it != variableRequests_.end();) {
        if (it.value()->treeWidget() == watch_)
            it = variableRequests_.erase(it);
        else
            ++it;
    }
    while (watch_->topLevelItemCount() > (watchDraft_ ? 1 : 0))
        delete watch_->takeTopLevelItem(0);
    int row = 0;
    for (const auto& expression : watchExpressions_) {
        auto* item = new QTreeWidgetItem({expression, {}});
        watch_->insertTopLevelItem(row++, item);
        if (selectedFrame_ >= 0 && session_.state() == debug::DebugSession::State::Stopped) {
            const int request = session_.evaluate(expression, QStringLiteral("watch"), selectedFrame_);
            if (request > 0) watchRequests_.insert(request, item);
        }
    }
}
void DebugInspectorWidget::addWatchExpression() {
    if (!watch_->isVisibleTo(this))
        watchHeader_->click();
    if (!watchDraft_) {
        watchDraft_ = new QTreeWidgetItem(watch_);
        watchDraft_->setFirstColumnSpanned(true);
        auto* input = new components::InlineInput(watch_->viewport());
        input->setObjectName("debugWatchExpressionInput");
        input->setPlaceholderText(tr("Expression to watch"));
        input->setAccessibleName(tr("Watch expression"));
        watchDraftInput_ = input;
        input->installEventFilter(this);
        watch_->setItemWidget(watchDraft_, 0, input);
        connect(input, &QLineEdit::returnPressed, this, [this] {
            finishWatchExpression(true);
            watch_->setFocus();
        });
    }
    watch_->scrollToItem(watchDraft_);
    watchDraftInput_->setFocus();
}

void DebugInspectorWidget::finishWatchExpression(bool accept) {
    if (!watchDraft_)
        return;
    const auto expression = watchDraftInput_->text().trimmed();
    watchDraftInput_ = nullptr;
    delete std::exchange(watchDraft_, nullptr);
    if (accept && !expression.isEmpty() && !watchExpressions_.contains(expression)) {
        watchExpressions_.append(expression);
        refreshWatch();
    }
}

bool DebugInspectorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == watchDraftInput_) {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishWatchExpression(false);
            watch_->setFocus();
            return true;
        }
        if (event->type() == QEvent::FocusOut &&
            static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
            finishWatchExpression(true);
    }
    return QWidget::eventFilter(watched, event);
}
void DebugInspectorWidget::requestVariables(QTreeWidgetItem* item) {
    const int reference = item->data(0, debug_inspector::ReferenceRole).toInt();
    if (reference <= 0 || item->childCount() != 1 ||
        item->child(0)->text(0) != tr("Loading…")) return;
    const int request = session_.variables(reference);
    if (request > 0) variableRequests_.insert(request, item);
}
void DebugInspectorWidget::handleInspection(int requestId, const QString& command,
                                              const QJsonObject& body) {
    if (command == "stackTrace") {
        callStack_->clear();
        callStack_->setRootIsDecorated(true);
        const auto frames = body.value("stackFrames").toArray();
        if (frames.isEmpty())
            return;
        auto* target = new QTreeWidgetItem(callStack_, {targetName_});
        target->setFirstColumnSpanned(true);
        auto* targetContent = new QWidget(callStack_);
        targetContent->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* targetLayout = new QHBoxLayout(targetContent);
        targetLayout->setContentsMargins(4, 0, 7, 0);
        targetLayout->setSpacing(5);
        auto* targetIcon = new QLabel(targetContent);
        targetIcon->setPixmap(themedIcon(":/icons/run-debug.svg",
                                  qApp->property("litecodeDarkTheme").toBool()).pixmap(14, 14));
        targetLayout->addWidget(targetIcon);
        auto* title = new components::ElidingLabel(targetName_, targetContent);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        targetLayout->addWidget(title, 1);
        auto* state = new QLabel(pauseDescription_, targetContent);
        state->setTextFormat(Qt::PlainText);
        state->setMaximumWidth(152);
        state->setObjectName("debugCallStackState");
        targetLayout->addWidget(state, 0, Qt::AlignVCenter);
        callStack_->setItemWidget(target, 0, targetContent);
        QTreeWidgetItem* firstFrame = nullptr;
        QTreeWidgetItem* skippedGroup = nullptr;
        for (const auto& value : frames) {
            const auto frame = value.toObject();
            const int line = frame.value("line").toInt();
            const int column = frame.value("column").toInt();
            const auto source = frame.value("source").toObject();
            const QString path = source.value("path").toString();
            const bool skipped = firstFrame &&
                (frame.value("presentationHint") == "deemphasize" ||
                 source.value("presentationHint") == "deemphasize");
            if (!skipped) {
                skippedGroup = nullptr;
            } else if (!skippedGroup) {
                skippedGroup = new QTreeWidgetItem(target);
                skippedGroup->setFirstColumnSpanned(true);
                skippedGroup->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
                skippedGroup->setData(0, debug_inspector::ShowSkippedFramesRole, true);
                skippedGroup->setForeground(0, Theme::tokens(
                    qApp->property("litecodeDarkTheme").toBool()).accent);
            }
            const auto origin = source.value("origin").toString();
            if (skippedGroup && skippedGroup->childCount() == 0)
                skippedGroup->setData(0, debug_inspector::FrameOriginRole, origin);
            const auto fileName = source.value("name").toString(QFileInfo(path).fileName());
            auto* item = new QTreeWidgetItem(
                {frame.value("name").toString(), QFileInfo(fileName).fileName(),
                 column > 0 ? QStringLiteral("%1:%2").arg(line).arg(column)
                            : QString::number(line)});
            if (skippedGroup) {
                skippedGroup->addChild(item);
                if (skippedGroup->data(0, debug_inspector::FrameOriginRole).toString() != origin)
                    skippedGroup->setData(0, debug_inspector::FrameOriginRole, QString{});
                const auto sharedOrigin = skippedGroup->data(0, debug_inspector::FrameOriginRole).toString();
                skippedGroup->setText(0, sharedOrigin.isEmpty()
                    ? tr("Show %1 more frames").arg(skippedGroup->childCount())
                    : tr("Show %1 more: %2").arg(skippedGroup->childCount()).arg(sharedOrigin));
                item->setForeground(0, Theme::tokens(
                    qApp->property("litecodeDarkTheme").toBool()).mutedForeground);
            } else {
                target->addChild(item);
                if (!firstFrame)
                    firstFrame = item;
            }
            QFont nameFont(QStringLiteral("Consolas"));
            nameFont.setStyleHint(QFont::Monospace);
            nameFont.setPixelSize(13);
            item->setFont(0, nameFont);
            item->setForeground(1, Theme::tokens(
                qApp->property("litecodeDarkTheme").toBool()).mutedForeground);
            item->setTextAlignment(2, Qt::AlignCenter);
            item->setData(0, debug_inspector::ReferenceRole, frame.value("id").toInt());
            item->setData(0, debug_inspector::PathRole, path);
            item->setData(0, debug_inspector::LineRole, line);
            item->setToolTip(0, path);
        }
        if (firstFrame) {
            target->setExpanded(true);
            callStack_->setCurrentItem(firstFrame);
            selectFrame(firstFrame);
        }
    } else if (command == "scopes" && requestId == scopeRequest_) {
        variables_->clear();
        bool expandedFirstScope = false;
        for (const auto& value : body.value("scopes").toArray()) {
            const auto scope = value.toObject();
            auto* item = new QTreeWidgetItem(variables_, {scope.value("name").toString(), {}});
            QFont scopeFont = item->font(0);
            scopeFont.setBold(true);
            scopeFont.setPixelSize(11);
            item->setFont(0, scopeFont);
            const int reference = scope.value("variablesReference").toInt();
            item->setData(0, debug_inspector::ReferenceRole, reference);
            if (reference > 0) {
                new QTreeWidgetItem(item, {tr("Loading…")});
                if (!expandedFirstScope && !scope.value("expensive").toBool()) {
                    item->setExpanded(true);
                    expandedFirstScope = true;
                }
            }
        }
    } else if (command == "variables") {
        QTreeWidgetItem* item = variableRequests_.take(requestId);
        if (!item) return;
        while (item->childCount()) delete item->takeChild(0);
        populateVariables(item, body.value("variables").toArray());
    } else if (command == "evaluate") {
        QTreeWidgetItem* item = watchRequests_.take(requestId);
        if (!item) return;
        item->setText(1, body.value("result").toString());
        const int reference = body.value("variablesReference").toInt();
        item->setData(0, debug_inspector::ReferenceRole, reference);
        if (reference > 0) new QTreeWidgetItem(item, {tr("Loading…")});
    }
}
} // namespace litecode::ui
