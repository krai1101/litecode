#include "ui/extensions/ExternalUriService.h"
#include "extensions/UriWire.h"
#include "ui/components/ModalDialogQueue.h"
#include "ui/extensions/NativeUriDispatcher.h"
#include "ui/extensions/UriConfirmationDialog.h"
#include <QDesktopServices>
#include <QDialog>
#include <QSettings>
#include <QUrlQuery>
#include <exception>
#include <limits>
namespace litecode::ui {
namespace {
constexpr auto trustedKey = "extensionUrlHandler/confirmedExtensions";
QString uriIdentity(const QString& owner) { return owner.toCaseFolded(); }
} // namespace
ExternalUriService::ExternalUriService(extensions::ExtensionManager& manager, QWidget* window,
                                       QObject* parent)
    : QObject(parent), manager_(&manager), window_(window) {
    if (window)
        modalQueue_ = &ModalDialogQueue::forWindow(*window);
    if (window)
        NativeUriDispatcher::forApplication().registerWindow(
            *window, *this, [this](const QUrl& uri) { return handleIncoming(uri); });
    connect(&manager.externalUris(), &extensions::WorkbenchExternalUris::requested, this,
            &ExternalUriService::perform);
    connect(&manager, &extensions::ExtensionManager::uriError, this,
            [this](const QString&, const QString& error) { emit this->error(error); });
}
ExternalUriService::~ExternalUriService() { delete dialog_.data(); }
void ExternalUriService::perform(const extensions::ExternalUriRequest& request) {
    if (!manager_)
        return;
    const QPointer<ExternalUriService> self(this);
    const auto manager = manager_;
    try {
        if (!request.open) {
            auto result = request.uri;
            const auto scheme = result.scheme().toLower();
            if (scheme != "http" && scheme != "https" && scheme != extensions::wire::AppUriScheme) {
                result = QUrl(extensions::wire::AppUriScheme + "://workspace");
                // Encode the entire already-encoded URI as one query value.
                // This preserves nested % escapes, '&' and authentication state.
                result.setQuery("uri=" + QString::fromLatin1(QUrl::toPercentEncoding(
                                             extensions::wire::encodeUri(request.uri))),
                                QUrl::StrictMode);
            }
            manager->externalUris().complete(request.owner, request.id,
                                             extensions::wire::encodeUri(result));
            return;
        }
        // Local desktop hosts need no tunnel. Remote transports must provide a
        // resolver rather than claiming a local no-op when remoteName is set.
        const bool ownScheme =
            request.uri.scheme().compare(extensions::wire::AppUriScheme, Qt::CaseInsensitive) == 0;
        const bool opened =
            ownScheme ? handleIncoming(request.uri) : QDesktopServices::openUrl(request.uri);
        if (self && manager)
            manager->externalUris().complete(request.owner, request.id, opened);
    } catch (const std::exception&) {
        if (manager)
            manager->externalUris().complete(request.owner, request.id, {},
                                             "Could not open or resolve external URI.");
    } catch (...) {
        if (manager)
            manager->externalUris().complete(request.owner, request.id, {},
                                             "Could not open or resolve external URI.");
    }
}
bool ExternalUriService::handleIncoming(const QUrl& uri) {
    if (!manager_ || !window_ || !modalQueue_ ||
        uri.scheme().compare(extensions::wire::AppUriScheme, Qt::CaseInsensitive) != 0)
        return false;
    if (uri.authority() == "workspace") {
        const auto workspace =
            extensions::wire::decodeUri(QUrlQuery(uri).queryItemValue("uri", QUrl::FullyDecoded));
        if (!workspace || !workspace->isLocalFile()) {
            emit error(
                tr("This workspace URI requires a filesystem provider that is not available."));
            return true;
        }
        if (nextDialog_ == std::numeric_limits<quint64>::max())
            return false;
        const auto key = "uri:" + QString::number(++nextDialog_);
        const QPointer<ExternalUriService> self(this);
        modalQueue_->enqueue(*this, key, [self, workspace = *workspace, key] {
            if (!self || !self->window_ || !self->modalQueue_)
                return;
            auto* dialog = new UriConfirmationDialog(
                tr("Open Folder"), tr("Allow this URI to open a folder in LiteCode?"),
                workspace.toLocalFile(), {}, self->window_);
            self->dialog_ = dialog;
            connect(dialog, &QDialog::finished, self, [self, workspace, key](int result) {
                if (!self)
                    return;
                self->dialog_ = nullptr;
                if (result == QDialog::Accepted)
                    emit self->workspaceRequested(workspace);
                if (self && self->modalQueue_)
                    self->modalQueue_->release(*self, key);
            });
            dialog->open();
        });
        return true;
    }
    const auto target = manager_->uriTarget(uri);
    if (!target) {
        emit error(tr("No enabled extension can handle this URI."));
        return false;
    }
    const auto owner = uriIdentity(target->id);
    if (QSettings().value(trustedKey).toStringList().contains(owner))
        return manager_->dispatchUri(uri);
    if (nextDialog_ == std::numeric_limits<quint64>::max()) {
        emit error(tr("URI confirmation service is unavailable."));
        return false;
    }
    const auto key = "uri:" + QString::number(++nextDialog_);
    const QPointer<ExternalUriService> self(this);
    modalQueue_->enqueue(*this, key, [self, target = *target, uri, key, owner] {
        if (!self || !self->window_ || !self->manager_ || !self->modalQueue_)
            return;
        if (!self->manager_->uriTarget(uri)) {
            self->modalQueue_->release(*self, key);
            return;
        }
        auto* dialog = new UriConfirmationDialog(
            tr("Open URI"), tr("Allow '%1' extension to open this URI?").arg(target.name),
            uri.toDisplayString(QUrl::RemoveUserInfo), tr("Do not ask me again for this extension"),
            self->window_);
        self->dialog_ = dialog;
        connect(self->manager_, &extensions::ExtensionManager::changed, dialog,
                [self, dialog, uri] {
                    if (!self || !self->manager_ || !self->manager_->uriTarget(uri))
                        dialog->reject();
                });
        connect(dialog, &QDialog::finished, self, [self, uri, key, owner, dialog](int result) {
            if (!self)
                return;
            self->dialog_ = nullptr;
            if (result == QDialog::Accepted && self->manager_ && self->manager_->uriTarget(uri)) {
                if (dialog->remember()) {
                    auto trusted = QSettings().value(trustedKey).toStringList();
                    if (!trusted.contains(owner))
                        trusted.append(owner);
                    QSettings().setValue(trustedKey, trusted);
                }
                self->manager_->dispatchUri(uri);
            }
            if (self && self->modalQueue_)
                self->modalQueue_->release(*self, key);
        });
        dialog->open();
    });
    return true;
}
} // namespace litecode::ui
