#include "ui/TerminalPanel.h"

#include "ui/components/Controls.h"
#include "ui/components/TerminalFindControls.h"

#include "terminal/ITerminalBackend.h"
#include "terminal/TerminalSessionService.h"
#include "ui/PopupPositioner.h"
#include "ui/TerminalSessionDelegate.h"
#include "ui/TerminalView.h"
#include "ui/ThemedIcon.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace litecode::ui {
namespace {

QString compactProfileName(const QString& profileId) {
    if (profileId == QStringLiteral("command-prompt"))
        return QStringLiteral("cmd");
    if (profileId == QStringLiteral("powershell"))
        return QStringLiteral("powershell");
    if (profileId == QStringLiteral("git-bash") || profileId == QStringLiteral("bash"))
        return QStringLiteral("bash");
    if (profileId == QStringLiteral("default-shell"))
        return QStringLiteral("shell");
    return profileId;
}

} // namespace

struct TerminalPanel::Session final {
    core::OperationId id;
    QString profileId;
    QString displayName;
    QString workingDirectory;
    QString foregroundProcessName;
    TerminalView* view{};
    QSize backendSize;
};

TerminalPanel::TerminalPanel(QWidget* parent, BackendFactory backendFactory)
    : QWidget(parent), workingDirectory_(QDir::homePath()),
      sessionService_(
          std::make_unique<terminal::TerminalSessionService>(nullptr, std::move(backendFactory))) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    controls_ = new QWidget(this);
    controls_->setObjectName(QStringLiteral("terminalToolbar"));
    auto* toolbarLayout = new QHBoxLayout(controls_);
    toolbarLayout->setContentsMargins(0, 0, 6, 0);
    toolbarLayout->setSpacing(2);
    const auto addTool = [this, toolbarLayout](const QString& icon, const QString& tooltip) {
        auto* button = new components::IconButton(controls_);
        button->setObjectName(QStringLiteral("terminalToolButton"));
        setThemedIcon(button, icon);
        button->setIconSize(QSize(17, 17));
        button->setToolTip(tooltip);
        toolbarLayout->addWidget(button);
        return button;
    };
    auto* newSession = addTool(QStringLiteral(":/icons/add.svg"), tr("New Terminal"));
    auto* profileMenu = new components::Menu(controls_);
    for (const terminal::TerminalProfile& profile : sessionService_->profiles()) {
        QAction* action = profileMenu->addAction(profile.displayName);
        action->setData(profile.id);
        connect(action, &QAction::triggered, this,
                [this, action] { startWithProfile(action->data().toString()); });
    }
    auto* profileButton =
        addTool(QStringLiteral(":/icons/chevron-down.svg"), tr("Select Terminal Profile"));
    profileMenu->setObjectName(QStringLiteral("terminalProfileMenu"));
    profileButton->setObjectName(QStringLiteral("terminalDropdownButton"));
    profileButton->setAccessibleName(tr("Select Terminal Profile"));
    profileButton->setIconSize(QSize(12, 12));
    connect(profileButton, &QToolButton::clicked, this, [this, profileButton, profileMenu] {
        PopupPositioner::popupMenu(*profileMenu, profileButton, window(),
                                   PopupVerticalPlacement::BelowFirst,
                                   PopupHorizontalPlacement::AlignRight);
    });
    deleteSession_ = addTool(QStringLiteral(":/icons/clear.svg"), tr("Kill Terminal"));
    auto* maximize = addTool(QStringLiteral(":/icons/maximize.svg"), tr("Toggle Maximized Panel"));
    auto* closePanel = addTool(QStringLiteral(":/icons/close.svg"), tr("Close Panel"));

    auto* terminalBody = new QWidget(this);
    terminalBody->setObjectName(QStringLiteral("terminalBody"));
    auto* bodyLayout = new QHBoxLayout(terminalBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    outputs_ = new QStackedWidget(terminalBody);
    outputs_->setObjectName(QStringLiteral("terminalSessions"));
    bodyLayout->addWidget(outputs_, 1);
    sessionList_ = new components::List(terminalBody);
    sessionList_->setObjectName(QStringLiteral("terminalSessionList"));
    sessionList_->setAccessibleName(tr("Open terminals"));
    sessionList_->setAccessibleDescription(tr("Select a terminal. Press Delete to kill it."));
    sessionList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sessionList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sessionList_->setIconSize(QSize(16, 16));
    sessionList_->setSpacing(0);
    sessionList_->setUniformItemSizes(true);
    sessionList_->setTextElideMode(Qt::ElideRight);
    sessionList_->setMouseTracking(true);
    sessionList_->setItemDelegate(
        new TerminalSessionDelegate([this](int index) { removeSession(index); }, sessionList_));
    sessionList_->installEventFilter(this);
    sessionList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        sessionList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            const QModelIndex index = sessionList_->indexAt(pos);
            if (!index.isValid())
                return;
            components::Menu menu(sessionList_);
            QAction* kill = menu.addAction(themedIcon(QStringLiteral(":/icons/clear.svg"),
                                                      qApp->property("litecodeDarkTheme").toBool()),
                                           tr("Kill Terminal"));
            if (PopupPositioner::execMenuAt(menu, sessionList_->viewport()->mapToGlobal(pos),
                                            window()) == kill) {
                removeSession(index.row());
            }
        });
    sessionList_->setFixedWidth(220);
    sessionList_->hide();
    bodyLayout->addWidget(sessionList_);
    layout->addWidget(terminalBody, 1);

    const components::TerminalFindControls findControls =
        components::createTerminalFindControls(this);
    findBar_ = findControls.bar;
    findInput_ = findControls.input;
    findCount_ = findControls.countLabel;
    matchCase_ = findControls.matchCase;
    wholeWord_ = findControls.wholeWord;
    regularExpression_ = findControls.regularExpression;
    QToolButton* previous = findControls.previousButton;
    QToolButton* next = findControls.nextButton;
    QToolButton* closeFind = findControls.closeButton;
    findInput_->installEventFilter(this);

    connect(findInput_, &QLineEdit::textChanged, this, &TerminalPanel::updateFind);
    connect(matchCase_, &QToolButton::toggled, this, &TerminalPanel::updateFind);
    connect(wholeWord_, &QToolButton::toggled, this, &TerminalPanel::updateFind);
    connect(regularExpression_, &QToolButton::toggled, this, &TerminalPanel::updateFind);
    connect(previous, &QToolButton::clicked, this, [this] {
        if (Session* session = activeSession())
            session->view->findNext(true);
    });
    connect(next, &QToolButton::clicked, this, [this] {
        if (Session* session = activeSession())
            session->view->findNext();
    });
    connect(closeFind, &QToolButton::clicked, this, [this] {
        findBar_->hide();
        if (Session* session = activeSession()) {
            session->view->clearSearch();
            session->view->setFocus();
        }
    });

    resizeTimer_ = new QTimer(this);
    resizeTimer_->setSingleShot(true);
    resizeTimer_->setInterval(40);
    connect(resizeTimer_, &QTimer::timeout, this, [this] {
        if (Session* session = activeSession())
            resizeBackend(*session);
    });
    connect(sessionList_, &QListWidget::currentRowChanged, this,
            [this](int row) { activateSession(row); });
    connect(newSession, &QToolButton::clicked, this, &TerminalPanel::start);
    connect(deleteSession_, &QToolButton::clicked, this, [this] {
        if (activeSessionIndex_ >= 0)
            removeSession(activeSessionIndex_);
    });
    connect(maximize, &QToolButton::clicked, this, &TerminalPanel::toggleMaximizeRequested);
    connect(closePanel, &QToolButton::clicked, this, &TerminalPanel::closePanelRequested);

    connect(sessionService_.get(), &terminal::TerminalSessionService::sessionStarted, this,
            [this](core::OperationId id) {
                Session* session = findSession(id);
                if (!session)
                    return;
                resizeBackend(*session);
                if (session == activeSession())
                    session->view->setFocus();
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::outputReady, this,
            [this](core::OperationId id, const QByteArray& bytes) {
                if (Session* session = findSession(id))
                    session->view->feed(bytes);
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::foregroundProcessChanged,
            this, [this](core::OperationId id, const QString& processName) {
                if (Session* session = findSession(id)) {
                    session->foregroundProcessName = processName;
                    rebuildSessionList();
                }
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::sessionError, this,
            [this](core::OperationId id, const QString& message) {
                if (Session* session = findSession(id))
                    session->view->feed(message.toUtf8() + QByteArrayLiteral("\r\n"));
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::inputRejected, this,
            [this](core::OperationId id, const QString& message) {
                if (Session* session = findSession(id))
                    session->view->feed(tr("\r\n[input rejected: %1]\r\n").arg(message).toUtf8());
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::sessionCompleted, this,
            [this](const terminal::TerminalExit& result) {
                if (Session* session = findSession(result.operationId)) {
                    session->view->feed(
                        tr("\r\n[terminal exited with code %1]\r\n").arg(result.exitCode).toUtf8());
                }
            });
    connect(sessionService_.get(), &terminal::TerminalSessionService::sessionRemoved, this,
            &TerminalPanel::removeSessionView);
}

TerminalPanel::~TerminalPanel() {
    // Stopping a backend may synchronously emit completion. Disconnect before the
    // session views and session list start being destroyed.
    QObject::disconnect(sessionService_.get(), nullptr, this, nullptr);
    sessionService_.reset();
}

QWidget* TerminalPanel::controlsWidget() const { return controls_; }

void TerminalPanel::setWorkingDirectory(const QString& path) {
    const QFileInfo directory(path);
    if (directory.exists() && directory.isDir()) {
        workingDirectory_ = directory.canonicalFilePath().isEmpty() ? directory.absoluteFilePath()
                                                                    : directory.canonicalFilePath();
    }
}

void TerminalPanel::openInDirectory(const QString& path) {
    const QFileInfo directory(path);
    if (!directory.exists() || !directory.isDir())
        return;
    const QString resolved = directory.canonicalFilePath().isEmpty()
                                 ? directory.absoluteFilePath()
                                 : directory.canonicalFilePath();
    startWithProfile(defaultProfileId_.isEmpty() ? sessionService_->profiles().constFirst().id
                                                 : defaultProfileId_,
                     resolved);
}

void TerminalPanel::showFind() {
    findBar_->show();
    positionFindBar();
    findBar_->raise();
    findInput_->setFocus();
    findInput_->selectAll();
    updateFind();
}

void TerminalPanel::newTerminal() { start(); }

void TerminalPanel::setDefaultProfile(const QString& profileId) {
    const auto found = std::find_if(
        sessionService_->profiles().cbegin(), sessionService_->profiles().cend(),
        [&profileId](const terminal::TerminalProfile& profile) { return profile.id == profileId; });
    defaultProfileId_ = found == sessionService_->profiles().cend() ? QString{} : profileId;
}

void TerminalPanel::setTerminalFont(const QFont& font) {
    terminalFont_ = font;
    for (const auto& session : sessions_)
        session->view->setTerminalFont(terminalFont_);
}

bool TerminalPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == findInput_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        QWidget* frame = findInput_->parentWidget();
        frame->setProperty("focused", event->type() == QEvent::FocusIn);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
    }
    if (watched == sessionList_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) &&
            key->modifiers() == Qt::NoModifier) {
            removeSession(sessionList_->currentRow());
            return true;
        }
    }
    if (watched == findInput_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            findBar_->hide();
            if (Session* session = activeSession()) {
                session->view->clearSearch();
                session->view->setFocus();
            }
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (Session* session = activeSession())
                session->view->findNext(key->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalPanel::start() {
    startWithProfile(defaultProfileId_.isEmpty() ? sessionService_->profiles().constFirst().id
                                                 : defaultProfileId_);
}

bool TerminalPanel::hasTerminals() const { return !sessions_.empty(); }

void TerminalPanel::startWithProfile(const QString& profileId, const QString& workingDirectory) {
    const auto profile =
        std::find_if(sessionService_->profiles().cbegin(), sessionService_->profiles().cend(),
                     [&profileId](const terminal::TerminalProfile& candidate) {
                         return candidate.id == profileId;
                     });
    if (profile == sessionService_->profiles().cend())
        return;
    const QString sessionWorkingDirectory =
        workingDirectory.isEmpty() ? workingDirectory_ : workingDirectory;
    const core::OperationId id = sessionService_->createSession(profileId, sessionWorkingDirectory);
    auto session = std::make_unique<Session>();
    session->id = id;
    session->profileId = profile->id;
    session->displayName = profile->displayName;
    session->workingDirectory = sessionWorkingDirectory;
    session->view = new TerminalView(outputs_);
    session->view->setTerminalFont(terminalFont_);
    session->view->setAccessibleName(tr("%1 terminal output and input").arg(profile->displayName));
    connect(session->view, &TerminalView::inputGenerated, this,
            [this, id](const QByteArray& bytes) { (void)sessionService_->sendInput(id, bytes); });
    connect(session->view, &TerminalView::terminalSizeChanged, this, [this, id](const QSize&) {
        if (findSession(id) == activeSession())
            scheduleTerminalResize();
    });
    connect(session->view, &TerminalView::terminalTitleChanged, this,
            [this](const QString&) { rebuildSessionList(); });
    connect(session->view, &TerminalView::searchResultChanged, this,
            [this, view = session->view](int current, int total) {
                if (Session* active = activeSession(); active && active->view == view)
                    updateFindResult(current, total);
            });
    connect(session->view, &TerminalView::linkActivated, this,
            [this, id](const QString& target, int line, int column, bool external) {
                const QUrl url(target);
                if (external && !url.isLocalFile()) {
                    const QString scheme = url.scheme().toLower();
                    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
                        scheme == QStringLiteral("mailto"))
                        QDesktopServices::openUrl(url);
                    return;
                }
                QString path = url.isLocalFile() ? url.toLocalFile() : target;
                if (QDir::isRelativePath(path)) {
                    const Session* source = findSession(id);
                    if (!source)
                        return;
                    path = QDir(source->workingDirectory).absoluteFilePath(path);
                }
                const QFileInfo info(QDir::cleanPath(path));
                if (info.exists() && info.isFile())
                    emit fileLinkActivated(info.absoluteFilePath(), std::max(1, line),
                                           std::max(1, column));
            });
    outputs_->addWidget(session->view);
    sessions_.push_back(std::move(session));
    activateSession(static_cast<int>(sessions_.size()) - 1);
    // createSession defers process launch until the next event-loop turn. Give the backend
    // the visible view's dimensions now so startup output uses the correct wrapping width.
    resizeBackend(*sessions_.back());
    rebuildSessionList();
}

void TerminalPanel::removeSession(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size()))
        return;
    sessionService_->requestRemove(sessions_.at(static_cast<size_t>(index))->id);
}

void TerminalPanel::removeSessionView(core::OperationId id) {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                    [id](const auto& session) { return session->id == id; });
    if (found == sessions_.end())
        return;
    const int index = static_cast<int>(std::distance(sessions_.begin(), found));
    const int previousActive = activeSessionIndex_;
    QWidget* view = (*found)->view;
    outputs_->removeWidget(view);
    sessions_.erase(found);
    delete view;
    if (sessions_.empty()) {
        activeSessionIndex_ = -1;
        rebuildSessionList();
        emit closePanelRequested();
        return;
    } else {
        const int next = index == previousActive
                             ? std::min(index, static_cast<int>(sessions_.size()) - 1)
                             : previousActive - (index < previousActive ? 1 : 0);
        activateSession(next);
    }
    rebuildSessionList();
}

void TerminalPanel::activateSession(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size()))
        return;
    activeSessionIndex_ = index;
    Session& session = *sessions_.at(static_cast<size_t>(index));
    outputs_->setCurrentWidget(session.view);
    const QSignalBlocker blocker(sessionList_);
    sessionList_->setCurrentRow(index);
    session.view->setFocus();
    if (findBar_->isVisible())
        updateFind();
    scheduleTerminalResize();
}

void TerminalPanel::rebuildSessionList() {
    const QSignalBlocker blocker(sessionList_);
    sessionList_->clear();
    for (const auto& session : sessions_) {
        const QString sequenceTitle = session->view->engine()->title().trimmed();
        const QString visibleName =
            !session->foregroundProcessName.isEmpty()
                ? session->foregroundProcessName
                : (!sequenceTitle.isEmpty() ? sequenceTitle
                                            : compactProfileName(session->profileId));
        auto* item = new QListWidgetItem(themedIcon(QStringLiteral(":/icons/terminal.svg"),
                                                    qApp->property("litecodeDarkTheme").toBool()),
                                         visibleName, sessionList_);
        item->setToolTip(sequenceTitle.isEmpty() ? session->displayName : sequenceTitle);
    }
    sessionList_->setCurrentRow(activeSessionIndex_);
    sessionList_->setVisible(sessions_.size() > 1);
    deleteSession_->setVisible(sessions_.size() == 1);
}

TerminalPanel::Session* TerminalPanel::activeSession() {
    if (activeSessionIndex_ < 0 || activeSessionIndex_ >= static_cast<int>(sessions_.size()))
        return nullptr;
    return sessions_.at(static_cast<size_t>(activeSessionIndex_)).get();
}

TerminalPanel::Session* TerminalPanel::findSession(core::OperationId id) {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                    [id](const auto& session) { return session->id == id; });
    return found == sessions_.end() ? nullptr : found->get();
}

void TerminalPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionFindBar();
    scheduleTerminalResize();
}

void TerminalPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    scheduleTerminalResize();
    QTimer::singleShot(0, this, [this] {
        if (sessions_.empty()) {
            start();
            return;
        }
        if (Session* session = activeSession())
            session->view->setFocus();
    });
}

void TerminalPanel::scheduleTerminalResize() {
    if (resizeTimer_ && isVisible())
        resizeTimer_->start();
}

void TerminalPanel::resizeBackend(Session& session) {
    if (!session.view->isVisible())
        return;
    const QSize characters = session.view->terminalSize();
    if (characters.width() < 20 || characters.height() < 2 || characters == session.backendSize)
        return;
    session.backendSize = characters;
    sessionService_->resizeSession(session.id, characters);
}

void TerminalPanel::updateFind() {
    if (Session* session = activeSession()) {
        session->view->setSearch(findInput_->text(), matchCase_->isChecked(),
                                 wholeWord_->isChecked(), regularExpression_->isChecked());
    } else {
        updateFindResult(0, 0);
    }
}

void TerminalPanel::updateFindResult(int current, int total) {
    findCount_->setText(total > 0 ? tr("%1 of %2").arg(current).arg(total) : tr("No results"));
}

void TerminalPanel::positionFindBar() {
    if (!findBar_)
        return;
    const int available =
        sessionList_ && sessionList_->isVisible() ? width() - sessionList_->width() : width();
    const int barWidth = std::max(160, std::min(available - 24, 419));
    findBar_->setGeometry(std::max(8, available - barWidth - 12), 8, barWidth, 35);
}

} // namespace litecode::ui
