#pragma once

#include "core/DocumentTypes.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace litecode::editor {

enum class DocumentState {
    Loading,
    OpenClean,
    OpenDirty,
    ExternallyModified,
    Deleted,
    Closed,
};

struct DocumentLoadResult final {
    enum class Outcome {
        Loaded,
        Binary,
        Refused,
        Failed,
    };

    Outcome outcome{Outcome::Failed};
    QByteArray contents;
    QString diagnostic;
    core::DocumentStorageMode storageMode{core::DocumentStorageMode::Normal};
    core::TextEncoding encoding{core::TextEncoding::Utf8};
    std::optional<core::TextEncoding> requestedEncoding;
    core::TextEncoding defaultEncoding{core::TextEncoding::Utf8};
    bool hasByteOrderMark{};
    bool writeByteOrderMark{};
    bool hadDecodingErrors{};
    bool readOnly{};
    QByteArray sourceFingerprint;
    QByteArray sourceIdentity;
    qint64 sourceSize{-1};
    qint64 sourceModifiedMs{-1};
    core::LineEnding lineEnding{core::LineEnding::None};
};

class DocumentSession final {
  public:
    static constexpr qint64 normalFileLimit() noexcept { return 4 * 1024 * 1024; }
    static constexpr qint64 maximumFileLimit() noexcept { return 32 * 1024 * 1024; }

    DocumentSession(core::DocumentId id, QString filePath);

    [[nodiscard]] core::DocumentId id() const noexcept { return id_; }
    [[nodiscard]] const QString& filePath() const noexcept { return filePath_; }
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] qint64 version() const noexcept { return version_; }
    [[nodiscard]] core::TextEncoding encoding() const noexcept { return encoding_; }
    [[nodiscard]] std::optional<core::TextEncoding> preferredEncoding() const noexcept {
        return preferredEncoding_;
    }
    [[nodiscard]] core::TextEncoding defaultEncoding() const noexcept { return defaultEncoding_; }
    [[nodiscard]] bool hasByteOrderMark() const noexcept { return hasByteOrderMark_; }
    [[nodiscard]] bool hadDecodingErrors() const noexcept { return hadDecodingErrors_; }
    [[nodiscard]] bool isReadOnly() const noexcept { return readOnly_; }
    [[nodiscard]] core::LineEnding lineEnding() const noexcept { return lineEnding_; }
    [[nodiscard]] core::DocumentStorageMode storageMode() const noexcept { return storageMode_; }
    [[nodiscard]] DocumentState state() const noexcept {
        return deleted_ ? DocumentState::Deleted : state_;
    }
    [[nodiscard]] bool isModified() const noexcept { return state_ == DocumentState::OpenDirty; }
    [[nodiscard]] bool isClosed() const noexcept { return state_ == DocumentState::Closed; }
    [[nodiscard]] bool isDeleted() const noexcept { return deleted_; }

    [[nodiscard]] static DocumentLoadResult
    loadBounded(const QString& filePath,
                std::optional<core::TextEncoding> requestedEncoding = std::nullopt,
                core::TextEncoding defaultEncoding = core::TextEncoding::Utf8,
                bool autoGuessEncoding = false);
    [[nodiscard]] static bool matchesDiskSnapshot(const QString& filePath,
                                                  const DocumentLoadResult& result);
    bool acceptLoad(const DocumentLoadResult& result);
    bool acceptReload(const DocumentLoadResult& result);
    bool applyEdit(core::DocumentEdit* edit);
    void setModified(bool modified);
    void markExternallyModified();
    void markDeleted();
    void markRestored();
    void close();
    void rebindPath(const QString& filePath, core::DocumentPathChange* pathChange = nullptr);
    bool save(const QByteArray& contents, QString* errorMessage);
    bool saveWithEncoding(const QByteArray& contents, core::TextEncoding encoding,
                          QString* errorMessage);
    bool saveAs(const QString& filePath, const QByteArray& contents,
                core::DocumentPathChange* pathChange, QString* errorMessage);

  private:
    core::DocumentId id_;
    QString filePath_;
    qint64 version_{1};
    core::TextEncoding encoding_{core::TextEncoding::Utf8};
    std::optional<core::TextEncoding> preferredEncoding_;
    core::TextEncoding defaultEncoding_{core::TextEncoding::Utf8};
    bool hasByteOrderMark_{};
    bool writeByteOrderMark_{};
    bool hadDecodingErrors_{};
    bool readOnly_{};
    QByteArray sourceFingerprint_;
    core::LineEnding lineEnding_{core::LineEnding::None};
    core::DocumentStorageMode storageMode_{core::DocumentStorageMode::Normal};
    DocumentState state_{DocumentState::Loading};
    bool deleted_{};
};

} // namespace litecode::editor
