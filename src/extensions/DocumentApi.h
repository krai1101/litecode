#pragma once
#include "extensions/TextApi.h"
#include <QFuture>
#include <QRegularExpression>
#include <QVector>
#include <memory>

namespace litecode::extensions {
class HostTextDocument;
class TextLine final {
  public:
    [[nodiscard]] int lineNumber() const { return line_; }
    [[nodiscard]] QString text() const { return text_; }
    [[nodiscard]] Range range() const { return Range(line_, 0, line_, static_cast<int>(text_.size())); }
    [[nodiscard]] Range rangeIncludingLineBreak() const { return last_ ? range() : Range(line_, 0, line_ + 1, 0); }
    [[nodiscard]] int firstNonWhitespaceCharacterIndex() const {
        int index = 0;
        // ECMAScript \s includes BOM but excludes Unicode NEL (U+0085).
        while (index < text_.size() && ((text_.at(index).isSpace() && text_.at(index).unicode() != 0x85) || text_.at(index).unicode() == 0xfeff)) ++index;
        return index;
    }
    [[nodiscard]] bool isEmptyOrWhitespace() const { return firstNonWhitespaceCharacterIndex() == text_.size(); }
  private:
    friend class HostTextDocument;
    TextLine(int line, QString text, bool last) : line_(line), text_(std::move(text)), last_(last) {}
    int line_;
    QString text_;
    bool last_;
};
class TextDocument {
  public:
    virtual ~TextDocument() = default;
    [[nodiscard]] virtual QUrl uri() const = 0;
    [[nodiscard]] virtual QString fileName() const = 0;
    [[nodiscard]] virtual bool isUntitled() const = 0;
    [[nodiscard]] virtual QString languageId() const = 0;
    [[nodiscard]] virtual QString encoding() const = 0;
    [[nodiscard]] virtual qint64 version() const = 0;
    [[nodiscard]] virtual bool isDirty() const = 0;
    [[nodiscard]] virtual bool isClosed() const = 0;
    virtual QFuture<bool> save() = 0;
    [[nodiscard]] virtual EndOfLine eol() const = 0;
    [[nodiscard]] virtual int lineCount() const = 0;
    [[nodiscard]] virtual TextLine lineAt(int line) const = 0;
    [[nodiscard]] virtual TextLine lineAt(const Position& position) const = 0;
    [[nodiscard]] virtual qint64 offsetAt(const Position& position) const = 0;
    [[nodiscard]] virtual Position positionAt(qint64 offset) const = 0;
    [[nodiscard]] virtual QString getText(const std::optional<Range>& range = {}) const = 0;
    [[nodiscard]] virtual std::optional<Range> getWordRangeAtPosition(const Position& position,
        const std::optional<QRegularExpression>& regex = {}) const = 0;
    [[nodiscard]] virtual Range validateRange(const Range& range) const = 0;
    [[nodiscard]] virtual Position validatePosition(const Position& position) const = 0;
};
enum class TextDocumentChangeReason { Undo = 1, Redo = 2 };
struct TextDocumentContentChangeEvent final {
    Range range;
    qint64 rangeOffset{}, rangeLength{};
    QString text;
};
struct TextDocumentChangeEvent final {
    std::shared_ptr<TextDocument> document;
    QVector<TextDocumentContentChangeEvent> contentChanges;
    std::optional<TextDocumentChangeReason> reason;
};
} // namespace litecode::extensions
