#pragma once
#include "extensions/DialogApi.h"
#include "extensions/EditorApi.h"
#include "extensions/InputBoxApi.h"
#include "extensions/MessageApi.h"
#include "extensions/NativeViewApi.h"
#include "extensions/OutputApi.h"
#include "extensions/ProgressApi.h"
#include "extensions/QuickPickApi.h"
#include "extensions/StatusBarApi.h"
#include "extensions/UriApi.h"
#include "extensions/WindowStateApi.h"
#include <QFuture>
#include <QPromise>
#include <QStringList>
#include <QVector>
#include <concepts>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace litecode::extensions {
class Window {
  public:
    virtual ~Window() = default;
    virtual Disposable registerViewProvider(const QString& viewId,
                                            std::shared_ptr<NativeViewProvider> provider) = 0;
    [[nodiscard]] virtual std::shared_ptr<TextEditor> activeTextEditor() const = 0;
    [[nodiscard]] virtual QVector<std::shared_ptr<TextEditor>> visibleTextEditors() const = 0;
    [[nodiscard]] virtual Event<std::shared_ptr<TextEditor>>
    onDidChangeActiveTextEditor() const = 0;
    [[nodiscard]] virtual Event<QVector<std::shared_ptr<TextEditor>>>
    onDidChangeVisibleTextEditors() const = 0;
    [[nodiscard]] virtual Event<TextEditorSelectionChangeEvent>
    onDidChangeTextEditorSelection() const = 0;
    [[nodiscard]] virtual WindowState state() const = 0;
    [[nodiscard]] virtual Event<WindowState> onDidChangeWindowState() const = 0;
    virtual Disposable registerUriHandler(std::shared_ptr<UriHandler> handler) = 0;
    std::shared_ptr<StatusBarItem>
    createStatusBarItem(StatusBarAlignment alignment = StatusBarAlignment::Left,
                        std::optional<double> priority = {}) {
        return createStatusBarEntry({}, alignment, priority);
    }
    std::shared_ptr<StatusBarItem>
    createStatusBarItem(const QString& id, StatusBarAlignment alignment = StatusBarAlignment::Left,
                        std::optional<double> priority = {}) {
        return createStatusBarEntry(id, alignment, priority);
    }
    Disposable setStatusBarMessage(const QString& text) { return beginStatusBarMessage(text, {}); }
    Disposable setStatusBarMessage(const QString& text, double hideAfterTimeout) {
        return beginStatusBarMessage(text, hideAfterTimeout);
    }
    template <typename T>
    Disposable setStatusBarMessage(const QString& text, QFuture<T> hideWhenDone) {
        auto disposable = beginStatusBarMessage(text, {});
        // Accept the future rather than its result so both void and valued
        // futures can signal completion without consuming their payload.
        hideWhenDone.then([disposable](QFuture<T>) mutable { disposable.dispose(); })
            .onFailed([disposable]() mutable { disposable.dispose(); })
            .onCanceled([disposable]() mutable { disposable.dispose(); });
        return disposable;
    }
    virtual QFuture<std::optional<QString>> showInputBox(const InputBoxOptions& options = {},
                                                         CancellationToken token = {}) = 0;
    template <typename T>
        requires(std::same_as<T, QString> || std::derived_from<T, QuickPickItem>)
    QFuture<std::optional<QuickPickSelection<T>>>
    showQuickPick(QVector<T> items, const QuickPickOptions& options = {},
                  CancellationToken token = {}) {
        QPromise<QVector<T>> ready;
        ready.start();
        const auto future = ready.future();
        ready.addResult(std::move(items));
        ready.finish();
        return showQuickPick(future, options, token);
    }
    template <typename T>
        requires(std::same_as<T, QString> || std::derived_from<T, QuickPickItem>)
    QFuture<std::optional<QuickPickSelection<T>>>
    showQuickPick(QFuture<QVector<T>> source, const QuickPickOptions& options = {},
                  CancellationToken token = {}) {
        struct State {
            std::mutex mutex;
            std::optional<QVector<T>> items;
        };
        auto state = std::make_shared<State>();
        auto session =
            beginQuickPick(options, token, [state, callback = options.onDidSelectItem](int index) {
                if (!callback)
                    return;
                std::optional<T> item;
                {
                    std::lock_guard lock(state->mutex);
                    if (state->items && index >= 0 && index < state->items->size())
                        item = state->items->at(index);
                }
                if (item) {
                    if constexpr (std::same_as<T, QString>)
                        callback(*item);
                    else
                        callback(static_cast<const QuickPickItem&>(*item));
                }
            });
        const auto fail = session.fail;
        source
            .then([state, setter = session.setItems](QVector<T> items) {
                QVector<QuickPickItem> presentation;
                presentation.reserve(items.size());
                for (const auto& item : items) {
                    if constexpr (std::same_as<T, QString>) {
                        QuickPickItem value;
                        value.label = item;
                        presentation.append(value);
                    } else
                        presentation.append(static_cast<const QuickPickItem&>(item));
                }
                {
                    std::lock_guard lock(state->mutex);
                    state->items = std::move(items);
                }
                setter(std::move(presentation));
            })
            .onFailed([fail](const std::exception& error) {
                if (dynamic_cast<const CancellationError*>(&error))
                    fail(std::make_exception_ptr(CancellationError()));
                else
                    fail(std::make_exception_ptr(std::runtime_error(error.what())));
            })
            .onFailed([fail] {
                fail(std::make_exception_ptr(std::runtime_error("Quick pick item source failed.")));
            })
            .onCanceled([fail] { fail(std::make_exception_ptr(CancellationError())); });
        return session.result.then(
            [state, many = options.canPickMany.value_or(false)](
                std::optional<QVector<int>> indices) -> std::optional<QuickPickSelection<T>> {
                if (!indices)
                    return {};
                std::lock_guard lock(state->mutex);
                if (!state->items || (!many && indices->size() != 1))
                    throw std::runtime_error("Invalid quick pick selection.");
                QVector<T> selected;
                selected.reserve(indices->size());
                for (const auto index : *indices) {
                    if (index < 0 || index >= state->items->size())
                        throw std::runtime_error("Invalid quick pick item index.");
                    selected.append(state->items->at(index));
                }
                if (many)
                    return QuickPickSelection<T>(std::move(selected));
                return QuickPickSelection<T>(selected.first());
            });
    }
    template <typename Task>
    auto withProgress(const ProgressOptions& options, Task&& task)
        -> std::invoke_result_t<Task&, std::shared_ptr<Progress<ProgressReport>>,
                                CancellationToken> {
        using Future = std::invoke_result_t<Task&, std::shared_ptr<Progress<ProgressReport>>,
                                            CancellationToken>;
        using Result = typename detail::ProgressFutureValue<Future>::Type;
        detail::ProgressSession session;
        try {
            session = beginProgress(options);
            const auto finished = std::make_shared<std::atomic<bool>>(false);
            const auto finish = [finished, end = session.finish] {
                if (!finished->exchange(true) && end) {
                    try {
                        end();
                    } catch (...) {
                    }
                }
            };
            Future future;
            try {
                future = std::invoke(task, session.progress, session.token);
            } catch (...) {
                finish();
                throw;
            }
            return future
                .then([finish](Future completed) -> Result {
                    finish();
                    if constexpr (std::is_void_v<Result>)
                        completed.waitForFinished();
                    else
                        return completed.result();
                })
                .onFailed([finish, future]() mutable -> Result {
                    finish();
                    if constexpr (std::is_void_v<Result>)
                        future.waitForFinished();
                    else
                        return future.result();
                })
                .onCanceled([finish]() -> Result {
                    finish();
                    throw CancellationError();
                });
        } catch (...) {
            if (session.finish) {
                try {
                    session.finish();
                } catch (...) {
                }
            }
            QPromise<Result> rejected;
            rejected.start();
            auto future = rejected.future();
            rejected.setException(std::current_exception());
            rejected.finish();
            return future;
        }
    }
    virtual QFuture<std::optional<QVector<QUrl>>>
    showOpenDialog(const OpenDialogOptions& options = {}) = 0;
    virtual QFuture<std::optional<QUrl>> showSaveDialog(const SaveDialogOptions& options = {}) = 0;
    virtual std::shared_ptr<OutputChannel>
    createOutputChannel(const QString& name, const std::optional<QString>& languageId = {}) = 0;
    virtual std::shared_ptr<LogOutputChannel>
    createOutputChannel(const QString& name, LogOutputChannelOptions options) = 0;

    QFuture<std::optional<QString>> showInformationMessage(const QString& message,
                                                           const QStringList& items = {}) {
        return showInformationMessage(message, MessageOptions{}, items);
    }
    QFuture<std::optional<QString>> showInformationMessage(const QString& message,
                                                           const MessageOptions& options,
                                                           const QStringList& items = {}) {
        return strings(1, message, options, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showInformationMessage(const QString& message,
                                                     const QVector<T>& items) {
        return showInformationMessage(message, MessageOptions{}, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showInformationMessage(const QString& message,
                                                     const MessageOptions& options,
                                                     const QVector<T>& items) {
        return objects(1, message, options, items);
    }
    QFuture<std::optional<QString>> showWarningMessage(const QString& message,
                                                       const QStringList& items = {}) {
        return showWarningMessage(message, MessageOptions{}, items);
    }
    QFuture<std::optional<QString>> showWarningMessage(const QString& message,
                                                       const MessageOptions& options,
                                                       const QStringList& items = {}) {
        return strings(2, message, options, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showWarningMessage(const QString& message, const QVector<T>& items) {
        return showWarningMessage(message, MessageOptions{}, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showWarningMessage(const QString& message,
                                                 const MessageOptions& options,
                                                 const QVector<T>& items) {
        return objects(2, message, options, items);
    }
    QFuture<std::optional<QString>> showErrorMessage(const QString& message,
                                                     const QStringList& items = {}) {
        return showErrorMessage(message, MessageOptions{}, items);
    }
    QFuture<std::optional<QString>> showErrorMessage(const QString& message,
                                                     const MessageOptions& options,
                                                     const QStringList& items = {}) {
        return strings(3, message, options, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showErrorMessage(const QString& message, const QVector<T>& items) {
        return showErrorMessage(message, MessageOptions{}, items);
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> showErrorMessage(const QString& message,
                                               const MessageOptions& options,
                                               const QVector<T>& items) {
        return objects(3, message, options, items);
    }

  protected:
    virtual std::shared_ptr<StatusBarItem> createStatusBarEntry(std::optional<QString> id,
                                                                StatusBarAlignment alignment,
                                                                std::optional<double> priority) = 0;
    virtual Disposable beginStatusBarMessage(const QString& text,
                                             std::optional<double> timeout) = 0;
    virtual detail::QuickPickSession beginQuickPick(const QuickPickOptions& options,
                                                    CancellationToken token,
                                                    std::function<void(int)> onDidSelectItem) = 0;
    virtual detail::ProgressSession beginProgress(const ProgressOptions& options) = 0;
    // Native implementation hook, not a new extension-facing operation. Only
    // an item index crosses IPC; caller-defined item fields stay in the host.
    virtual QFuture<std::optional<int>> requestMessage(int severity, const QString& message,
                                                       const MessageOptions& options,
                                                       const QVector<MessageItem>& items) = 0;

  private:
    QFuture<std::optional<QString>> strings(int severity, const QString& message,
                                            const MessageOptions& options,
                                            const QStringList& items) {
        QVector<MessageItem> actions;
        actions.reserve(items.size());
        for (const auto& title : items)
            actions.append({title, {}});
        return requestMessage(severity, message, options, actions)
            .then([items](std::optional<int> index) -> std::optional<QString> {
                if (!index)
                    return {};
                if (*index < 0 || *index >= items.size())
                    throw std::runtime_error("Invalid message action index.");
                return items[*index];
            });
    }
    template <std::derived_from<MessageItem> T>
    QFuture<std::optional<T>> objects(int severity, const QString& message,
                                      const MessageOptions& options, const QVector<T>& items) {
        QVector<MessageItem> actions;
        actions.reserve(items.size());
        for (const auto& item : items)
            actions.append({item.title, item.isCloseAffordance});
        return requestMessage(severity, message, options, actions)
            .then([items](std::optional<int> index) -> std::optional<T> {
                if (!index)
                    return {};
                if (*index < 0 || *index >= items.size())
                    throw std::runtime_error("Invalid message action index.");
                return items[*index];
            });
    }
};
} // namespace litecode::extensions
