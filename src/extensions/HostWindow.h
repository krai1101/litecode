#pragma once
#include "extensions/HostDialogs.h"
#include "extensions/HostEditors.h"
#include "extensions/HostInputBox.h"
#include "extensions/HostMessages.h"
#include "extensions/HostNativeViews.h"
#include "extensions/HostOutput.h"
#include "extensions/HostProgress.h"
#include "extensions/HostQuickPick.h"
#include "extensions/HostStatusBar.h"
#include "extensions/HostUriHandlers.h"
#include "extensions/HostWindowState.h"
#include "extensions/WindowApi.h"
#include <utility>

namespace litecode::extensions {
// Thin native window facade. Individual capability services retain their own
// transport, ordering, state and shutdown logic rather than growing one host.
class HostWindow final : public Window {
  public:
    HostWindow(QString logs, std::function<void(const QJsonObject&)> send, QString owner = {},
               WindowState initial = {true, true})
        : output_(std::move(logs), send), messages_(send), dialogs_(send), progress_(send),
          inputBox_(send), quickPick_(send), statusBar_(owner, send),
          uriHandlers_(std::move(owner), send), nativeViews_(std::move(send)), state_(initial) {}
    ~HostWindow() override { shutdown(); }
    Disposable registerViewProvider(const QString& id,
                                    std::shared_ptr<NativeViewProvider> provider) override {
        return nativeViews_.registerProvider(id, std::move(provider));
    }
    HostNativeViews& nativeViews() { return nativeViews_; }
    void attachEditors(HostDocuments& documents, HostWorkspaceRequests& requests) {
        if (editors_)
            throw std::logic_error("Editor service is already attached.");
        editors_ = std::make_unique<HostEditors>(documents, requests);
    }
    HostEditors& editors() {
        if (!editors_)
            throw std::logic_error("Editor service is unavailable.");
        return *editors_;
    }
    std::shared_ptr<TextEditor> activeTextEditor() const override {
        return editors_ ? editors_->activeTextEditor() : nullptr;
    }
    QVector<std::shared_ptr<TextEditor>> visibleTextEditors() const override {
        return editors_ ? editors_->visibleTextEditors() : QVector<std::shared_ptr<TextEditor>>{};
    }
    Event<std::shared_ptr<TextEditor>> onDidChangeActiveTextEditor() const override {
        return editors_ ? editors_->onDidChangeActiveTextEditor()
                        : Event<std::shared_ptr<TextEditor>>{};
    }
    Event<QVector<std::shared_ptr<TextEditor>>> onDidChangeVisibleTextEditors() const override {
        return editors_ ? editors_->onDidChangeVisibleTextEditors()
                        : Event<QVector<std::shared_ptr<TextEditor>>>{};
    }
    Event<TextEditorSelectionChangeEvent> onDidChangeTextEditorSelection() const override {
        return editors_ ? editors_->onDidChangeTextEditorSelection()
                        : Event<TextEditorSelectionChangeEvent>{};
    }
    WindowState state() const override { return state_.state(); }
    Event<WindowState> onDidChangeWindowState() const override {
        return state_.onDidChangeWindowState();
    }
    HostWindowState& windowState() { return state_; }
    QFuture<std::optional<QString>> showInputBox(const InputBoxOptions& options = {},
                                                 CancellationToken token = {}) override {
        return inputBox_.show(options, token);
    }
    HostInputBox& inputBox() { return inputBox_; }
    HostQuickPick& quickPick() { return quickPick_; }
    HostUriHandlers& uriHandlers() { return uriHandlers_; }
    Disposable registerUriHandler(std::shared_ptr<UriHandler> handler) override {
        return uriHandlers_.registerUriHandler(std::move(handler));
    }
    QFuture<std::optional<QVector<QUrl>>>
    showOpenDialog(const OpenDialogOptions& options = {}) override {
        return dialogs_.showOpenDialog(options);
    }
    QFuture<std::optional<QUrl>> showSaveDialog(const SaveDialogOptions& options = {}) override {
        return dialogs_.showSaveDialog(options);
    }
    std::shared_ptr<OutputChannel>
    createOutputChannel(const QString& name,
                        const std::optional<QString>& languageId = {}) override {
        return output_.createOutputChannel(name, languageId);
    }
    std::shared_ptr<LogOutputChannel>
    createOutputChannel(const QString& name, LogOutputChannelOptions options) override {
        return output_.createOutputChannel(name, options);
    }
    HostOutput& output() { return output_; }
    HostMessages& messages() { return messages_; }
    HostDialogs& dialogs() { return dialogs_; }
    HostProgress& progress() { return progress_; }
    void shutdown() {
        nativeViews_.shutdown();
        if (editors_)
            editors_->shutdown();
        state_.shutdown();
        uriHandlers_.shutdown();
        statusBar_.shutdown();
        quickPick_.shutdown();
        inputBox_.shutdown();
        progress_.shutdown();
        dialogs_.shutdown();
        messages_.shutdown();
        output_.shutdown();
    }

  private:
    std::shared_ptr<StatusBarItem> createStatusBarEntry(std::optional<QString> id,
                                                        StatusBarAlignment alignment,
                                                        std::optional<double> priority) override {
        return statusBar_.create(std::move(id), alignment, priority);
    }
    Disposable beginStatusBarMessage(const QString& text, std::optional<double> timeout) override {
        return statusBar_.message(text, timeout);
    }
    detail::QuickPickSession beginQuickPick(const QuickPickOptions& options,
                                            CancellationToken token,
                                            std::function<void(int)> focus) override {
        return quickPick_.begin(options, token, std::move(focus));
    }
    detail::ProgressSession beginProgress(const ProgressOptions& options) override {
        return progress_.begin(options);
    }
    QFuture<std::optional<int>> requestMessage(int severity, const QString& message,
                                               const MessageOptions& options,
                                               const QVector<MessageItem>& items) override {
        return messages_.show(severity, message, options, items);
    }
    HostOutput output_;
    HostMessages messages_;
    HostDialogs dialogs_;
    HostProgress progress_;
    HostInputBox inputBox_;
    HostQuickPick quickPick_;
    HostStatusBar statusBar_;
    HostUriHandlers uriHandlers_;
    HostNativeViews nativeViews_;
    HostWindowState state_;
    std::unique_ptr<HostEditors> editors_;
};
} // namespace litecode::extensions
