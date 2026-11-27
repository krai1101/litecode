#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <optional>

namespace litecode::extensions {

// Private workbench-side state for a chat participant request. A stream is
// identified by the workbench request ID, not by an extension-supplied ID.
// This class deliberately exposes no plugin API until the complete stable
// chat participant contract can be represented by the native SDK.
class ChatStreamLedger final {
  public:
    struct Part {
        quint64 sequence{};
        QJsonObject payload;
    };

    bool start(quint64 requestId);
    std::optional<Part> append(quint64 requestId, const QJsonObject& payload);
    bool finish(quint64 requestId);
    bool cancel(quint64 requestId);
    bool contains(quint64 requestId) const;
    bool isCancelled(quint64 requestId) const;
    void clear();
    [[nodiscard]] static bool isValidPart(const QJsonObject& payload);

  private:
    struct State {
        quint64 nextSequence{};
        bool cancelled{};
    };
    QHash<quint64, State> requests_;
};

} // namespace litecode::extensions
