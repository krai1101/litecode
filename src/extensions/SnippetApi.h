#pragma once
#include "extensions/TextApi.h"
#include <QStringList>
#include <functional>

namespace litecode::extensions {
// Native value equivalents of the pinned stable snippet builders. Numbers are
// signed native integers; callbacks build a nested value and discard their return.
class SnippetString final {
  public:
    using Builder = std::function<void(SnippetString&)>;
    explicit SnippetString(QString value = {});
    QString value;
    SnippetString& appendText(const QString& text);
    SnippetString& appendTabstop(std::optional<qint64> number = {});
    SnippetString& appendPlaceholder(const QString& text, std::optional<qint64> number = {});
    SnippetString& appendPlaceholder(const Builder& builder, std::optional<qint64> number = {});
    SnippetString& appendChoice(const QStringList& values, std::optional<qint64> number = {});
    SnippetString& appendVariable(const QString& name, const QString& defaultValue = {});
    SnippetString& appendVariable(const QString& name, const Builder& builder);

  private:
    qint64 takeNumber(std::optional<qint64> number);
    QString buildNested(const Builder& builder);
    SnippetString& appendPlaceholderValue(QString text, qint64 number);
    SnippetString& appendVariableValue(const QString& name, QString text);
    qint64 tabstop_{1};
};

class SnippetTextEdit final {
  public:
    SnippetTextEdit(Range range, SnippetString snippet)
        : range(std::move(range)), snippet(std::move(snippet)) {}
    static SnippetTextEdit replace(Range range, SnippetString snippet) {
        return SnippetTextEdit(std::move(range), std::move(snippet));
    }
    static SnippetTextEdit insert(Position position, SnippetString snippet) {
        return replace(Range(position, position), std::move(snippet));
    }
    Range range;
    SnippetString snippet;
    std::optional<bool> keepWhitespace;
};
} // namespace litecode::extensions
