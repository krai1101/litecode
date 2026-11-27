#include "ui/extensions/InputBoxService.h"
#include "ui/components/QuickInputDialog.h"
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QTimer>
#include <algorithm>
#include <limits>
namespace litecode::ui {
class InputBoxSession final : public QObject {
  public:
    InputBoxSession(extensions::WorkbenchInputBox& backend, extensions::InputBoxRequest request, QWidget* window, QObject* parent)
        : QObject(parent), backend_(&backend), request_(std::move(request)) {
        dialog_ = new components::QuickInputDialog(request_.options.title.value_or(QString{}),
            request_.options.prompt.value_or(QString{}), request_.options.placeHolder.value_or(QString{}), window);
        // A Qt Popup auto-hides outside the editor even with ignoreFocusOut.
        // Tool windows let the shared input implement that option explicitly.
        dialog_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        dialog_->setWindowModality(Qt::NonModal);
        dialog_->setRawInputMode(true);
        dialog_->setTitleText(request_.options.title.value_or(QString{}));
        dialog_->setHint(request_.options.prompt.value_or(QString{}));
        auto* input = dialog_->input();
        input->setPlaceholderText(request_.options.placeHolder.value_or(QString{}));
        input->setMaxLength(std::numeric_limits<int>::max());
        input->setEchoMode(request_.options.password.value_or(false) ? QLineEdit::Password : QLineEdit::Normal);
        input->setText(request_.options.value.value_or(QString{}));
        if (const auto selection = request_.options.valueSelection) {
            const int length = static_cast<int>(input->text().size());
            const auto end = std::clamp(selection->second, 0, length);
            const auto start = std::min(std::clamp(selection->first, 0, length), end);
            input->setSelection(start, end - start);
        } else input->selectAll();
        debounce_.setInterval(100);
        debounce_.setSingleShot(true);
        connect(input, &QLineEdit::textChanged, this, [this] { debounce_.start(); });
        connect(&debounce_, &QTimer::timeout, this, [this] { if (dialog_) validate(dialog_->value()); });
        dialog_->setAcceptanceHandler([this] { accept(); });
        connect(dialog_, &QDialog::finished, this, [this] {
            if (backend_) backend_->complete(request_.owner, request_.id, {});
        });
        connect(&backend, &extensions::WorkbenchInputBox::validated, this,
            [this](const QString& owner, quint64 id, quint64 sequence, const extensions::InputBoxValidationResult& result, const QString& error) {
                if (owner != request_.owner || id != request_.id || !dialog_) return;
                if (sequence == sequence_) {
                    result_ = result; error_ = error; pending_ = false;
                    display(result, error);
                }
                auto found = accepting_.find(sequence);
                if (found == accepting_.end()) return;
                const auto value = found.value();
                accepting_.erase(found);
                if (error.isEmpty() && accepts(result) && backend_) backend_->complete(request_.owner, request_.id, value);
            });
        dialog_->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
            if (dialog_ && dialog_->isVisible() && now && now != dialog_ && !dialog_->isAncestorOf(now) &&
                !request_.options.ignoreFocusOut.value_or(false)) dialog_->reject();
        });
    }
    ~InputBoxSession() override {
        if (dialog_) { disconnect(dialog_.data(), nullptr, this, nullptr); delete dialog_.data(); }
    }
    const extensions::InputBoxRequest& request() const { return request_; }
    void start() {
        if (!dialog_) return;
        const QPointer<InputBoxSession> self(this);
        const auto dialog = dialog_;
        dialog->show();
        if (!self || !dialog) return;
        dialog->activateWindow();
        if (!self || !dialog) return;
        dialog->input()->setFocus(Qt::PopupFocusReason);
        if (!self || !dialog) return;
        if (request_.hasValidation) validate(dialog_->value());
    }
    void dismiss() {
        debounce_.stop();
        if (dialog_) { disconnect(dialog_.data(), nullptr, this, nullptr); dialog_->hide(); }
        deleteLater();
    }
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == dialog_ && event->type() == QEvent::WindowDeactivate && !request_.options.ignoreFocusOut.value_or(false)) {
            const QPointer<components::QuickInputDialog> dialog(dialog_);
            QTimer::singleShot(0, this, [dialog] { if (dialog && dialog->isVisible()) dialog->reject(); });
        }
        return QObject::eventFilter(watched, event);
    }
  private:
    static bool accepts(const extensions::InputBoxValidationResult& result) {
        if (const auto* text = std::get_if<QString>(&result)) return text->isEmpty();
        if (const auto* message = std::get_if<extensions::InputBoxValidationMessage>(&result))
            return message->severity != extensions::InputBoxValidationSeverity::Error;
        return true;
    }
    void display(const extensions::InputBoxValidationResult& result, const QString& error) {
        if (!dialog_) return;
        if (!error.isEmpty()) { dialog_->setValidationSeverity(error, 3); return; }
        if (const auto* text = std::get_if<QString>(&result)) dialog_->setValidationSeverity(*text, text->isEmpty() ? 0 : 3);
        else if (const auto* message = std::get_if<extensions::InputBoxValidationMessage>(&result))
            dialog_->setValidationSeverity(message->message, static_cast<int>(message->severity));
        else dialog_->setValidationSeverity({}, 0);
    }
    quint64 validate(const QString& value) {
        if (!backend_ || !request_.hasValidation) return 0;
        if (sequence_ && value == validationValue_ && error_.isEmpty()) return sequence_;
        validationValue_ = value; result_.reset(); error_.clear(); pending_ = true;
        sequence_ = backend_->validate(request_.owner, request_.id, value);
        return sequence_;
    }
    void accept() {
        if (!dialog_ || !backend_) return;
        debounce_.stop();
        const auto value = dialog_->value();
        if (!request_.hasValidation) { backend_->complete(request_.owner, request_.id, value); return; }
        const auto sequence = validate(value);
        if (!sequence) return;
        if (!pending_ && result_) {
            if (error_.isEmpty() && accepts(*result_)) backend_->complete(request_.owner, request_.id, value);
            else display(*result_, error_);
        } else accepting_.insert(sequence, value);
    }
    QPointer<extensions::WorkbenchInputBox> backend_;
    extensions::InputBoxRequest request_;
    QPointer<components::QuickInputDialog> dialog_;
    QTimer debounce_;
    QString validationValue_, error_;
    quint64 sequence_{};
    bool pending_{};
    std::optional<extensions::InputBoxValidationResult> result_;
    QHash<quint64, QString> accepting_;
};
InputBoxService::InputBoxService(extensions::WorkbenchInputBox& boxes, QWidget* window, QObject* parent)
    : QObject(parent), boxes_(&boxes), window_(window) {
    connect(&boxes, &extensions::WorkbenchInputBox::requested, this, [this](const extensions::InputBoxRequest& request) {
        const QPointer<InputBoxService> self(this);
        if (active_) { const auto previous = active_->request(); boxes_->complete(previous.owner, previous.id, {}); }
        if (!self || !boxes_ || !boxes_->isPending(request.owner, request.id)) return;
        if (!window_) { boxes_->complete(request.owner, request.id, {}, "The workbench window is unavailable."); return; }
        active_ = new InputBoxSession(*boxes_, request, window_, this);
        active_->start();
    });
    connect(&boxes, &extensions::WorkbenchInputBox::dismissed, this, [this](const QString& owner, quint64 id) {
        if (!active_ || active_->request().owner != owner || active_->request().id != id) return;
        auto* session = active_.data(); active_ = nullptr; session->dismiss();
    });
}
InputBoxService::~InputBoxService() {
    if (boxes_) disconnect(boxes_.data(), nullptr, this, nullptr);
    if (active_ && boxes_) { const auto request = active_->request(); boxes_->complete(request.owner, request.id, {}, "The workbench has stopped."); }
}
} // namespace litecode::ui
