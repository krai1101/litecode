#include "editor/DocumentSession.h"
#include "core/FileSystemPath.h"
#include "core/TextEncoding.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <utility>

namespace litecode::editor {
namespace {

core::LineEnding detectLineEnding(const QByteArray& text) {
    qint64 lf = 0;
    qint64 crlf = 0;
    qint64 cr = 0;
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (text.at(index) == '\r') {
            if (index + 1 < text.size() && text.at(index + 1) == '\n') {
                ++crlf;
                ++index;
            } else {
                ++cr;
            }
        } else if (text.at(index) == '\n') {
            ++lf;
        }
    }
    const qint64 total = lf + crlf + cr;
    if (total > 0)
        return cr + crlf > total / 2 ? core::LineEnding::CrLf : core::LineEnding::Lf;
#ifdef Q_OS_WIN
    return core::LineEnding::CrLf;
#else
    return core::LineEnding::Lf;
#endif
}

QByteArray normalizedLineEndings(const QByteArray& text, core::LineEnding lineEnding) {
    const QByteArray replacement =
        lineEnding == core::LineEnding::CrLf ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
    QByteArray normalized;
    normalized.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index) {
        const char character = text.at(index);
        if (character == '\r') {
            if (index + 1 < text.size() && text.at(index + 1) == '\n')
                ++index;
            normalized.append(replacement);
        } else if (character == '\n') {
            normalized.append(replacement);
        } else {
            normalized.append(character);
        }
    }
    return normalized;
}

bool writeAtomically(const QString& path, const QByteArray& contents, QString* errorMessage) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    if (file.write(contents) != contents.size()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}

QByteArray fingerprint(QByteArrayView contents) {
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
}

std::optional<QByteArray> fingerprintOnDisk(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError)
        return std::nullopt;
    return hash.result();
}

bool pathMatches(const QString& left, const QString& right) {
    return core::pathsReferToSameEntry(left, right);
}

bool canOverwrite(const QString& path, const QByteArray& expectedFingerprint,
                  QString* errorMessage) {
    const std::optional<QByteArray> current = fingerprintOnDisk(path);
    if (current && *current == expectedFingerprint)
        return true;
    if (errorMessage) {
        *errorMessage = QStringLiteral(
            "The file changed on disk after it was opened. Reload or use Save As to preserve "
            "both versions.");
    }
    return false;
}

bool canSaveDecodedContents(bool hadDecodingErrors, QString* errorMessage) {
    if (!hadDecodingErrors)
        return true;
    if (errorMessage) {
        *errorMessage = QStringLiteral(
            "The file contained byte sequences that could not be decoded safely. Reopen it "
            "with the correct encoding, or use Save As to preserve the original file.");
    }
    return false;
}

} // namespace

DocumentSession::DocumentSession(core::DocumentId id, QString filePath)
    : id_(id), filePath_(QFileInfo(filePath).absoluteFilePath()) {}

QString DocumentSession::displayName() const { return QFileInfo(filePath_).fileName(); }

DocumentLoadResult DocumentSession::loadBounded(const QString& filePath,
                                                std::optional<core::TextEncoding> requestedEncoding,
                                                core::TextEncoding defaultEncoding,
                                                bool autoGuessEncoding) {
    DocumentLoadResult result;
    result.requestedEncoding = requestedEncoding;
    result.defaultEncoding = defaultEncoding;
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        result.diagnostic =
            QStringLiteral("The selected file does not exist or is not a regular file.");
        return result;
    }
    result.readOnly = !info.isWritable();
    result.sourceSize = info.size();
    result.sourceModifiedMs = info.lastModified().toMSecsSinceEpoch();
    result.sourceIdentity = core::fileSystemEntryIdentity(info.absoluteFilePath());
    if (info.size() > maximumFileLimit()) {
        result.outcome = DocumentLoadResult::Outcome::Refused;
        result.diagnostic =
            QStringLiteral("The file is larger than LiteCode's 32 MiB safety limit.");
        return result;
    }
    result.storageMode = info.size() > normalFileLimit() ? core::DocumentStorageMode::Large
                                                         : core::DocumentStorageMode::Normal;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        result.diagnostic = file.errorString();
        return result;
    }
    result.contents.resize(static_cast<qsizetype>(info.size()));
    qint64 offset = 0;
    while (offset < info.size()) {
        const qint64 count = file.read(result.contents.data() + offset, info.size() - offset);
        if (count <= 0) {
            result.contents.clear();
            result.diagnostic = file.errorString().isEmpty()
                                    ? QStringLiteral("The file could not be read completely.")
                                    : file.errorString();
            return result;
        }
        offset += count;
    }
    result.sourceFingerprint = fingerprint(result.contents);
    QFileInfo completedInfo(info.absoluteFilePath());
    completedInfo.refresh();
    const QByteArray completedIdentity =
        core::fileSystemEntryIdentity(completedInfo.absoluteFilePath());
    if (!completedInfo.exists() || completedInfo.size() != result.sourceSize ||
        completedInfo.lastModified().toMSecsSinceEpoch() != result.sourceModifiedMs ||
        (!result.sourceIdentity.isEmpty() && completedIdentity != result.sourceIdentity)) {
        result.contents.clear();
        result.diagnostic =
            QStringLiteral("The file changed while it was being read. Please try again.");
        return result;
    }
    QByteArray decoded;
    const core::TextEncoding decodingFallback =
        !requestedEncoding && defaultEncoding == core::TextEncoding::Utf8Bom
            ? core::TextEncoding::Utf8
            : requestedEncoding.value_or(defaultEncoding);
    if (!core::decodeText(result.contents, decodingFallback, !requestedEncoding, &decoded,
                          &result.encoding, &result.diagnostic, &result.hasByteOrderMark,
                          autoGuessEncoding && !requestedEncoding, &result.hadDecodingErrors)) {
        result.outcome = result.diagnostic == QStringLiteral("The file appears to be binary.")
                             ? DocumentLoadResult::Outcome::Binary
                             : DocumentLoadResult::Outcome::Refused;
        result.contents.clear();
        if (!requestedEncoding && result.outcome == DocumentLoadResult::Outcome::Binary)
            result.diagnostic +=
                QStringLiteral(" If this is BOM-less text, use Open File with Encoding.");
        return result;
    }
    if (requestedEncoding == core::TextEncoding::Utf8 &&
        result.encoding == core::TextEncoding::Utf8Bom)
        result.requestedEncoding = core::TextEncoding::Utf8Bom;
    result.writeByteOrderMark =
        result.encoding == core::TextEncoding::Utf8Bom ||
        result.encoding == core::TextEncoding::Utf16Le ||
        result.encoding == core::TextEncoding::Utf16Be ||
        (!requestedEncoding && defaultEncoding == core::TextEncoding::Utf8Bom &&
         result.encoding == core::TextEncoding::Utf8);
    result.lineEnding = detectLineEnding(decoded);
    result.contents = normalizedLineEndings(decoded, result.lineEnding);
    result.outcome = DocumentLoadResult::Outcome::Loaded;
    return result;
}

bool DocumentSession::matchesDiskSnapshot(const QString& filePath,
                                          const DocumentLoadResult& result) {
    if (result.outcome != DocumentLoadResult::Outcome::Loaded)
        return false;
    QFileInfo info(filePath);
    info.refresh();
    if (!info.exists() || !info.isFile() || info.size() != result.sourceSize ||
        info.lastModified().toMSecsSinceEpoch() != result.sourceModifiedMs)
        return false;
    const QByteArray identity = core::fileSystemEntryIdentity(info.absoluteFilePath());
    return result.sourceIdentity.isEmpty() || identity == result.sourceIdentity;
}

bool DocumentSession::acceptLoad(const DocumentLoadResult& result) {
    if (state_ != DocumentState::Loading || result.outcome != DocumentLoadResult::Outcome::Loaded)
        return false;
    encoding_ = result.encoding;
    preferredEncoding_ = result.requestedEncoding;
    defaultEncoding_ = result.defaultEncoding;
    hasByteOrderMark_ = result.hasByteOrderMark;
    writeByteOrderMark_ = result.writeByteOrderMark;
    hadDecodingErrors_ = result.hadDecodingErrors;
    readOnly_ = result.readOnly;
    sourceFingerprint_ = result.sourceFingerprint;
    lineEnding_ = result.lineEnding;
    storageMode_ = result.storageMode;
    state_ = DocumentState::OpenClean;
    return true;
}

bool DocumentSession::acceptReload(const DocumentLoadResult& result) {
    if (state_ == DocumentState::Closed || state_ == DocumentState::Loading ||
        result.outcome != DocumentLoadResult::Outcome::Loaded)
        return false;
    encoding_ = result.encoding;
    preferredEncoding_ = result.requestedEncoding;
    defaultEncoding_ = result.defaultEncoding;
    hasByteOrderMark_ = result.hasByteOrderMark;
    writeByteOrderMark_ = result.writeByteOrderMark;
    hadDecodingErrors_ = result.hadDecodingErrors;
    readOnly_ = result.readOnly;
    sourceFingerprint_ = result.sourceFingerprint;
    lineEnding_ = result.lineEnding;
    storageMode_ = result.storageMode;
    state_ = DocumentState::OpenClean;
    deleted_ = false;
    ++version_;
    return true;
}

bool DocumentSession::applyEdit(core::DocumentEdit* edit) {
    if (!edit || state_ == DocumentState::Closed || edit->position < 0 || edit->removedLength < 0)
        return false;
    ++version_;
    edit->documentId = id_;
    edit->filePath = filePath_;
    edit->version = version_;
    state_ = DocumentState::OpenDirty;
    return true;
}

void DocumentSession::setModified(bool modified) {
    if (state_ == DocumentState::Closed || state_ == DocumentState::Loading)
        return;
    state_ = modified ? DocumentState::OpenDirty : DocumentState::OpenClean;
}

void DocumentSession::markExternallyModified() {
    if (state_ != DocumentState::Closed)
        state_ = DocumentState::ExternallyModified;
}

void DocumentSession::markDeleted() {
    if (state_ != DocumentState::Closed)
        deleted_ = true;
}

void DocumentSession::markRestored() { deleted_ = false; }

void DocumentSession::close() {
    deleted_ = false;
    state_ = DocumentState::Closed;
}

void DocumentSession::rebindPath(const QString& filePath, core::DocumentPathChange* pathChange) {
    const QString nextPath = QFileInfo(filePath).absoluteFilePath();
    if (pathChange)
        *pathChange = {id_, filePath_, nextPath, version_};
    filePath_ = nextPath;
}

bool DocumentSession::save(const QByteArray& contents, QString* errorMessage) {
    if (state_ == DocumentState::Closed || state_ == DocumentState::Loading) {
        if (errorMessage)
            *errorMessage = state_ == DocumentState::Closed
                                ? QStringLiteral("The document is already closed.")
                                : QStringLiteral("The document is still loading.");
        return false;
    }
    if (!canSaveDecodedContents(hadDecodingErrors_, errorMessage) ||
        !canOverwrite(filePath_, sourceFingerprint_, errorMessage))
        return false;
    QByteArray encoded;
    if (!core::encodeText(contents, encoding_, &encoded, errorMessage, writeByteOrderMark_) ||
        !writeAtomically(filePath_, encoded, errorMessage))
        return false;
    hasByteOrderMark_ = writeByteOrderMark_;
    sourceFingerprint_ = fingerprint(encoded);
    readOnly_ = !QFileInfo(filePath_).isWritable();
    state_ = DocumentState::OpenClean;
    deleted_ = false;
    return true;
}

bool DocumentSession::saveWithEncoding(const QByteArray& contents, core::TextEncoding encoding,
                                       QString* errorMessage) {
    if (state_ == DocumentState::Closed || state_ == DocumentState::Loading) {
        if (errorMessage)
            *errorMessage = state_ == DocumentState::Closed
                                ? QStringLiteral("The document is already closed.")
                                : QStringLiteral("The document is still loading.");
        return false;
    }
    if (!canSaveDecodedContents(hadDecodingErrors_, errorMessage) ||
        !canOverwrite(filePath_, sourceFingerprint_, errorMessage))
        return false;
    QByteArray encoded;
    if (!core::encodeText(contents, encoding, &encoded, errorMessage) ||
        !writeAtomically(filePath_, encoded, errorMessage))
        return false;
    encoding_ = encoding;
    preferredEncoding_ = encoding;
    hasByteOrderMark_ = encoding == core::TextEncoding::Utf8Bom ||
                        encoding == core::TextEncoding::Utf16Le ||
                        encoding == core::TextEncoding::Utf16Be;
    writeByteOrderMark_ = hasByteOrderMark_;
    hadDecodingErrors_ = false;
    sourceFingerprint_ = fingerprint(encoded);
    readOnly_ = !QFileInfo(filePath_).isWritable();
    state_ = DocumentState::OpenClean;
    deleted_ = false;
    return true;
}

bool DocumentSession::saveAs(const QString& filePath, const QByteArray& contents,
                             core::DocumentPathChange* pathChange, QString* errorMessage) {
    if (state_ == DocumentState::Closed || state_ == DocumentState::Loading) {
        if (errorMessage)
            *errorMessage = state_ == DocumentState::Closed
                                ? QStringLiteral("The document is already closed.")
                                : QStringLiteral("The document is still loading.");
        return false;
    }
    QByteArray encoded;
    if (!core::encodeText(contents, encoding_, &encoded, errorMessage, writeByteOrderMark_))
        return false;
    const QString nextPath = QFileInfo(filePath).absoluteFilePath();
    if (pathMatches(nextPath, filePath_) &&
        (!canSaveDecodedContents(hadDecodingErrors_, errorMessage) ||
         !canOverwrite(filePath_, sourceFingerprint_, errorMessage)))
        return false;
    if (!writeAtomically(nextPath, encoded, errorMessage))
        return false;
    if (pathChange) {
        *pathChange = {id_, filePath_, nextPath, version_};
    }
    filePath_ = nextPath;
    hasByteOrderMark_ = writeByteOrderMark_;
    hadDecodingErrors_ = false;
    sourceFingerprint_ = fingerprint(encoded);
    readOnly_ = !QFileInfo(filePath_).isWritable();
    state_ = DocumentState::OpenClean;
    deleted_ = false;
    return true;
}

} // namespace litecode::editor
