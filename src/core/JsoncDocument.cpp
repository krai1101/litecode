#include "core/JsoncDocument.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVector>
#include <algorithm>
#include <stdexcept>

namespace litecode::core {
namespace {

struct Node;
struct Property final {
    QString name;
    qsizetype start{};
    qsizetype comma{-1};
    std::shared_ptr<Node> node;
};
struct Node final {
    qsizetype start{}, end{};
    QJsonValue value;
    QVector<Property> properties;
};

QByteArray encode(const QJsonValue& value) {
    // The Qt JSON serializer accepts only documents; wrapping also handles
    // primitives without hand-written string escaping or number formatting.
    auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return bytes.mid(1, bytes.size() - 2);
}

class Parser final {
  public:
    explicit Parser(const QByteArray& source) : source_(source) {
        if (source_.startsWith(QByteArray::fromHex("efbbbf"))) offset_ = 3;
    }
    std::shared_ptr<Node> document(bool allowEmptyContent = false) {
        trivia();
        if (offset_ == source_.size()) {
            if (!allowEmptyContent) fail("Expected JSON document.");
            return std::make_shared<Node>(Node{offset_, offset_, QJsonObject{}, {}});
        }
        auto result = node(0);
        trivia();
        if (offset_ != source_.size()) fail("Unexpected content after JSON value.");
        return result;
    }

  private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error((QString::fromLatin1(message) + " At byte " +
                                  QString::number(offset_) + '.').toStdString());
    }
    char peek() const { return offset_ < source_.size() ? source_.at(offset_) : '\0'; }
    void trivia() {
        for (;;) {
            while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++offset_;
            if (peek() != '/' || offset_ + 1 >= source_.size()) return;
            const auto next = source_.at(offset_ + 1);
            if (next == '/') {
                offset_ += 2;
                while (offset_ < source_.size() && peek() != '\r' && peek() != '\n') ++offset_;
            } else if (next == '*') {
                offset_ += 2;
                const auto end = source_.indexOf("*/", offset_);
                if (end < 0) fail("Unterminated JSON block comment.");
                offset_ = end + 2;
            } else return;
        }
    }
    QJsonValue primitive() {
        const auto start = offset_;
        if (peek() == '"') {
            ++offset_;
            bool closed = false;
            while (offset_ < source_.size()) {
                const auto ch = source_.at(offset_++);
                if (ch == '\\') {
                    if (offset_ == source_.size()) fail("Unterminated JSON string.");
                    ++offset_;
                } else if (ch == '"') { closed = true; break; }
            }
            if (!closed) fail("Unterminated JSON string.");
        } else {
            while (offset_ < source_.size()) {
                const auto ch = peek();
                if (ch == ',' || ch == ']' || ch == '}' || ch == '/' ||
                    ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
                ++offset_;
            }
        }
        if (start == offset_) fail("Expected JSON value.");
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(
            '[' + source_.mid(start, offset_ - start) + ']', &error);
        if (error.error != QJsonParseError::NoError || !parsed.isArray() || parsed.array().size() != 1)
            fail("Invalid JSON value.");
        return parsed.array().at(0);
    }
    std::shared_ptr<Node> node(int depth) {
        if (depth > 128) fail("JSON nesting is too deep.");
        trivia();
        auto result = std::make_shared<Node>();
        result->start = offset_;
        const auto opening = peek();
        if (opening != '{' && opening != '[') {
            result->value = primitive();
            result->end = offset_;
            return result;
        }
        ++offset_;
        trivia();
        QJsonObject object;
        QJsonArray array;
        const auto closing = opening == '{' ? '}' : ']';
        while (peek() != closing) {
            Property property;
            if (opening == '{') {
                if (peek() != '"') fail("Expected JSON property name.");
                property.start = offset_;
                property.name = primitive().toString();
                trivia();
                if (peek() != ':') fail("Expected colon after JSON property name.");
                ++offset_;
            }
            auto child = node(depth + 1);
            if (opening == '{') {
                property.node = child;
                object.insert(property.name, child->value);
            } else array.append(child->value);
            trivia();
            if (peek() == ',') {
                property.comma = offset_++;
                if (opening == '{') result->properties.append(std::move(property));
                trivia();
            } else {
                if (opening == '{') result->properties.append(std::move(property));
                if (peek() != closing) fail("Expected comma or closing delimiter.");
            }
        }
        ++offset_;
        result->end = offset_;
        result->value = opening == '{' ? QJsonValue(object) : QJsonValue(array);
        return result;
    }
    const QByteArray& source_;
    qsizetype offset_{};
};

const Property* findProperty(const Node& node, const QString& name, bool editing) {
    const Property* result = nullptr;
    for (const auto& property : node.properties) {
        if (property.name != name) continue;
        // Reading follows JSON last-value semantics. Editing an ambiguous
        // duplicate would leave competing settings behind, so refuse the edit.
        if (result && editing)
            throw std::runtime_error("Cannot edit a duplicated JSON property. Remove the duplicate first.");
        result = &property;
    }
    return result;
}

struct Edit final { qsizetype offset{}, length{}; QByteArray text; };

QByteArray applyEdits(QByteArray source, QVector<Edit> edits) {
    std::sort(edits.begin(), edits.end(), [](const auto& first, const auto& second) {
        return first.offset > second.offset;
    });
    for (const auto& edit : edits) source.replace(edit.offset, edit.length, edit.text);
    // Guard the text-edit algorithm too, before any caller persists it.
    Parser(source).document();
    return source;
}

QByteArray indentation(const QByteArray& source, qsizetype offset) {
    if (offset <= 0) return {};
    const auto line = source.lastIndexOf('\n', offset - 1) + 1;
    QByteArray result;
    for (auto index = line; index < offset; ++index) {
        if (source.at(index) != ' ' && source.at(index) != '\t') break;
        result += source.at(index);
    }
    return result;
}

} // namespace

struct JsoncDocument::Data final {
    QByteArray source;
    std::shared_ptr<Node> root;
};

JsoncDocument::JsoncDocument(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

JsoncDocument JsoncDocument::parse(QByteArray source, bool allowEmptyContent) {
    auto root = Parser(source).document(allowEmptyContent);
    return JsoncDocument(std::make_shared<Data>(Data{std::move(source), std::move(root)}));
}

QJsonValue JsoncDocument::value() const { return data_->root->value; }
const QByteArray& JsoncDocument::source() const { return data_->source; }

QStringList JsoncDocument::propertyNames(const QStringList& path) const {
    auto* node = data_->root.get();
    for (const auto& segment : path) {
        const auto* property = findProperty(*node, segment, false);
        if (!property) return {};
        node = property->node.get();
    }
    QStringList result;
    for (const auto& property : node->properties) result.append(property.name);
    return result;
}

QByteArray JsoncDocument::updateProperty(const QStringList& path, const QJsonValue& value) const {
    if (path.isEmpty()) {
        if (value.isUndefined()) throw std::invalid_argument("Cannot remove a JSON document root.");
        return applyEdits(data_->source,
            {{data_->root->start, data_->root->end - data_->root->start, encode(value)}});
    }
    auto* parent = data_->root.get();
    qsizetype segment = 0;
    while (segment + 1 < path.size()) {
        if (!parent->value.isObject())
            throw std::runtime_error("Cannot traverse a non-object JSON value.");
        const auto* property = findProperty(*parent, path.at(segment), true);
        if (!property) break;
        parent = property->node.get();
        ++segment;
    }
    if (!parent->value.isObject()) throw std::runtime_error("Expected JSON object for a property edit.");
    const auto* existing = findProperty(*parent, path.at(segment), true);
    if (existing) {
        if (!value.isUndefined())
            return applyEdits(data_->source, {{existing->node->start,
                existing->node->end - existing->node->start, encode(value)}});
        QVector<Edit> edits;
        if (existing->comma >= 0) {
            edits.append({existing->start, existing->comma + 1 - existing->start, {}});
        } else {
            edits.append({existing->start, existing->node->end - existing->start, {}});
            const auto index = existing - parent->properties.constData();
            if (index > 0) edits.append({parent->properties.at(index - 1).comma, 1, {}});
        }
        return applyEdits(data_->source, std::move(edits));
    }
    if (value.isUndefined()) return data_->source;
    QJsonValue nested = value;
    for (auto index = path.size() - 1; index > segment; --index)
        nested = QJsonObject{{path.at(index), nested}};
    const auto propertyText = encode(path.at(segment)) + ": " + encode(nested);
    const QByteArray eol = data_->source.contains("\r\n") ? "\r\n" : "\n";
    if (parent->start == parent->end) {
        const auto prefix = data_->source.isEmpty() || data_->source.endsWith('\n') ? QByteArray{} : eol;
        return applyEdits(data_->source, {{parent->start, 0,
            prefix + '{' + eol + "    " + propertyText + eol + '}' + eol}});
    }
    const auto parentIndent = indentation(data_->source, parent->start);
    auto childIndent = parent->properties.isEmpty() ? QByteArray{}
        : indentation(data_->source, parent->properties.first().start);
    if (childIndent.size() <= parentIndent.size()) childIndent = parentIndent + "    ";
    QVector<Edit> edits;
    bool trailingComma = false;
    qsizetype commaInsertion = -1;
    if (!parent->properties.isEmpty()) {
        const auto& last = parent->properties.last();
        trailingComma = last.comma >= 0;
        if (!trailingComma) commaInsertion = last.node->end;
    }
    auto insertion = parent->end - 1;
    // Consume only the closing delimiter's indentation, never a comment.
    while (insertion > parent->start + 1 &&
           (data_->source.at(insertion - 1) == ' ' || data_->source.at(insertion - 1) == '\t'))
        --insertion;
    const auto prefix = insertion > 0 && data_->source.at(insertion - 1) == '\n'
        ? QByteArray{} : eol;
    if (commaInsertion >= 0 && commaInsertion != insertion)
        edits.append({commaInsertion, 0, ","});
    edits.append({insertion, parent->end - 1 - insertion,
        (commaInsertion == insertion ? QByteArray(",") : QByteArray{}) + prefix + childIndent +
        propertyText + (trailingComma ? "," : "") + eol + parentIndent});
    return applyEdits(data_->source, std::move(edits));
}

} // namespace litecode::core
