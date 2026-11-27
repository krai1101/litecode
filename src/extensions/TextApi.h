#pragma once
#include <QString>
#include <QUrl>
#include <QtGlobal>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
// Native value equivalents of the stable immutable text coordinates. Getter
// names match upstream properties; offsets remain UTF-16 code units. C++ value
// returns preserve equality, not JavaScript object identity.
struct PositionTranslateChange final {
    std::optional<int> lineDelta, characterDelta;
};
struct PositionWithChange final {
    std::optional<int> line, character;
};
class Position final {
  public:
    Position(int line, int character) : line_(line), character_(character) {
        if (line < 0 || character < 0)
            throw std::invalid_argument("Position coordinates must be non-negative.");
    }
    [[nodiscard]] int line() const { return line_; }
    [[nodiscard]] int character() const { return character_; }
    [[nodiscard]] bool isBefore(const Position& other) const { return compareTo(other) < 0; }
    [[nodiscard]] bool isBeforeOrEqual(const Position& other) const {
        return compareTo(other) <= 0;
    }
    [[nodiscard]] bool isAfter(const Position& other) const { return compareTo(other) > 0; }
    [[nodiscard]] bool isAfterOrEqual(const Position& other) const { return compareTo(other) >= 0; }
    [[nodiscard]] bool isEqual(const Position& other) const {
        return line_ == other.line_ && character_ == other.character_;
    }
    [[nodiscard]] int compareTo(const Position& other) const {
        if (line_ != other.line_)
            return line_ < other.line_ ? -1 : 1;
        if (character_ != other.character_)
            return character_ < other.character_ ? -1 : 1;
        return 0;
    }
    [[nodiscard]] Position translate(int lineDelta = 0, int characterDelta = 0) const {
        const auto line = static_cast<qint64>(line_) + lineDelta;
        const auto character = static_cast<qint64>(character_) + characterDelta;
        if (line < 0 || character < 0 || line > std::numeric_limits<int>::max() ||
            character > std::numeric_limits<int>::max())
            throw std::invalid_argument(
                "Translated position is outside the native coordinate range.");
        return Position(static_cast<int>(line), static_cast<int>(character));
    }
    [[nodiscard]] Position translate(const PositionTranslateChange& change) const {
        return translate(change.lineDelta.value_or(0), change.characterDelta.value_or(0));
    }
    [[nodiscard]] Position with(std::optional<int> line = {},
                                std::optional<int> character = {}) const {
        return Position(line.value_or(line_), character.value_or(character_));
    }
    [[nodiscard]] Position with(const PositionWithChange& change) const {
        return with(change.line, change.character);
    }

  private:
    int line_, character_;
};

struct RangeWithChange final {
    std::optional<Position> start, end;
};
class Range {
  public:
    Range(Position start, Position end)
        : start_(start.isBeforeOrEqual(end) ? start : end),
          end_(start.isBeforeOrEqual(end) ? end : start) {}
    Range(int startLine, int startCharacter, int endLine, int endCharacter)
        : Range(Position(startLine, startCharacter), Position(endLine, endCharacter)) {}
    [[nodiscard]] const Position& start() const { return start_; }
    [[nodiscard]] const Position& end() const { return end_; }
    [[nodiscard]] bool isEmpty() const { return start_.isEqual(end_); }
    [[nodiscard]] bool isSingleLine() const { return start_.line() == end_.line(); }
    [[nodiscard]] bool contains(const Position& position) const {
        return start_.isBeforeOrEqual(position) && end_.isAfterOrEqual(position);
    }
    [[nodiscard]] bool contains(const Range& range) const {
        return contains(range.start_) && contains(range.end_);
    }
    [[nodiscard]] bool isEqual(const Range& other) const {
        return start_.isEqual(other.start_) && end_.isEqual(other.end_);
    }
    [[nodiscard]] std::optional<Range> intersection(const Range& other) const {
        const auto start = start_.isAfter(other.start_) ? start_ : other.start_;
        const auto end = end_.isBefore(other.end_) ? end_ : other.end_;
        if (start.isAfter(end))
            return std::nullopt;
        return Range(start, end);
    }
    // union is a C++ keyword, like SecretStorage.delete_.
    [[nodiscard]] Range union_(const Range& other) const {
        return Range(start_.isBefore(other.start_) ? start_ : other.start_,
                     end_.isAfter(other.end_) ? end_ : other.end_);
    }
    [[nodiscard]] Range with(std::optional<Position> start = {},
                             std::optional<Position> end = {}) const {
        return Range(start.value_or(start_), end.value_or(end_));
    }
    [[nodiscard]] Range with(const RangeWithChange& change) const {
        return with(change.start, change.end);
    }

  private:
    Position start_, end_;
};

class Selection final : public Range {
  public:
    Selection(Position anchor, Position active)
        : Range(anchor, active), anchor_(anchor), active_(active) {}
    Selection(int anchorLine, int anchorCharacter, int activeLine, int activeCharacter)
        : Selection(Position(anchorLine, anchorCharacter), Position(activeLine, activeCharacter)) {}
    [[nodiscard]] const Position& anchor() const { return anchor_; }
    [[nodiscard]] const Position& active() const { return active_; }
    [[nodiscard]] bool isReversed() const { return anchor_.isEqual(end()); }

  private:
    Position anchor_, active_;
};

struct Location final {
    QUrl uri;
    Range range;
    Location(QUrl uri, Range range) : uri(std::move(uri)), range(std::move(range)) {}
    Location(QUrl uri, Position position) : Location(std::move(uri), Range(position, position)) {}
};

enum class EndOfLine { LF = 1, CRLF = 2 };
class TextEdit final {
  public:
    TextEdit(Range range, QString newText) : range(std::move(range)), newText(std::move(newText)) {}
    static TextEdit replace(Range range, QString newText) {
        return TextEdit(std::move(range), std::move(newText));
    }
    static TextEdit insert(Position position, QString newText) {
        return TextEdit(Range(position, position), std::move(newText));
    }
    static TextEdit delete_(Range range) { return TextEdit(std::move(range), {}); }
    static TextEdit setEndOfLine(EndOfLine eol) {
        if (eol != EndOfLine::LF && eol != EndOfLine::CRLF)
            throw std::invalid_argument("Invalid end-of-line kind.");
        auto edit = TextEdit(Range(0, 0, 0, 0), {});
        edit.newEol = eol;
        return edit;
    }
    Range range;
    QString newText;
    std::optional<EndOfLine> newEol;
};
} // namespace litecode::extensions
