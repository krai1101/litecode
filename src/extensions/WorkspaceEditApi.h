#pragma once
#include "extensions/TextApi.h"
#include "extensions/ThemeApi.h"
#include <QByteArray>
#include <QVector>
#include <QPair>
#include <optional>
#include <variant>
#include <algorithm>

namespace litecode::extensions {
struct WorkspaceEditEntryMetadata final {
    bool needsConfirmation{};
    QString label;
    std::optional<QString> description;
    std::optional<IconPath> iconPath;
};
struct WorkspaceEditMetadata final { std::optional<bool> isRefactoring; };
struct CreateFileOptions final {
    std::optional<bool> overwrite, ignoreIfExists;
    std::optional<QByteArray> contents;
};
struct DeleteFileOptions final { std::optional<bool> recursive, ignoreIfNotExists; };
struct RenameFileOptions final { std::optional<bool> overwrite, ignoreIfExists; };
namespace detail {
struct WorkspaceTextEntry { QUrl uri; TextEdit edit; std::optional<WorkspaceEditEntryMetadata> metadata; };
struct WorkspaceCreateEntry { QUrl uri; CreateFileOptions options; std::optional<WorkspaceEditEntryMetadata> metadata; };
struct WorkspaceDeleteEntry { QUrl uri; DeleteFileOptions options; std::optional<WorkspaceEditEntryMetadata> metadata; };
struct WorkspaceRenameEntry { QUrl from, to; RenameFileOptions options; std::optional<WorkspaceEditEntryMetadata> metadata; };
using WorkspaceEditEntry = std::variant<WorkspaceTextEntry, WorkspaceCreateEntry, WorkspaceDeleteEntry, WorkspaceRenameEntry>;
class WorkspaceEditAccess;
}
// Experimental while snippet/notebook/DataTransferFile overloads and backend
// integration are being completed. Never advertise this as WorkspaceEdit parity.
class WorkspaceEdit final {
  public:
    void replace(const QUrl& uri, Range range, QString newText, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        entries_.append(detail::WorkspaceTextEntry{uri, TextEdit(std::move(range), std::move(newText)), std::move(metadata)});
    }
    void insert(const QUrl& uri, Position position, QString text, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        replace(uri, Range(position, position), std::move(text), std::move(metadata));
    }
    void delete_(const QUrl& uri, Range range, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        replace(uri, std::move(range), {}, std::move(metadata));
    }
    void set(const QUrl& uri, const QVector<TextEdit>& edits) {
        for (const auto& edit : edits) entries_.append(detail::WorkspaceTextEntry{uri, edit, {}});
    }
    void set(const QUrl& uri, const QVector<QPair<TextEdit, std::optional<WorkspaceEditEntryMetadata>>>& edits) {
        for (const auto& edit : edits) entries_.append(detail::WorkspaceTextEntry{uri, edit.first, edit.second});
    }
    void set(const QUrl& uri, std::nullopt_t) {
        entries_.removeIf([&](const detail::WorkspaceEditEntry& entry) {
            const auto* text = std::get_if<detail::WorkspaceTextEntry>(&entry);
            return text && key(text->uri) == key(uri);
        });
    }
    [[nodiscard]] QVector<TextEdit> get(const QUrl& uri) const {
        QVector<TextEdit> result;
        for (const auto& entry : entries_) if (const auto* text = std::get_if<detail::WorkspaceTextEntry>(&entry);
            text && key(text->uri) == key(uri)) result.append(text->edit);
        return result;
    }
    [[nodiscard]] bool has(const QUrl& uri) const { return !get(uri).isEmpty(); }
    [[nodiscard]] QVector<QPair<QUrl, QVector<TextEdit>>> entries() const {
        QVector<QPair<QUrl, QVector<TextEdit>>> result;
        for (const auto& entry : entries_) if (const auto* text = std::get_if<detail::WorkspaceTextEntry>(&entry)) {
            auto found = std::find_if(result.begin(), result.end(), [&](const auto& group) { return key(group.first) == key(text->uri); });
            if (found == result.end()) result.append({text->uri, {text->edit}});
            else found->second.append(text->edit);
        }
        return result;
    }
    [[nodiscard]] qsizetype size() const { return entries().size(); }
    void createFile(const QUrl& uri, CreateFileOptions options = {}, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        entries_.append(detail::WorkspaceCreateEntry{uri, std::move(options), std::move(metadata)});
    }
    void deleteFile(const QUrl& uri, DeleteFileOptions options = {}, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        entries_.append(detail::WorkspaceDeleteEntry{uri, std::move(options), std::move(metadata)});
    }
    void renameFile(const QUrl& from, const QUrl& to, RenameFileOptions options = {}, std::optional<WorkspaceEditEntryMetadata> metadata = {}) {
        entries_.append(detail::WorkspaceRenameEntry{from, to, std::move(options), std::move(metadata)});
    }
  private:
    friend class detail::WorkspaceEditAccess;
    static QString key(const QUrl& uri) { return uri.toString(QUrl::FullyEncoded); }
    QVector<detail::WorkspaceEditEntry> entries_;
};
} // namespace litecode::extensions
