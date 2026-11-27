#pragma once
#include "extensions/DocumentApi.h"
#include <QJsonObject>
#include <functional>

namespace litecode::extensions {
struct TextDocumentMetadata final {
    QString id, languageId, encoding;
    bool dirty{};
};
struct TextDocumentSnapshot final {
    QString id;
    QUrl uri;
    QString languageId, encoding, text;
    qint64 version{1};
    bool dirty{};
    EndOfLine eol{EndOfLine::LF};
    std::optional<TextDocumentChangeReason> reason;
};
namespace wire {
QJsonObject encodeDocumentMetadata(const TextDocumentMetadata& metadata);
std::optional<TextDocumentMetadata> decodeDocumentMetadata(const QJsonObject& message);
struct DocumentChange final {
    QString id;
    qint64 previousVersion{}, version{};
    Range range;
    QString text;
    bool dirty{};
    std::optional<TextDocumentChangeReason> reason;
};
QJsonObject encodeDocumentChange(const DocumentChange& change);
std::optional<DocumentChange> decodeDocumentChange(const QJsonObject& message);
void sendDocumentChange(const DocumentChange& change, const std::function<void(const QJsonObject&)>& send);
class DocumentChangeDecoder final {
  public:
    std::optional<DocumentChange> accept(const QJsonObject& message, QString* error);
    [[nodiscard]] bool transferring() const { return pending_.has_value(); }
  private:
    std::optional<DocumentChange> pending_;
    qint64 expected_{};
    quint64 sequence_{};
};
struct DocumentBatch final {
    QString id;
    qint64 previousVersion{}, version{};
    QVector<TextEdit> edits;
    bool dirty{};
    std::optional<TextDocumentChangeReason> reason;
};
void sendDocumentBatch(const DocumentBatch& batch, const std::function<void(const QJsonObject&)>& send);
class DocumentBatchDecoder final {
  public:
    std::optional<DocumentBatch> accept(const QJsonObject& message, QString* error);
    [[nodiscard]] bool transferring() const { return pending_.has_value(); }
  private:
    std::optional<DocumentBatch> pending_;
    DocumentChangeDecoder changes_;
    qint64 expected_{}, length_{};
};
void sendDocumentSnapshot(const TextDocumentSnapshot& snapshot,
    const std::function<void(const QJsonObject&)>& send);
class DocumentSnapshotDecoder final {
  public:
    std::optional<TextDocumentSnapshot> accept(const QJsonObject& message, QString* error);
    [[nodiscard]] bool transferring() const { return pending_.has_value(); }
  private:
    std::optional<TextDocumentSnapshot> pending_;
    qint64 expected_{};
    quint64 sequence_{};
};
} // namespace wire
} // namespace litecode::extensions
