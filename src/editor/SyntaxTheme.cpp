#include "editor/SyntaxTheme.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

int qInitResources_LiteCodeThemes();

namespace litecode::editor {
namespace {

QColor color(const QJsonObject& colors, const char* name, const char* fallback) {
    const QColor loaded(colors.value(QLatin1String(name)).toString());
    return loaded.isValid() ? loaded : QColor(QLatin1String(fallback));
}

QColor cssColor(const QJsonObject& colors, const char* name, const char* fallback) {
    const QString text = colors.value(QLatin1String(name)).toString(QLatin1String(fallback));
    if (text.size() == 9 && text.startsWith(QLatin1Char('#'))) {
        QColor result(text.left(7));
        bool validAlpha = false;
        const int alpha = text.right(2).toInt(&validAlpha, 16);
        if (result.isValid() && validAlpha) {
            result.setAlpha(alpha);
            return result;
        }
    }
    return color(colors, name, fallback);
}

SyntaxPalette loadPalette(bool dark) {
    static const bool initialized = [] {
        ::qInitResources_LiteCodeThemes();
        return true;
    }();
    Q_UNUSED(initialized);
    QFile file(dark ? QStringLiteral(":/litecode/themes/palettes/dark-modern.json")
                    : QStringLiteral(":/litecode/themes/palettes/light-modern.json"));
    QJsonObject colors;
    if (file.open(QIODevice::ReadOnly))
        colors = QJsonDocument::fromJson(file.readAll())
                     .object()
                     .value(QStringLiteral("colors"))
                     .toObject();
    return {
        color(colors, "foreground", dark ? "#cccccc" : "#3b3b3b"),
        color(colors, "background", dark ? "#1f1f1f" : "#ffffff"),
        color(colors, "lineNumber", dark ? "#6e7681" : "#6e7681"),
        color(colors, "caret", dark ? "#f2f4f8" : "#000000"),
        color(colors, "currentLine", dark ? "#2a2d2e" : "#e5e5e5"),
        color(colors, "indentGuide", dark ? "#404040" : "#d3d3d3"),
        color(colors, "selection", dark ? "#264f78" : "#cce4f7"),
        color(colors, "inactiveSelection", dark ? "#3a3d41" : "#e5ebf1"),
        cssColor(colors, "findMatch", dark ? "#27678290" : "#A8AC94"),
        cssColor(colors, "findMatchHighlight", dark ? "#27678280" : "#EA5C0055"),
        color(colors, "comment", dark ? "#6a9955" : "#008000"),
        color(colors, "keyword", dark ? "#569cd6" : "#0000ff"),
        color(colors, "declaration", dark ? "#c586c0" : "#af00db"),
        color(colors, "number", dark ? "#b5cea8" : "#098658"),
        color(colors, "string", dark ? "#ce9178" : "#a31515"),
        color(colors, "escape", dark ? "#d7ba7d" : "#ee0000"),
        color(colors, "preprocessor", dark ? "#c586c0" : "#af00db"),
        color(colors, "type", dark ? "#4ec9b0" : "#267f99"),
        color(colors, "parameter", dark ? "#9cdcfe" : "#001080"),
        color(colors, "variable", dark ? "#9cdcfe" : "#001080"),
        color(colors, "enumMember", dark ? "#4fc1ff" : "#0070c1"),
        color(colors, "function", dark ? "#dcdcaa" : "#795e26"),
        color(colors, "macro", dark ? "#569cd6" : "#0000ff"),
        color(colors, "markupHeading", dark ? "#569cd6" : "#800000"),
        color(colors, "markupBold", dark ? "#569cd6" : "#000080"),
        color(colors, "markupItalic", dark ? "#c586c0" : "#800080"),
        color(colors, "markupQuote", dark ? "#6a9955" : "#008000"),
        color(colors, "markupList", dark ? "#6796e6" : "#0451a5"),
    };
}

} // namespace

SyntaxPalette SyntaxTheme::palette(bool dark) {
    static const SyntaxPalette light = loadPalette(false);
    static const SyntaxPalette darkModern = loadPalette(true);
    return dark ? darkModern : light;
}

QColor SyntaxTheme::color(SyntaxRole role, bool dark) {
    const auto theme = palette(dark);
    switch (role) {
    case SyntaxRole::Foreground:
        return theme.foreground;
    case SyntaxRole::Comment:
        return theme.comment;
    case SyntaxRole::Keyword:
        return theme.keyword;
    case SyntaxRole::Declaration:
        return theme.declaration;
    case SyntaxRole::Number:
        return theme.number;
    case SyntaxRole::String:
        return theme.string;
    case SyntaxRole::Escape:
        return theme.escape;
    case SyntaxRole::Preprocessor:
        return theme.preprocessor;
    case SyntaxRole::Type:
        return theme.type;
    case SyntaxRole::Parameter:
        return theme.parameter;
    case SyntaxRole::Variable:
        return theme.variable;
    case SyntaxRole::EnumMember:
        return theme.enumMember;
    case SyntaxRole::Function:
        return theme.function;
    case SyntaxRole::Macro:
        return theme.macro;
    case SyntaxRole::MarkupHeading:
        return theme.markupHeading;
    case SyntaxRole::MarkupBold:
        return theme.markupBold;
    case SyntaxRole::MarkupItalic:
        return theme.markupItalic;
    case SyntaxRole::MarkupQuote:
        return theme.markupQuote;
    case SyntaxRole::MarkupList:
        return theme.markupList;
    }
    return theme.foreground;
}

} // namespace litecode::editor
