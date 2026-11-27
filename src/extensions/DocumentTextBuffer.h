#pragma once
#include "extensions/DocumentApi.h"
#include <QStringList>

namespace litecode::extensions {
// Shared private UTF-16 mirror. Single-line edits update a prefix-sum tree;
// complete text is materialized only when a consumer actually requests it.
class DocumentTextBuffer final {
  public:
    DocumentTextBuffer(QString text = {}, EndOfLine eol = EndOfLine::LF);
    [[nodiscard]] int lineCount() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] const QString& line(int index) const;
    [[nodiscard]] Position validate(const Position& position) const;
    [[nodiscard]] qint64 offsetAt(const Position& position) const;
    [[nodiscard]] Position positionAt(qint64 offset) const;
    [[nodiscard]] QString text() const;
    [[nodiscard]] QString text(const Range& range) const;
    [[nodiscard]] TextDocumentContentChangeEvent apply(const Range& range, const QString& text);
    [[nodiscard]] QVector<TextDocumentContentChangeEvent> applyBatch(const QVector<TextEdit>& edits);
  private:
    void rebuild();
    void add(int index, qint64 delta);
    qint64 prefix(int count) const;
    QString separator() const;
    QStringList lines_;
    QVector<qint64> sums_;
    EndOfLine eol_;
    mutable std::optional<QString> cachedText_;
};
} // namespace litecode::extensions
