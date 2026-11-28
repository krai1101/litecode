#include "ui/extensions/NativeUriDispatcher.h"
#include "extensions/UriWire.h"
#include <QApplication>
#include <QDesktopServices>
#include <QFileOpenEvent>
#include <QTimer>
#include <QWidget>
#include <algorithm>
namespace litecode::ui {
NativeUriDispatcher& NativeUriDispatcher::forApplication() {
    static QPointer<NativeUriDispatcher> instance;
    if (!instance)
        instance = new NativeUriDispatcher(qApp);
    return *instance;
}
NativeUriDispatcher::NativeUriDispatcher(QObject* parent) : QObject(parent) {
    clock_.start();
    qApp->installEventFilter(this);
    QDesktopServices::setUrlHandler(extensions::wire::AppUriScheme, this, "dispatchUri");
}
NativeUriDispatcher::~NativeUriDispatcher() {
    QDesktopServices::unsetUrlHandler(extensions::wire::AppUriScheme);
}
void NativeUriDispatcher::registerWindow(QWidget& window, QObject& lifetime,
                                         std::function<bool(const QUrl&)> handler) {
    receivers_.append({&window, &lifetime, std::move(handler), ++nextOrder_});
    window.installEventFilter(this);
    connect(&lifetime, &QObject::destroyed, this, [this, identity = &lifetime] {
        receivers_.erase(std::remove_if(receivers_.begin(), receivers_.end(),
                                        [identity](const Receiver& receiver) {
                                            return !receiver.lifetime ||
                                                   receiver.lifetime == identity;
                                        }),
                         receivers_.end());
    });
    // Defer replay until the service constructor and signal wiring are done.
    QTimer::singleShot(0, this, [this] {
        auto pending = std::move(pending_);
        pending_.clear();
        for (const auto& item : pending) {
            if (clock_.elapsed() - item.received < 5 * 60 * 1000)
                dispatchUri(item.uri);
        }
    });
}
bool NativeUriDispatcher::dispatchUri(const QUrl& uri) {
    if (!uri.isValid() ||
        uri.scheme().compare(extensions::wire::AppUriScheme, Qt::CaseInsensitive) != 0)
        return false;
    const Receiver* selected = nullptr;
    auto* active = qApp->activeWindow();
    for (const auto& receiver : receivers_) {
        if (!receiver.window || !receiver.lifetime)
            continue;
        if (!selected || receiver.window == active ||
            (selected->window != active && receiver.order > selected->order))
            selected = &receiver;
    }
    // Copy before calling extension/workbench code; callbacks may remove windows.
    if (selected) {
        const auto handler = selected->handler;
        return handler(uri);
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [this](const Pending& item) {
                                      return clock_.elapsed() - item.received >= 5 * 60 * 1000;
                                  }),
                   pending_.end());
    if (pending_.size() >= 128 || uri.toEncoded().size() > 64 * 1024) {
        emit routingError(
            tr("Too many pending URI callbacks or callback exceeds the routing limit."));
        return false;
    }
    pending_.append({uri, clock_.elapsed()});
    return true;
}
bool NativeUriDispatcher::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qApp && event->type() == QEvent::FileOpen) {
        const auto uri = static_cast<QFileOpenEvent*>(event)->url();
        if (uri.scheme().compare(extensions::wire::AppUriScheme, Qt::CaseInsensitive) == 0) {
            dispatchUri(uri);
            return true;
        }
    }
    if (event->type() == QEvent::WindowActivate) {
        bool registered = false;
        for (auto& receiver : receivers_)
            if (receiver.window == watched) {
                receiver.order = ++nextOrder_;
                registered = true;
            }
        if (registered)
            emit windowActivated();
    }
    return QObject::eventFilter(watched, event);
}
} // namespace litecode::ui
