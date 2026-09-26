#pragma once

#include <QColor>

namespace litecode::editor {

enum class SyntaxRole {
    Foreground,
    Comment,
    Keyword,
    Declaration,
    Number,
    String,
    Escape,
    Preprocessor,
    Type,
    Parameter,
    Variable,
    EnumMember,
    Function,
    Macro,
    MarkupHeading,
    MarkupBold,
    MarkupItalic,
    MarkupQuote,
    MarkupList
};

// Lexilla identifies token classes but intentionally does not prescribe colors.
// This palette is the single mapping from those classes to LiteCode's visual language.
struct SyntaxPalette final {
    QColor foreground;
    QColor background;
    QColor lineNumber;
    QColor caret;
    QColor currentLine;
    QColor indentGuide;
    QColor selection;
    QColor inactiveSelection;
    QColor findMatch;
    QColor findMatchHighlight;
    QColor comment;
    QColor keyword;
    QColor declaration;
    QColor number;
    QColor string;
    QColor escape;
    QColor preprocessor;
    QColor type;
    QColor parameter;
    QColor variable;
    QColor enumMember;
    QColor function;
    QColor macro;
    QColor markupHeading;
    QColor markupBold;
    QColor markupItalic;
    QColor markupQuote;
    QColor markupList;
};

class SyntaxTheme final {
  public:
    [[nodiscard]] static SyntaxPalette palette(bool dark);
    [[nodiscard]] static QColor color(SyntaxRole role, bool dark);
};

} // namespace litecode::editor
