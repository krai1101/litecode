#pragma once
#include "extensions/DocumentApi.h"
#include "extensions/DocumentWire.h"
#include "extensions/DocumentTextBuffer.h"
#include <QRecursiveMutex>
#include <QStringList>
#include <functional>

namespace litecode::extensions {
class HostTextDocument final : public TextDocument {
  public:
    HostTextDocument(TextDocumentSnapshot snapshot, std::function<QFuture<bool>(const QString&)> save);
    QUrl uri() const override;
    QString fileName() const override;
    bool isUntitled() const override;
    QString languageId() const override;
    QString encoding() const override;
    qint64 version() const override;
    bool isDirty() const override;
    bool isClosed() const override;
    QFuture<bool> save() override;
    EndOfLine eol() const override;
    int lineCount() const override;
    TextLine lineAt(int line) const override;
    TextLine lineAt(const Position& position) const override;
    qint64 offsetAt(const Position& position) const override;
    Position positionAt(qint64 offset) const override;
    QString getText(const std::optional<Range>& range = {}) const override;
    std::optional<Range> getWordRangeAtPosition(const Position& position,
        const std::optional<QRegularExpression>& regex = {}) const override;
    Range validateRange(const Range& range) const override;
    Position validatePosition(const Position& position) const override;
    void accept(TextDocumentSnapshot snapshot);
    void metadata(const TextDocumentMetadata& metadata);
    TextDocumentContentChangeEvent change(qint64 previousVersion, qint64 version, const Range& range, const QString& text, bool dirty);
    void close();
    QVector<TextDocumentContentChangeEvent> changeBatch(const wire::DocumentBatch& batch);
  private:
    void rebuildLines();
    mutable QRecursiveMutex mutex_;
    TextDocumentSnapshot state_;
    DocumentTextBuffer buffer_;
    std::function<QFuture<bool>(const QString&)> save_;
    bool closed_{};
};
} // namespace litecode::extensions
