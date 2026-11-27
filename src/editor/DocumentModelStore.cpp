#include "editor/DocumentModelStore.h"
#include "editor/LanguageSupport.h"
#include "core/FileSystemPath.h"
#include <QFileInfo>
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace litecode::editor {
core::DocumentId DocumentModelStore::nextId() {
    const auto id = ids_.next();
    if (!id.isValid()) throw std::runtime_error("Document identifier space exhausted.");
    return id;
}
std::shared_ptr<TextDocumentModel> DocumentModelStore::createFile(const QString& path) {
    if (path.isEmpty()) throw std::invalid_argument("A file model requires a path.");
    const QFileInfo info(path);
    const auto resolved = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    const auto uri = QUrl::fromLocalFile(resolved);
    if (auto existing = find(uri)) return existing;
    auto model = std::make_shared<TextDocumentModel>(nextId(), resolved, languageSupportForFile(resolved).id);
    models_.insert(model->session().id().value(), model);
    return model;
}
std::shared_ptr<TextDocumentModel> DocumentModelStore::createUntitled(const QString& content,
    const QString& language, core::TextEncoding encoding, core::LineEnding lineEnding) {
    auto model = std::make_shared<TextDocumentModel>(nextId(), QString(), language);
    if (!model->initializeUntitled(content, encoding, lineEnding)) throw std::runtime_error("Could not initialize untitled document.");
    models_.insert(model->session().id().value(), model);
    return model;
}
std::shared_ptr<TextDocumentModel> DocumentModelStore::createVirtual(const QUrl& uri, const std::optional<QString>& content) {
    if (auto existing = find(uri)) return existing;
    auto model = std::make_shared<TextDocumentModel>(nextId(), QString(), languageSupportForFile(uri.path()).id);
    model->bindVirtualResource(uri);
    if (content && !model->initializeVirtual(uri, *content)) throw std::runtime_error("Could not initialize virtual document.");
    models_.insert(model->session().id().value(), model);
    return model;
}
std::shared_ptr<TextDocumentModel> DocumentModelStore::find(core::DocumentId id) const {
    const auto model = models_.value(id.value());
    return model && !model->session().isClosed() ? model : nullptr;
}
std::shared_ptr<TextDocumentModel> DocumentModelStore::find(const QUrl& uri) const {
    for (const auto& model : models_) {
        if (model->session().isClosed()) continue;
        if (model->resource() == uri || (uri.isLocalFile() && !model->session().filePath().isEmpty() &&
            core::pathsReferToSameEntry(model->session().filePath(), uri.toLocalFile()))) return model;
    }
    return {};
}
QVector<std::shared_ptr<TextDocumentModel>> DocumentModelStore::models() const {
    QVector<std::shared_ptr<TextDocumentModel>> result;
    result.reserve(models_.size());
    for (const auto& model : models_) if (!model->session().isClosed()) result.append(model);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left->session().id().value() < right->session().id().value();
    });
    return result;
}
bool DocumentModelStore::remove(core::DocumentId id) {
    const auto model = models_.take(id.value());
    references_.remove(id.value());
    if (!model) return false;
    model->session().close();
    return true;
}
void DocumentModelStore::retain(core::DocumentId id) {
    if (!find(id)) throw std::invalid_argument("Cannot retain a closed document.");
    if (references(id) == std::numeric_limits<int>::max()) throw std::overflow_error("Too many document references.");
    ++references_[id.value()];
}
void DocumentModelStore::release(core::DocumentId id) {
    const auto count = references(id);
    if (count <= 1) references_.remove(id.value());
    else references_[id.value()] = count - 1;
}
} // namespace litecode::editor
