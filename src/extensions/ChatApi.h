#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/CommandApi.h"
#include "extensions/MarkdownApi.h"
#include "extensions/ThemeApi.h"
#include "extensions/TextApi.h"
#include "extensions/ProviderApi.h"
#include <QFuture>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace litecode::extensions {
// Experimental native equivalents of the stable vscode.chat request path.
// The complete chat contract (models, tools, references, followups and feedback)
// is tracked in docs/extension-api-contract.md; these types are not parity yet.
using ChatPromptReferenceValue = std::variant<QString, QUrl, Location, QJsonValue>;
struct ChatPromptReference final {
    QString id;
    std::optional<std::pair<int, int>> range;
    std::optional<QString> modelDescription;
    ChatPromptReferenceValue value;
};
struct ChatLanguageModelToolReference final {
    QString name;
    std::optional<std::pair<int, int>> range;
};
struct ChatRequest final {
    QString prompt;
    std::optional<QString> command;
    QVector<ChatPromptReference> references;
    QVector<ChatLanguageModelToolReference> toolReferences;
};
struct ChatErrorDetails final {
    QString message;
    bool responseIsFiltered{};
};
struct ChatResult final {
    std::optional<ChatErrorDetails> errorDetails;
    QJsonObject metadata;
};
enum class ChatResultFeedbackKind { Unhelpful = 0, Helpful = 1 };
struct ChatResultFeedback final {
    ChatResult result;
    ChatResultFeedbackKind kind{ChatResultFeedbackKind::Unhelpful};
};
struct ChatFollowup final {
    QString prompt;
    std::optional<QString> label;
    std::optional<QString> participant;
    std::optional<QString> command;
};
// Native value equivalents of the stable ChatResponsePart union. The JSON wire
// representation is private to the host and is not part of this extension API.
using ChatResource = std::variant<QUrl, Location>;
struct ChatResponseFileTree final {
    QString name;
    std::optional<QVector<ChatResponseFileTree>> children;
};
struct ChatResponseMarkdownPart final {
    MarkdownString value;
    ChatResponseMarkdownPart(MarkdownString markdown) : value(std::move(markdown)) {
        if (const auto trusted = std::get_if<bool>(&value.isTrusted); trusted && *trusted)
            throw std::invalid_argument("The boolean true form of MarkdownString.isTrusted is not supported for chat participants.");
    }
    ChatResponseMarkdownPart(QString markdown) : value(std::move(markdown)) {}
};
struct ChatResponseFileTreePart final {
    QVector<ChatResponseFileTree> value;
    QUrl baseUri;
};
struct ChatResponseAnchorPart final {
    ChatResource value;
    std::optional<QString> title;
};
struct ChatResponseProgressPart final { QString value; };
struct ChatResponseReferencePart final {
    ChatResource value;
    std::optional<IconPath> iconPath;
};
struct ChatResponseCommandButtonPart final { Command value; };
using ChatResponsePart = std::variant<
    ChatResponseMarkdownPart, ChatResponseFileTreePart, ChatResponseAnchorPart,
    ChatResponseProgressPart, ChatResponseReferencePart, ChatResponseCommandButtonPart>;
using ChatResponseContentPart = std::variant<
    ChatResponseMarkdownPart, ChatResponseFileTreePart, ChatResponseAnchorPart,
    ChatResponseCommandButtonPart>;
struct ChatRequestTurn final {
    QString prompt;
    QString participant;
    std::optional<QString> command;
    QVector<ChatPromptReference> references;
    QVector<ChatLanguageModelToolReference> toolReferences;
};
struct ChatResponseTurn final {
    QVector<ChatResponseContentPart> response;
    ChatResult result;
    QString participant;
    std::optional<QString> command;
};
using ChatTurn = std::variant<ChatRequestTurn, ChatResponseTurn>;
struct ChatContext final { QVector<ChatTurn> history; };
class ChatFollowupProvider {
  public:
    virtual ~ChatFollowupProvider() = default;
    virtual ProviderResult<QVector<ChatFollowup>> provideFollowups(
        ChatResult result, ChatContext context, CancellationToken token) = 0;
};
class ChatResponseStream {
  public:
    virtual ~ChatResponseStream() = default;
    virtual void push(const ChatResponsePart& part) = 0;
    void markdown(const QString& value) { push(ChatResponseMarkdownPart(value)); }
    void markdown(const MarkdownString& value) { push(ChatResponseMarkdownPart(value)); }
    void anchor(const ChatResource& value, std::optional<QString> title = {}) {
        push(ChatResponseAnchorPart{value, std::move(title)});
    }
    void button(const Command& command) { push(ChatResponseCommandButtonPart{command}); }
    void filetree(const QVector<ChatResponseFileTree>& value, const QUrl& baseUri) {
        push(ChatResponseFileTreePart{value, baseUri});
    }
    void progress(const QString& value) { push(ChatResponseProgressPart{value}); }
    void reference(const ChatResource& value, std::optional<IconPath> iconPath = {}) {
        push(ChatResponseReferencePart{value, std::move(iconPath)});
    }
};
// Native representation of ProviderResult<ChatResult | void>, including an
// absent result delivered by an asynchronous provider.
using ChatHandlerResult = std::variant<std::monostate, ChatResult, std::optional<ChatResult>,
                                      QFuture<ChatResult>, QFuture<std::optional<ChatResult>>, QFuture<void>>;
using ChatRequestHandler = std::function<ChatHandlerResult(
    ChatRequest, ChatContext, std::shared_ptr<ChatResponseStream>,
    CancellationToken)>;
class ChatParticipant {
  public:
    virtual ~ChatParticipant() = default;
    virtual QString id() const = 0;
    [[nodiscard]] virtual ChatRequestHandler requestHandler() const = 0;
    virtual void requestHandler(ChatRequestHandler handler) = 0;
    virtual void iconPath(std::optional<IconPath> iconPath) = 0;
    [[nodiscard]] virtual std::optional<IconPath> iconPath() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ChatFollowupProvider> followupProvider() const = 0;
    virtual void followupProvider(std::shared_ptr<ChatFollowupProvider> provider) = 0;
    [[nodiscard]] virtual Event<ChatResultFeedback> onDidReceiveFeedback() const = 0;
    virtual void dispose() = 0;
};
class Chat {
  public:
    virtual ~Chat() = default;
    virtual std::shared_ptr<ChatParticipant> createChatParticipant(
        const QString& id, ChatRequestHandler handler) = 0;
};
} // namespace litecode::extensions
