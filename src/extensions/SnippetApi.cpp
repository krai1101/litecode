// Native adaptation of VS Code snippetString.ts, Copyright Microsoft Corporation.
// MIT license; see licenses/VSCODE-MIT.txt. Pinned source is recorded in the API audit.
#include "extensions/SnippetApi.h"
#include <QStringView>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
namespace {
QString escape(const QString& text, QStringView characters) {
    QString result;
    result.reserve(text.size());
    for (const QChar character : text) {
        if (characters.contains(character))
            result += QLatin1Char('\\');
        result += character;
    }
    return result;
}
} // namespace

SnippetString::SnippetString(QString initial) : value(std::move(initial)) {}

qint64 SnippetString::takeNumber(std::optional<qint64> number) {
    // Explicit numbers never consume the automatic sequence, including zero.
    if (number)
        return *number;
    if (tabstop_ == std::numeric_limits<qint64>::max())
        throw std::overflow_error("Snippet tabstop exceeds the native integer range.");
    return tabstop_++;
}

QString SnippetString::buildNested(const Builder& builder) {
    SnippetString nested;
    nested.tabstop_ = tabstop_;
    builder(nested);
    tabstop_ = nested.tabstop_;
    return std::move(nested.value);
}

SnippetString& SnippetString::appendText(const QString& text) {
    value += escape(text, u"$}\\");
    return *this;
}

SnippetString& SnippetString::appendTabstop(std::optional<qint64> number) {
    const auto index = takeNumber(number);
    value += QLatin1Char('$');
    value += QString::number(index);
    return *this;
}

SnippetString& SnippetString::appendPlaceholderValue(QString text, qint64 number) {
    value +=
        QStringLiteral("${") + QString::number(number) + QLatin1Char(':') + text + QLatin1Char('}');
    return *this;
}

SnippetString& SnippetString::appendPlaceholder(const QString& text, std::optional<qint64> number) {
    const auto index = takeNumber(number);
    return appendPlaceholderValue(escape(text, u"$}\\"), index);
}

SnippetString& SnippetString::appendPlaceholder(const Builder& builder,
                                                std::optional<qint64> number) {
    const auto index = takeNumber(number);
    return appendPlaceholderValue(buildNested(builder), index);
}

SnippetString& SnippetString::appendChoice(const QStringList& choices,
                                           std::optional<qint64> number) {
    const auto index = takeNumber(number);
    QStringList escaped;
    escaped.reserve(choices.size());
    for (const auto& choice : choices)
        escaped.append(escape(choice, u"|\\,"));
    value += QStringLiteral("${") + QString::number(index) + QLatin1Char('|') +
             escaped.join(QLatin1Char(',')) + QStringLiteral("|}");
    return *this;
}

SnippetString& SnippetString::appendVariableValue(const QString& name, QString text) {
    value += QStringLiteral("${") + name;
    if (!text.isEmpty())
        value += QLatin1Char(':') + text;
    value += QLatin1Char('}');
    return *this;
}

SnippetString& SnippetString::appendVariable(const QString& name, const QString& defaultValue) {
    // Unlike literal text/placeholder builders, upstream leaves backslashes in
    // variable defaults untouched. Choice escaping is also a separate rule.
    return appendVariableValue(name, escape(defaultValue, u"$}"));
}

SnippetString& SnippetString::appendVariable(const QString& name, const Builder& builder) {
    return appendVariableValue(name, buildNested(builder));
}
} // namespace litecode::extensions
