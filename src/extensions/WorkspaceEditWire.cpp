#include "extensions/WorkspaceEditWire.h"
#include "extensions/DocumentWire.h"
#include "extensions/IconWire.h"
#include <QJsonArray>
#include <type_traits>
namespace litecode::extensions::wire {
namespace {
QJsonObject encodeMetadata(const WorkspaceEditEntryMetadata& value) {
    QJsonObject result{{"needsConfirmation", value.needsConfirmation}, {"label", value.label}};
    if (value.description) result.insert("description", *value.description);
    if (value.iconPath) result.insert("iconPath", encodeIconPath(*value.iconPath));
    return result;
}
std::optional<WorkspaceEditEntryMetadata> decodeMetadata(const QJsonValue& value) {
    if (!value.isObject()) return {};
    const auto object = value.toObject();
    if (!object.value("needsConfirmation").isBool() || !object.value("label").isString() ||
        (object.contains("description") && !object.value("description").isString())) return {};
    WorkspaceEditEntryMetadata result{object.value("needsConfirmation").toBool(), object.value("label").toString(), {}, {}};
    if (object.contains("description")) result.description = object.value("description").toString();
    if (object.contains("iconPath")) {
        result.iconPath = decodeIconPath(object.value("iconPath"));
        if (!result.iconPath) return {};
    }
    return result;
}
void optionalBool(QJsonObject& object, const char* key, const std::optional<bool>& value) { if (value) object.insert(key, *value); }
std::optional<bool> readBool(const QJsonObject& object, const char* key) { return object.contains(key) ? std::optional(object.value(key).toBool()) : std::nullopt; }
}
QJsonObject encodeWorkspaceEdit(const WorkspaceEditRequest& request) {
    QJsonArray entries;
    for (const auto& entry : request.entries) std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        QJsonObject object, options;
        if constexpr (std::is_same_v<T, detail::WorkspaceTextEntry>) {
            object = {{"kind", "text"}, {"uri", value.uri.toString(QUrl::FullyEncoded)}, {"text", value.edit.newText}};
            object.insert("range", encodeDocumentChange({"1", 1, 2, value.edit.range, {}, false, {}}).value("range"));
            if (value.edit.newEol) object.insert("newEol", static_cast<int>(*value.edit.newEol));
        } else if constexpr (std::is_same_v<T, detail::WorkspaceRenameEntry>) {
            object = {{"kind", "rename"}, {"uri", value.from.toString(QUrl::FullyEncoded)}, {"to", value.to.toString(QUrl::FullyEncoded)}};
            optionalBool(options, "overwrite", value.options.overwrite); optionalBool(options, "ignoreIfExists", value.options.ignoreIfExists);
        } else if constexpr (std::is_same_v<T, detail::WorkspaceCreateEntry>) {
            object = {{"kind", "create"}, {"uri", value.uri.toString(QUrl::FullyEncoded)}};
            optionalBool(options, "overwrite", value.options.overwrite); optionalBool(options, "ignoreIfExists", value.options.ignoreIfExists);
            if (value.options.contents) options.insert("contents", QString::fromLatin1(value.options.contents->toBase64()));
        } else {
            object = {{"kind", "delete"}, {"uri", value.uri.toString(QUrl::FullyEncoded)}};
            optionalBool(options, "recursive", value.options.recursive); optionalBool(options, "ignoreIfNotExists", value.options.ignoreIfNotExists);
        }
        if (!options.isEmpty()) object.insert("options", options);
        if (value.metadata) object.insert("metadata", encodeMetadata(*value.metadata));
        entries.append(object);
    }, entry);
    QJsonObject versions;
    for (auto it = request.versions.cbegin(); it != request.versions.cend(); ++it) versions.insert(it.key(), QString::number(it.value()));
    QJsonObject result{{"entries", entries}, {"versions", versions}};
    if (request.metadata && request.metadata->isRefactoring) result.insert("isRefactoring", *request.metadata->isRefactoring);
    return result;
}
std::optional<WorkspaceEditRequest> decodeWorkspaceEdit(const QJsonObject& message) {
    if (!message.value("entries").isArray() || !message.value("versions").isObject() ||
        (message.contains("isRefactoring") && !message.value("isRefactoring").isBool())) return {};
    WorkspaceEditRequest result;
    if (message.contains("isRefactoring")) result.metadata = WorkspaceEditMetadata{message.value("isRefactoring").toBool()};
    const auto versions = message.value("versions").toObject();
    for (auto it = versions.begin(); it != versions.end(); ++it) {
        bool valid = false;
        const auto text = it.value().toString();
        const auto version = text.toLongLong(&valid);
        if (!valid || version < 1 || QString::number(version) != text) return {};
        result.versions.insert(it.key(), version);
    }
    for (const auto& encoded : message.value("entries").toArray()) {
        if (!encoded.isObject()) return {};
        const auto object = encoded.toObject();
        if (!object.value("uri").isString()) return {};
        const QUrl uri(object.value("uri").toString());
        if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty()) return {};
        std::optional<WorkspaceEditEntryMetadata> metadata;
        if (object.contains("metadata")) { metadata = decodeMetadata(object.value("metadata")); if (!metadata) return {}; }
        if (object.contains("options") && !object.value("options").isObject()) return {};
        const auto options = object.value("options").toObject();
        for (const auto* key : {"overwrite", "ignoreIfExists", "recursive", "ignoreIfNotExists"})
            if (options.contains(key) && !options.value(key).isBool()) return {};
        const auto kind = object.value("kind").toString();
        if (kind == "text") {
            auto value = object;
            value.insert("id", "1"); value.insert("previousVersion", "1"); value.insert("version", "2"); value.insert("dirty", false);
            const auto change = decodeDocumentChange(value);
            if (!change) return {};
            TextEdit edit(change->range, change->text);
            if (object.contains("newEol")) {
                const auto eol = object.value("newEol");
                if (!eol.isDouble() || (eol.toDouble() != 1 && eol.toDouble() != 2)) return {};
                edit.newEol = static_cast<EndOfLine>(eol.toInt());
            }
            result.entries.append(detail::WorkspaceTextEntry{uri, std::move(edit), metadata});
        } else if (kind == "create") {
            CreateFileOptions decoded{readBool(options, "overwrite"), readBool(options, "ignoreIfExists"), {}};
            if (options.contains("contents")) {
                if (!options.value("contents").isString()) return {};
                const auto encodedBytes = options.value("contents").toString().toLatin1();
                const auto bytes = QByteArray::fromBase64(encodedBytes);
                if (bytes.toBase64() != encodedBytes) return {};
                decoded.contents = bytes;
            }
            result.entries.append(detail::WorkspaceCreateEntry{uri, std::move(decoded), metadata});
        } else if (kind == "delete") {
            result.entries.append(detail::WorkspaceDeleteEntry{uri, {readBool(options, "recursive"), readBool(options, "ignoreIfNotExists")}, metadata});
        } else if (kind == "rename") {
            if (!object.value("to").isString()) return {};
            const QUrl to(object.value("to").toString());
            if (!to.isValid() || to.isEmpty() || to.scheme().isEmpty()) return {};
            result.entries.append(detail::WorkspaceRenameEntry{uri, to, {readBool(options, "overwrite"), readBool(options, "ignoreIfExists")}, metadata});
        } else return {};
    }
    return result;
}
}
