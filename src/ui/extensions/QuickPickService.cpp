#include "ui/extensions/QuickPickService.h"
#include "ui/QuickOpenDialog.h"
#include "ui/FileIconTheme.h"
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QTimer>

namespace litecode::ui {
namespace {
QuickPickItem presentation(const extensions::QuickPickItem& source, int index, bool dark) {
    QuickPickItem item;
    item.id = QString::number(index);
    item.label = source.label;
    item.separator = source.kind.value_or(extensions::QuickPickItemKind::Default) == extensions::QuickPickItemKind::Separator;
    if (item.separator) return item;
    item.description = source.description.value_or(QString{});
    item.detail = source.detail.value_or(QString{});
    item.picked = source.picked.value_or(false);
    item.alwaysShow = source.alwaysShow.value_or(false);
    if (source.resourceUri) {
        const auto path = source.resourceUri->isLocalFile() ? source.resourceUri->toLocalFile() : source.resourceUri->path();
        if (item.label.isEmpty()) item.label = QFileInfo(path).fileName();
        if (item.description.isEmpty()) item.description = path;
    }
    if (source.iconPath) {
        std::visit([&](const auto& icon) {
            using T = std::decay_t<decltype(icon)>;
            if constexpr (std::same_as<T, extensions::ThemeIcon>) {
                if (icon.id() == "file" || icon.id() == "folder") {
                    const auto path = source.resourceUri ? source.resourceUri->path() : item.label;
                    item.icon = fileIconForPath(path, icon.id() == "folder");
                } else item.label.prepend("$(" + icon.id() + ") ");
            } else {
                const QUrl path = [&] {
                    if constexpr (std::same_as<T, QUrl>) return icon;
                    else return dark ? icon.dark : icon.light;
                }();
                if (path.isLocalFile()) item.icon = QIcon(path.toLocalFile());
            }
        }, *source.iconPath);
    }
    return item;
}
}
class QuickPickSession final : public QObject {
  public:
    QuickPickSession(extensions::WorkbenchQuickPick& backend, extensions::QuickPickRequest request, QWidget* window, QObject* parent)
        : QObject(parent), backend_(&backend), request_(std::move(request)) {
        dialog_ = new QuickOpenDialog(request_.options.title.value_or(QString{}),
            request_.options.placeHolder.value_or(QString{}), QVector<QuickPickItem>{}, {}, window);
        dialog_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        dialog_->setWindowModality(Qt::NonModal);
        dialog_->configureExtensionPicker(request_.options.canPickMany.value_or(false),
            request_.options.matchOnDescription.value_or(false), request_.options.matchOnDetail.value_or(false),
            request_.options.prompt.value_or(QString{}));
        dialog_->setBusy(true);
        connect(dialog_, &QDialog::finished, this, [this](int result) {
            if (!backend_ || !dialog_) return;
            std::optional<QVector<int>> selection;
            if (result == QDialog::Accepted) {
                selection.emplace();
                for (const auto& id : dialog_->selectedItemIds()) {
                    bool valid = false; const int index = id.toInt(&valid);
                    if (!valid) { backend_->complete(request_.owner, request_.id, {}, "Invalid picker selection."); return; }
                    selection->append(index);
                }
            }
            backend_->complete(request_.owner, request_.id, selection);
        });
        connect(dialog_, &QuickOpenDialog::activeItemChanged, this, [this](const QString& id) {
            bool valid = false; const int index = id.toInt(&valid);
            if (valid && backend_) backend_->focus(request_.owner, request_.id, index);
        }, Qt::QueuedConnection);
        dialog_->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
            if (dialog_ && dialog_->isVisible() && now && now != dialog_ && !dialog_->isAncestorOf(now) &&
                !request_.options.ignoreFocusOut.value_or(false)) dialog_->reject();
        });
    }
    ~QuickPickSession() override {
        if (dialog_) { disconnect(dialog_.data(), nullptr, this, nullptr); delete dialog_.data(); }
    }
    const extensions::QuickPickRequest& request() const { return request_; }
    void start() {
        const QPointer<QuickPickSession> self(this); const auto dialog = dialog_;
        if (!dialog) return;
        dialog->show();
        if (self && dialog) dialog->activateWindow();
    }
    void setItems(const QVector<extensions::QuickPickItem>& items) {
        if (!dialog_) return;
        QVector<QuickPickItem> rows; rows.reserve(items.size());
        const bool dark = qApp->property("litecodeDarkTheme").toBool();
        for (qsizetype index = 0; index < items.size(); ++index) rows.append(presentation(items.at(index), static_cast<int>(index), dark));
        dialog_->setPickerItems(std::move(rows));
    }
    void dismiss() {
        if (dialog_) { disconnect(dialog_.data(), nullptr, this, nullptr); dialog_->hide(); }
        deleteLater();
    }
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == dialog_ && event->type() == QEvent::WindowDeactivate && !request_.options.ignoreFocusOut.value_or(false)) {
            const auto dialog = dialog_;
            QTimer::singleShot(0, this, [dialog] { if (dialog && dialog->isVisible()) dialog->reject(); });
        }
        return QObject::eventFilter(watched, event);
    }
  private:
    QPointer<extensions::WorkbenchQuickPick> backend_;
    extensions::QuickPickRequest request_;
    QPointer<QuickOpenDialog> dialog_;
};
QuickPickService::QuickPickService(extensions::WorkbenchQuickPick& backend, QWidget* window, QObject* parent)
    : QObject(parent), backend_(&backend), window_(window) {
    connect(&backend, &extensions::WorkbenchQuickPick::requested, this, [this](const extensions::QuickPickRequest& request) {
        const QPointer<QuickPickService> self(this);
        if (active_) { const auto previous = active_->request(); backend_->complete(previous.owner, previous.id, {}); }
        if (!self || !backend_ || !backend_->isPending(request.owner, request.id)) return;
        if (!window_) { backend_->complete(request.owner, request.id, {}, "The workbench window is unavailable."); return; }
        active_ = new QuickPickSession(*backend_, request, window_, this);
        active_->start();
    });
    connect(&backend, &extensions::WorkbenchQuickPick::itemsChanged, this,
        [this](const QString& owner, quint64 id, const QVector<extensions::QuickPickItem>& items) {
            if (active_ && active_->request().owner == owner && active_->request().id == id) active_->setItems(items);
        });
    connect(&backend, &extensions::WorkbenchQuickPick::dismissed, this, [this](const QString& owner, quint64 id) {
        if (!active_ || active_->request().owner != owner || active_->request().id != id) return;
        auto* session = active_.data(); active_ = nullptr; session->dismiss();
    });
}
QuickPickService::~QuickPickService() {
    if (backend_) disconnect(backend_.data(), nullptr, this, nullptr);
    if (active_ && backend_) { const auto request = active_->request(); backend_->complete(request.owner, request.id, {}, "The workbench has stopped."); }
}
}
