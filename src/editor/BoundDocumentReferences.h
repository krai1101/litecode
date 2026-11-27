#pragma once
#include "core/DocumentTypes.h"
#include <QObject>
#include <QUrl>
#include <deque>
#include <functional>

class QTimer;
namespace litecode::editor {
struct DocumentReferenceLimits final {
    int maximumAgeMs{3 * 60 * 1000};
    qint64 maximumLength{80 * 1024 * 1024}; // UTF-16 code units, not UTF-8 bytes
    int maximumCount{50};
};
// Native equivalent of MainThreadDocuments' bounded reference collection.
// Each entry represents a reference, not a distinct URI; releasing a reference
// does not itself close a dirty or visible workbench document.
class BoundDocumentReferences final : public QObject {
  public:
    explicit BoundDocumentReferences(std::function<void(core::DocumentId)> release,
        DocumentReferenceLimits limits = {}, QObject* parent = nullptr);
    ~BoundDocumentReferences() override;
    void add(core::DocumentId id, QUrl resource, qint64 length);
    void remove(core::DocumentId id);
    void remove(const QUrl& resource);
    void clear();
    [[nodiscard]] qsizetype count() const { return static_cast<qsizetype>(entries_.size()); }
    [[nodiscard]] qint64 length() const { return length_; }
  private:
    struct Entry { quint64 token; core::DocumentId id; QUrl resource; qint64 length; QTimer* timer; };
    void release(quint64 token);
    std::deque<Entry> entries_;
    std::function<void(core::DocumentId)> release_;
    DocumentReferenceLimits limits_;
    qint64 length_{};
    quint64 nextToken_{};
};
} // namespace litecode::editor
