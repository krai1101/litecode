#pragma once
#include <QHash>
#include <QJsonObject>
#include <QUrl>
#include <functional>
#include <optional>

namespace litecode::extensions::wire {
struct TextContentTransfer final {
    quint64 transferId{}, handle{}, requestId{};
    QUrl uri;
    std::optional<QString> content;
    QString error;
    bool change{};
};
void sendTextContent(const TextContentTransfer& transfer, const std::function<void(const QJsonObject&)>& send);
class TextContentDecoder final {
  public:
    std::optional<TextContentTransfer> accept(const QJsonObject& message, QString* error);
    void reset() { pending_.clear(); reserved_ = 0; }
    static constexpr qint64 maximumLength() { return 50 * 1024 * 1024; }
  private:
    struct Pending { TextContentTransfer value; qint64 length{}; quint64 sequence{}; };
    QHash<quint64, Pending> pending_;
    qint64 reserved_{};
};
} // namespace litecode::extensions::wire
