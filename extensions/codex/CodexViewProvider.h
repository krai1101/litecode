#pragma once
#include "CodexAttachmentStore.h"
#include "CodexViewLayout.h"
#include "codex/ChatSession.h"
#include <QTimer>
#include <extensions/ExtensionApi.h>
namespace litecode::codex {
class CodexTurnActions;
class CodexDiffDocuments;
class CodexViewProvider final : public QObject, public extensions::NativeViewProvider {
  public:
    explicit CodexViewProvider(extensions::ExtensionContext& context);
    ~CodexViewProvider() override;
    extensions::ProviderResult<void> resolveView(std::shared_ptr<extensions::NativeView> view,
                                                 extensions::CancellationToken token) override;

  private:
    void render();
    void refresh();
    void loadHistory(const QString& cursor = {});
    void openThread(const QString& id);
    void receiveThreadLink(const QString& id);
    void openPendingThreadLink();
    void updatePresentation();
    QString presentationSignature() const;
    bool handlePresentationAction(const extensions::NativeViewEvent& event);
    void normalizeModel();
    void input(const extensions::NativeViewEvent& event);
    bool handleComposerAction(const extensions::NativeViewEvent& event);
    bool handleMenuAction(const extensions::NativeViewEvent& event);
    bool handleAuthAction(const extensions::NativeViewEvent& event);
    void openAttachments();
    void submit(bool goal = false);
    void notice(const QString& text);
    void options(const QString& action);
    void finishRename(bool save);
    void interaction(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void questions(const QJsonValue& id, const QJsonArray& questions, int index,
                   QJsonObject answers, quint64 epoch);
    void resetInteractions();
    void showPendingApproval();
    bool handleApprovalAction(const extensions::NativeViewEvent& event);
    struct PendingApproval {
        QJsonValue id;
        QString method, key;
        QJsonObject params;
        CodexApprovalPrompt prompt;
    };
    QVector<PendingApproval> approvals_;
    quint64 approvalSerial_{};
    extensions::ExtensionContext& context_;
    ChatSession session_;
    CodexAttachmentStore attachmentStore_;
    std::shared_ptr<CodexDiffDocuments> diffDocuments_;
    std::unique_ptr<CodexTurnActions> turnActions_;
    std::shared_ptr<extensions::NativeView> view_;
    std::vector<extensions::Disposable> subscriptions_;
    CodexViewState state_;
    QTimer refreshTimer_;
    QTimer historyFilterTimer_;
    QTimer workClock_;
    QTimer copyFeedbackTimer_;
    std::shared_ptr<extensions::CancellationTokenSource> interactionCancellation_;
    quint64 interactionEpoch_{};
    QString editTurn_, editItem_, savedDraft_;
    QString submittedDraft_;
    QStringList submittedAttachments_;
    QString layoutSignature_;
    QString pendingOlderEndId_;
    bool olderRequestInFlight_{};
    bool resolved_{};
    bool historyReturnToConversation_{};
    QString lastOpenThread_;
    QString pendingThreadLink_;
    QString renameThread_;
    QString apiKeyDraft_; // Transient only: never a layout, history or settings value.
    bool followOpenedThread_{true};
};
} // namespace litecode::codex
