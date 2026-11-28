#include "editor/BoundDocumentReferences.h"
#include "core/FileSystemPath.h"
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace litecode::editor {
BoundDocumentReferences::BoundDocumentReferences(std::function<void(core::DocumentId)> release,
                                                 DocumentReferenceLimits limits, QObject* parent)
    : QObject(parent), release_(std::move(release)), limits_(limits) {
    if (!release_ || limits_.maximumAgeMs <= 0 || limits_.maximumLength < 0 ||
        limits_.maximumCount <= 0)
        throw std::invalid_argument("Invalid document reference limits.");
}
BoundDocumentReferences::~BoundDocumentReferences() { clear(); }
void BoundDocumentReferences::add(core::DocumentId id, QUrl resource, qint64 length) {
    if (!id.isValid() || length < 0 || length > std::numeric_limits<qint64>::max() - length_ ||
        nextToken_ == std::numeric_limits<quint64>::max())
        throw std::invalid_argument("Invalid document reference.");
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    const auto token = ++nextToken_;
    entries_.push_back({token, id, std::move(resource), length, timer});
    length_ += length;
    connect(timer, &QTimer::timeout, this, [this, token] { release(token); });
    timer->start(limits_.maximumAgeMs);
    const QPointer<BoundDocumentReferences> alive(this);
    while (alive && length_ > limits_.maximumLength && !entries_.empty())
        release(entries_.front().token);
    if (!alive)
        return;
    const auto extra = static_cast<qsizetype>(std::ceil(limits_.maximumCount * 1.2));
    if (count() >= extra) {
        const auto removeCount = extra - limits_.maximumCount;
        for (qsizetype index = 0; alive && index < removeCount && !entries_.empty(); ++index)
            release(entries_.front().token);
    }
}
void BoundDocumentReferences::release(quint64 token) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [token](const Entry& entry) { return entry.token == token; });
    if (found == entries_.end())
        return;
    const auto entry = *found;
    length_ -= entry.length;
    entries_.erase(found); // Publish removal before invoking reentrant observers.
    entry.timer->stop();
    entry.timer->deleteLater();
    const auto callback = release_;
    callback(entry.id);
}
void BoundDocumentReferences::remove(core::DocumentId id) {
    QVector<quint64> tokens;
    for (const auto& entry : entries_)
        if (entry.id == id)
            tokens.append(entry.token);
    const QPointer<BoundDocumentReferences> alive(this);
    for (const auto token : tokens) {
        if (!alive)
            return;
        release(token);
    }
}
void BoundDocumentReferences::remove(const QUrl& resource) {
    QVector<quint64> tokens;
    for (const auto& entry : entries_) {
        bool matches = entry.resource == resource;
        if (!matches && entry.resource.isLocalFile() && resource.isLocalFile()) {
            const auto parent = core::normalizedFileSystemPath(resource.toLocalFile());
            const auto child = core::normalizedFileSystemPath(entry.resource.toLocalFile());
            const auto sensitivity = core::fileSystemCaseSensitivity(parent);
            matches = core::pathsReferToSameEntry(child, parent) ||
                      child.startsWith(
                          parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/'),
                          sensitivity);
        } else if (!matches && entry.resource.scheme() == resource.scheme() &&
                   entry.resource.authority() == resource.authority() &&
                   entry.resource.query() == resource.query()) {
            const auto parent = resource.path();
            matches = !parent.isEmpty() &&
                      entry.resource.path().startsWith(
                          parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/'));
        }
        if (matches)
            tokens.append(entry.token);
    }
    const QPointer<BoundDocumentReferences> alive(this);
    for (const auto token : tokens) {
        if (!alive)
            return;
        release(token);
    }
}
void BoundDocumentReferences::clear() {
    const QPointer<BoundDocumentReferences> alive(this);
    while (alive && !entries_.empty())
        release(entries_.front().token);
}
} // namespace litecode::editor
