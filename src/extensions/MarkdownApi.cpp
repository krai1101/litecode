#include "extensions/MarkdownApi.h"
#include <QRegularExpression>

namespace litecode::extensions {
MarkdownString& MarkdownString::appendText(const QString& text) {
    QString source = text;
    if (supportThemeIcons.value_or(false)) {
        const QRegularExpression icons(QStringLiteral(R"((\\)?\$\([A-Za-z0-9-]+(?:~[A-Za-z]+)?\))"));
        auto matches = icons.globalMatch(source);
        QString escaped;
        qsizetype offset = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            escaped += source.mid(offset, match.capturedStart() - offset);
            if (match.captured(1).isEmpty()) escaped += QLatin1Char('\\');
            escaped += match.captured();
            offset = match.capturedEnd();
        }
        escaped += source.mid(offset);
        source = std::move(escaped);
    }
    QString escaped;
    const auto tokens = QStringLiteral("\\`*_{}[]()#+!~");
    bool leading = true;
    for (const QChar character : source) {
        if (character == QLatin1Char(' ') || character == QLatin1Char('\t')) {
            escaped += QStringLiteral("&nbsp;");
            continue;
        }
        if (tokens.contains(character) || character == QLatin1Char('>') || (leading && character == QLatin1Char('-')))
            escaped += QLatin1Char('\\');
        escaped += character;
        if (character == QLatin1Char('\n')) escaped += QLatin1Char('\n');
        leading = character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
            character == QChar(0x2028) || character == QChar(0x2029);
    }
    value += escaped;
    return *this;
}
MarkdownString& MarkdownString::appendMarkdown(const QString& text) { value += text; return *this; }
MarkdownString& MarkdownString::appendCodeblock(const QString& text, const QString& language) {
    qsizetype longest = 0, run = 0;
    bool leading = true;
    for (const QChar character : text) {
        if (leading && character == QLatin1Char('`')) { longest = qMax(longest, ++run); continue; }
        leading = character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
            character == QChar(0x2028) || character == QChar(0x2029);
        run = 0;
    }
    const QString fence(qMax<qsizetype>(3, longest + 1), QLatin1Char('`'));
    value += QLatin1Char('\n') + fence + language + QLatin1Char('\n') + text + QLatin1Char('\n') + fence + QLatin1Char('\n');
    return *this;
}
} // namespace litecode::extensions
