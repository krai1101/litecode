#include "editor/ScintillaEditor.h"

#include "editor/LanguageStyleCatalog.h"
#include "editor/LanguageSupport.h"
#include "editor/SyntaxTheme.h"

#include <ILexer.h>
#include <Lexilla.h>
#include <SciLexer.h>
#include <ScintillaEditBase.h>
#include <ScintillaMessages.h>
#include <ScintillaStructures.h>
#include <ScintillaTypes.h>

#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>

namespace litecode::editor {
namespace {

unsigned int message(Scintilla::Message value) { return static_cast<unsigned int>(value); }

Scintilla::uptr_t value(auto item) { return static_cast<Scintilla::uptr_t>(item); }

Scintilla::sptr_t colour(const QColor& color) {
    return color.red() | (color.green() << 8) | (color.blue() << 16);
}

QColor blendedOver(const QColor& foreground, const QColor& background) {
    const int alpha = foreground.alpha();
    return QColor((foreground.red() * alpha + background.red() * (255 - alpha)) / 255,
                  (foreground.green() * alpha + background.green() * (255 - alpha)) / 255,
                  (foreground.blue() * alpha + background.blue() * (255 - alpha)) / 255);
}

// VS Code-style $1 capture references use Scintilla's \1 replacement syntax.
QByteArray regexReplacement(const QString& replacement) {
    const QByteArray input = replacement.toUtf8();
    QByteArray output;
    for (qsizetype index = 0; index < input.size(); ++index) {
        if (input.at(index) == '$' && index + 1 < input.size()) {
            const char next = input.at(index + 1);
            if (next >= '0' && next <= '9') {
                output += '\\';
                output += next;
                ++index;
                continue;
            }
            if (next == '$') {
                output += '$';
                ++index;
                continue;
            }
        }
        output += input.at(index);
    }
    return output;
}

QString lexerStyleMetadata(ScintillaEditBase* control, Scintilla::Message metadataMessage,
                           int style) {
    const auto length = control->send(message(metadataMessage), value(style));
    if (length <= 0)
        return {};
    QByteArray text(static_cast<qsizetype>(length) + 1, '\0');
    control->send(message(metadataMessage), value(style),
                  reinterpret_cast<Scintilla::sptr_t>(text.data()));
    text.resize(static_cast<qsizetype>(length));
    return QString::fromUtf8(text);
}

QByteArray lineText(ScintillaEditBase* control, int line) {
    const auto start = control->send(message(Scintilla::Message::PositionFromLine), value(line));
    const auto end = control->send(message(Scintilla::Message::GetLineEndPosition), value(line));
    if (start < 0 || end < start || end - start > 1024 * 1024)
        return {};
    QByteArray text(static_cast<qsizetype>(end - start) + 1, '\0');
    Scintilla::TextRangeFull range{{start, end}, text.data()};
    control->send(message(Scintilla::Message::GetTextRangeFull), 0,
                  reinterpret_cast<Scintilla::sptr_t>(&range));
    text.chop(1);
    return text;
}

core::TextPosition textPosition(ScintillaEditBase* control, Scintilla::Position position) {
    const auto line = control->send(message(Scintilla::Message::LineFromPosition), value(position));
    const auto lineStart =
        control->send(message(Scintilla::Message::PositionFromLine), value(line));
    if (line < 0 || lineStart < 0 || position < lineStart)
        return {};

    QByteArray prefix(static_cast<qsizetype>(position - lineStart) + 1, '\0');
    Scintilla::TextRangeFull range{{lineStart, position}, prefix.data()};
    control->send(message(Scintilla::Message::GetTextRangeFull), 0,
                  reinterpret_cast<Scintilla::sptr_t>(&range));
    prefix.chop(1);
    return {static_cast<int>(line), static_cast<int>(QString::fromUtf8(prefix).size())};
}

core::TextPosition removedEndPosition(core::TextPosition start, const QByteArray& removedText) {
    int lineBreaks = 0;
    qsizetype lastLineStart = 0;
    for (qsizetype index = 0; index < removedText.size(); ++index) {
        if (removedText.at(index) == '\n') {
            ++lineBreaks;
            lastLineStart = index + 1;
        }
    }
    if (lineBreaks == 0)
        return {start.line,
                start.utf16Column + static_cast<int>(QString::fromUtf8(removedText).size())};
    const auto tail = QString::fromUtf8(removedText.constData() + lastLineStart,
                                        removedText.size() - lastLineStart);
    return {start.line + lineBreaks, static_cast<int>(tail.size())};
}

void configureLexerProperties(Scintilla::ILexer5* lexer, const QByteArray& lexerName,
                              const QString& languageId) {
    if (!lexer)
        return;

    if (lexerName == QByteArrayLiteral("cpp"))
        lexer->PropertySet("lexer.cpp.escape.sequence", "1");
    else if (lexerName == QByteArrayLiteral("json")) {
        lexer->PropertySet("lexer.json.escape.sequence", "1");
        if (languageId == QStringLiteral("jsonc"))
            lexer->PropertySet("lexer.json.allow.comments", "1");
    } else if (lexerName == QByteArrayLiteral("css")) {
        if (languageId == QStringLiteral("scss"))
            lexer->PropertySet("lexer.css.scss.language", "1");
        else if (languageId == QStringLiteral("less"))
            lexer->PropertySet("lexer.css.less.language", "1");
    } else if (lexerName == QByteArrayLiteral("r")) {
        lexer->PropertySet("lexer.r.escape.sequence", "1");
    }

    // Lexilla's C-family lexer normally colors an entire preprocessor line as
    // one token. Modern editor themes scope only the directive, leaving the
    // include operand available to the ordinary string/header style.
    static const QStringList preprocessorLanguages{
        QStringLiteral("c"),           QStringLiteral("cpp"),           QStringLiteral("cuda-cpp"),
        QStringLiteral("objective-c"), QStringLiteral("objective-cpp"), QStringLiteral("glsl"),
        QStringLiteral("hlsl"),        QStringLiteral("shaderlab"),
    };
    if (lexerName == QByteArrayLiteral("cpp") && preprocessorLanguages.contains(languageId))
        lexer->PropertySet("styling.within.preprocessor", "1");

    // These languages share Lexilla's C-family scanner but do not have C/C++
    // preprocessor semantics. Keeping this explicit prevents '#' and template
    // syntax from inheriting accidental C++ behavior.
    if (languageId.startsWith(QStringLiteral("javascript")) ||
        languageId.startsWith(QStringLiteral("typescript"))) {
        lexer->PropertySet("lexer.cpp.enable.preprocessor", "0");
        lexer->PropertySet("lexer.cpp.allow.hashes", "1");
        lexer->PropertySet("lexer.cpp.backquoted.strings", "2");
    } else if (languageId == QStringLiteral("go")) {
        lexer->PropertySet("lexer.cpp.enable.preprocessor", "0");
        lexer->PropertySet("lexer.cpp.backquoted.strings", "1");
    } else if (languageId == QStringLiteral("java") || languageId == QStringLiteral("kotlin") ||
               languageId == QStringLiteral("scala") || languageId == QStringLiteral("swift") ||
               languageId == QStringLiteral("groovy")) {
        lexer->PropertySet("lexer.cpp.enable.preprocessor", "0");
        if (languageId == QStringLiteral("kotlin") || languageId == QStringLiteral("scala") ||
            languageId == QStringLiteral("swift")) {
            lexer->PropertySet("lexer.cpp.triplequoted.strings", "1");
        }
    } else if (languageId == QStringLiteral("pascal")) {
        lexer->PropertySet("lexer.pascal.smart.highlighting", "1");
    } else if (languageId == QStringLiteral("nim")) {
        lexer->PropertySet("lexer.nim.raw.strings.highlight.ident", "1");
    }
}

QVector<SemanticTokenSpan> decodeSemanticTokens(const QStringList& tokenTypes,
                                                const QJsonArray& tokenData) {
    constexpr qsizetype maximumTokens = 20000;
    QVector<SemanticTokenSpan> spans;
    spans.reserve(qMin(maximumTokens, tokenData.size() / 5));
    int line = 0;
    int column = 0;
    const qsizetype dataLimit = qMin(tokenData.size(), maximumTokens * 5);
    for (qsizetype offset = 0; offset + 4 < dataLimit; offset += 5) {
        const int deltaLine = tokenData.at(offset).toInt();
        const int deltaColumn = tokenData.at(offset + 1).toInt();
        const int tokenLength = tokenData.at(offset + 2).toInt();
        const int typeIndex = tokenData.at(offset + 3).toInt();
        line += deltaLine;
        column = deltaLine == 0 ? column + deltaColumn : deltaColumn;
        if (line < 0 || column < 0 || tokenLength <= 0 || typeIndex < 0 ||
            typeIndex >= tokenTypes.size()) {
            continue;
        }

        const QString& type = tokenTypes.at(typeIndex);
        int indicator = -1;
        if (type == QStringLiteral("namespace"))
            indicator = 16;
        else if (type == QStringLiteral("type") || type == QStringLiteral("class") ||
                 type == QStringLiteral("enum") || type == QStringLiteral("interface") ||
                 type == QStringLiteral("struct") || type == QStringLiteral("typeParameter"))
            indicator = 17;
        else if (type == QStringLiteral("parameter"))
            indicator = 18;
        else if (type == QStringLiteral("variable"))
            indicator = 19;
        else if (type == QStringLiteral("property"))
            indicator = 20;
        else if (type == QStringLiteral("enumMember"))
            indicator = 21;
        else if (type == QStringLiteral("function") || type == QStringLiteral("method"))
            indicator = 22;
        else if (type == QStringLiteral("macro") || type == QStringLiteral("decorator"))
            indicator = 23;
        if (indicator >= 0)
            spans.push_back({line, column, tokenLength, indicator});
    }
    return spans;
}

} // namespace

ScintillaEditor::ScintillaEditor(QWidget* parent) : EditorBackend(parent) {
    setObjectName(QStringLiteral("scintillaEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    control_ = new ScintillaEditBase(this);
    control_->setObjectName(QStringLiteral("scintillaControl"));
    control_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(control_);
    setFocusProxy(control_);

    connect(control_, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
        if (modified_ == dirty) {
            return;
        }
        modified_ = dirty;
        emit modificationChanged(modified_);
    });
    connect(control_, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) {
        updateBraceHighlight();
        const auto position = control_->send(message(Scintilla::Message::GetCurrentPos));
        const auto line =
            control_->send(message(Scintilla::Message::LineFromPosition), value(position));
        const auto column = control_->send(message(Scintilla::Message::GetColumn), value(position));
        emit cursorPositionChanged(static_cast<int>(line) + 1, static_cast<int>(column) + 1);
    });
    connect(control_, &ScintillaEditBase::notifyChange, this, [this] {
        ++contentRevision_;
        emit textChanged();
    });
    connect(
        control_, &ScintillaEditBase::modified, this,
        [this](Scintilla::ModificationFlags flags, Scintilla::Position position,
               Scintilla::Position length, Scintilla::Position, const QByteArray& changedText,
               Scintilla::Position, Scintilla::FoldLevel, Scintilla::FoldLevel) {
            const auto rawFlags = static_cast<unsigned int>(flags);
            const bool inserted =
                (rawFlags & static_cast<unsigned int>(Scintilla::ModificationFlags::InsertText)) !=
                0;
            const bool deleted =
                (rawFlags & static_cast<unsigned int>(Scintilla::ModificationFlags::DeleteText)) !=
                0;
            if (inserted == deleted)
                return;
            if (findScope_) {
                auto& [scopeStart, scopeEnd] = *findScope_;
                if (inserted) {
                    if (position <= scopeStart) {
                        scopeStart += length;
                        scopeEnd += length;
                    } else if (position < scopeEnd) {
                        scopeEnd += length;
                    }
                } else {
                    const auto mapDeletedPosition = [position, length](qint64 value) {
                        return value <= position ? value
                                                 : std::max<qint64>(position, value - length);
                    };
                    scopeStart = mapDeletedPosition(scopeStart);
                    scopeEnd = mapDeletedPosition(scopeEnd);
                }
            }
            const core::TextPosition start = textPosition(control_, position);
            const core::TextPosition end = deleted ? removedEndPosition(start, changedText) : start;
            emit textEdited(static_cast<qint64>(position),
                            deleted ? static_cast<qint64>(length) : 0,
                            inserted ? changedText : QByteArray{}, {start, end});
        });
    connect(control_, &QWidget::customContextMenuRequested, this,
            &ScintillaEditor::contextMenuRequested);
    connect(control_, &ScintillaEditBase::dwellStart, this, [this](int x, int y) {
        const auto position =
            control_->send(message(Scintilla::Message::PositionFromPointClose), value(x), y);
        if (position < 0) {
            return;
        }
        for (const DiagnosticRange& diagnostic : diagnostics_) {
            const auto start = control_->send(message(Scintilla::Message::FindColumn),
                                              value(diagnostic.startLine), diagnostic.startColumn);
            const auto end = control_->send(message(Scintilla::Message::FindColumn),
                                            value(diagnostic.endLine), diagnostic.endColumn);
            if (position >= start && position <= end && !diagnostic.message.isEmpty()) {
                const QByteArray tip = diagnostic.message.toUtf8();
                control_->sends(message(Scintilla::Message::CallTipShow), value(position),
                                tip.constData());
                return;
            }
        }
    });
    connect(control_, &ScintillaEditBase::dwellEnd, this,
            [this](int, int) { control_->send(message(Scintilla::Message::CallTipCancel)); });
    configure();
}

void ScintillaEditor::setText(const QByteArray& text) {
    const bool restoreReadOnly = readOnly_;
    if (restoreReadOnly)
        control_->send(message(Scintilla::Message::SetReadOnly), false);
    control_->sends(message(Scintilla::Message::SetText), 0, text.constData());
    // Scintilla starts with a 2000px scroll width. Reset it for each loaded document and let
    // its line-width tracker grow it only when the visible content actually needs scrolling.
    control_->send(message(Scintilla::Message::SetScrollWidth), 1);
    // SetText is recorded by Scintilla as an edit. A document loaded from disk must instead
    // become the undo baseline; otherwise repeated Ctrl+Z eventually removes the entire file.
    control_->send(message(Scintilla::Message::EmptyUndoBuffer));
    markSaved();
    if (restoreReadOnly)
        control_->send(message(Scintilla::Message::SetReadOnly), true);
    ++contentRevision_;
}

void ScintillaEditor::reloadText(const QByteArray& text) {
    const bool restoreReadOnly = readOnly_;
    if (restoreReadOnly)
        control_->send(message(Scintilla::Message::SetReadOnly), false);

    const qint64 currentPosition = control_->send(message(Scintilla::Message::GetCurrentPos));
    const qint64 anchor = control_->send(message(Scintilla::Message::GetAnchor));
    const auto firstVisibleLine = control_->send(message(Scintilla::Message::GetFirstVisibleLine));
    const auto horizontalOffset = control_->send(message(Scintilla::Message::GetXOffset));

    // Keep the reload as an undoable model update, as VS Code does, while preserving the view.
    // Initial loads use setText(), which deliberately establishes a fresh undo baseline.
    control_->sends(message(Scintilla::Message::SetText), 0, text.constData());
    control_->send(message(Scintilla::Message::SetScrollWidth), 1);
    markSaved();

    const qint64 length = control_->send(message(Scintilla::Message::GetLength));
    control_->send(message(Scintilla::Message::SetSel), value(qBound(qint64{0}, anchor, length)),
                   value(qBound(qint64{0}, currentPosition, length)));
    control_->send(message(Scintilla::Message::SetFirstVisibleLine), firstVisibleLine);
    control_->send(message(Scintilla::Message::SetXOffset), horizontalOffset);
    if (restoreReadOnly)
        control_->send(message(Scintilla::Message::SetReadOnly), true);
    ++contentRevision_;
}

void ScintillaEditor::setReadOnly(bool readOnly) {
    readOnly_ = readOnly;
    control_->send(message(Scintilla::Message::SetReadOnly), readOnly);
}

void ScintillaEditor::setFilePath(const QString& filePath) {
    filePath_ = QFileInfo(filePath).absoluteFilePath();
    if (largeFileMode_) {
        clearLexer();
        return;
    }
    const LanguageSupport language = languageSupportForFile(filePath);
    configureWrapping(language.wordWrapByDefault);
    if (language.id == QStringLiteral("cpp") || language.id == QStringLiteral("c")) {
        configureCppLexer();
    } else if (!language.lexer.isEmpty()) {
        configureLanguageLexer(language.lexer, language.id);
    } else {
        clearLexer();
    }
}

void ScintillaEditor::setLineEnding(core::LineEnding lineEnding) {
    Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf;
    if (lineEnding == core::LineEnding::CrLf)
        mode = Scintilla::EndOfLine::CrLf;
    else if (lineEnding == core::LineEnding::Cr)
        mode = Scintilla::EndOfLine::Cr;
    control_->send(message(Scintilla::Message::SetEOLMode), value(mode));
}

void ScintillaEditor::setLargeFileMode(bool enabled) {
    if (largeFileMode_ == enabled)
        return;
    largeFileMode_ = enabled;
    if (largeFileMode_) {
        diagnostics_.clear();
        clearSemanticIndicators();
    }
    setFilePath(filePath_);
}

void ScintillaEditor::configureWrapping(bool wordWrap) {
    control_->send(message(Scintilla::Message::SetWrapMode),
                   value(wordWrap ? Scintilla::Wrap::Word : Scintilla::Wrap::None));
    control_->send(message(Scintilla::Message::SetHScrollBar), !wordWrap);
}

QByteArray ScintillaEditor::text() const {
    const auto length = control_->send(message(Scintilla::Message::GetTextLength));
    QByteArray contents(static_cast<qsizetype>(length) + 1, '\0');
    control_->send(message(Scintilla::Message::GetText), value(contents.size()),
                   reinterpret_cast<Scintilla::sptr_t>(contents.data()));
    contents.resize(static_cast<qsizetype>(length));
    return contents;
}

void ScintillaEditor::markSaved() { sendCommand(message(Scintilla::Message::SetSavePoint)); }

bool ScintillaEditor::isModified() const { return modified_; }

void ScintillaEditor::goToLine(int line) {
    control_->send(message(Scintilla::Message::GotoLine), value(std::max(0, line - 1)));
    control_->send(message(Scintilla::Message::ScrollCaret));
    control_->setFocus();
}

void ScintillaEditor::goToMatch(int line, int column, int length) {
    if (line <= 0 || column <= 0 || length <= 0) {
        goToLine(line);
        return;
    }
    const auto lineStart =
        control_->send(message(Scintilla::Message::PositionFromLine), value(line - 1));
    if (lineStart < 0) {
        goToLine(line);
        return;
    }
    const QString content = QString::fromUtf8(lineText(control_, line - 1));
    if (column - 1 > content.size() || length > content.size() - (column - 1)) {
        goToLine(line);
        return;
    }
    const qint64 start = lineStart + content.left(column - 1).toUtf8().size();
    const qint64 end = start + content.mid(column - 1, length).toUtf8().size();
    selectByteRange(start, end);
    control_->setFocus();
}

void ScintillaEditor::selectByteRange(qint64 start, qint64 end) {
    const qint64 length = control_->send(message(Scintilla::Message::GetLength));
    control_->send(message(Scintilla::Message::SetSel), value(qBound<qint64>(0, start, length)),
                   value(qBound<qint64>(0, end, length)));
    control_->send(message(Scintilla::Message::ScrollCaret));
}

QPair<qint64, qint64> ScintillaEditor::selectionByteRange() const {
    return {control_->send(message(Scintilla::Message::GetSelectionStart)),
            control_->send(message(Scintilla::Message::GetSelectionEnd))};
}

void ScintillaEditor::setDiagnostics(const QVector<DiagnosticRange>& diagnostics) {
    constexpr int errorIndicator = 8;
    constexpr int warningIndicator = 9;
    constexpr int informationIndicator = 10;
    const auto length = control_->send(message(Scintilla::Message::GetLength));
    for (const int indicator : {errorIndicator, warningIndicator, informationIndicator}) {
        control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(indicator));
        control_->send(message(Scintilla::Message::IndicatorClearRange), 0, length);
    }
    if (largeFileMode_) {
        diagnostics_.clear();
        return;
    }
    diagnostics_ = diagnostics;
    for (const DiagnosticRange& diagnostic : diagnostics) {
        const int indicator = diagnostic.severity == 1   ? errorIndicator
                              : diagnostic.severity == 2 ? warningIndicator
                                                         : informationIndicator;
        const auto start = control_->send(message(Scintilla::Message::FindColumn),
                                          value(diagnostic.startLine), diagnostic.startColumn);
        const auto end = control_->send(message(Scintilla::Message::FindColumn),
                                        value(diagnostic.endLine), diagnostic.endColumn);
        control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(indicator));
        control_->send(message(Scintilla::Message::IndicatorFillRange), value(start),
                       std::max<Scintilla::sptr_t>(1, end - start));
    }
}

void ScintillaEditor::setSemanticTokens(const QStringList& tokenTypes,
                                        const QJsonArray& tokenData) {
    // Lexilla remains the single source of syntax styling. Scintilla indicators are also used
    // by diagnostics and retained semantic indicator state can render as strike-through lines
    // on Windows. Do not layer semantic-token colors over the lexer styles.
    Q_UNUSED(tokenTypes);
    Q_UNUSED(tokenData);
    ++semanticTokenGeneration_;
    semanticTokenTypes_.clear();
    semanticTokenData_ = {};
    semanticTokenSpans_.clear();
    clearSemanticIndicators();
}

QString ScintillaEditor::selectedText() const {
    const auto start = control_->send(message(Scintilla::Message::GetSelectionStart));
    const auto end = control_->send(message(Scintilla::Message::GetSelectionEnd));
    if (end <= start)
        return {};
    QByteArray selected(static_cast<qsizetype>(end - start) + 1, '\0');
    control_->send(message(Scintilla::Message::GetSelText), 0,
                   reinterpret_cast<Scintilla::sptr_t>(selected.data()));
    selected.chop(1);
    return QString::fromUtf8(selected);
}

QString ScintillaEditor::wordAtCaret() const {
    const auto position = control_->send(message(Scintilla::Message::GetCurrentPos));
    const auto start =
        control_->send(message(Scintilla::Message::WordStartPosition), value(position), true);
    const auto end =
        control_->send(message(Scintilla::Message::WordEndPosition), value(position), true);
    if (end <= start || end - start > 256)
        return {};
    QByteArray word(static_cast<qsizetype>(end - start) + 1, '\0');
    Scintilla::TextRangeFull range{{start, end}, word.data()};
    control_->send(message(Scintilla::Message::GetTextRangeFull), 0,
                   reinterpret_cast<Scintilla::sptr_t>(&range));
    word.chop(1);
    return QString::fromUtf8(word);
}

void ScintillaEditor::setEditorFont(const QString& family, int pixelSize, int weight) {
    const QByteArray name = family.toUtf8();
    const auto defaultStyle = value(Scintilla::StylesCommon::Default);
    control_->sends(message(Scintilla::Message::StyleSetFont), defaultStyle, name.constData());
    const qreal pointsPerPixel = 72.0 / qMax(1, logicalDpiY());
    control_->send(message(Scintilla::Message::StyleSetSizeFractional), defaultStyle,
                   qRound(qBound(6, pixelSize, 72) * pointsPerPixel * 100.0));
    const auto fontWeight = weight == QFont::DemiBold ? Scintilla::FontWeight::SemiBold
                            : weight == QFont::Bold   ? Scintilla::FontWeight::Bold
                                                      : Scintilla::FontWeight::Normal;
    control_->send(message(Scintilla::Message::StyleSetWeight), defaultStyle, value(fontWeight));
    // Keep a relaxed default editor line height without changing the
    // font metrics used for columns and indentation.
    control_->send(message(Scintilla::Message::SetExtraAscent), 1);
    control_->send(message(Scintilla::Message::SetExtraDescent), 1);
    control_->send(message(Scintilla::Message::StyleClearAll));
    applyEditorChromeStyles();
    applyLanguageStyles();
    applySemanticTokens();
    control_->send(message(Scintilla::Message::Colourise), 0, -1);
}

void ScintillaEditor::resetZoom() { control_->send(message(Scintilla::Message::SetZoom), 0); }

void ScintillaEditor::setIndentWidth(int width) {
    const int boundedWidth = qBound(1, width, 16);
    control_->send(message(Scintilla::Message::SetTabWidth), boundedWidth);
    control_->send(message(Scintilla::Message::SetIndent), boundedWidth);
}

void ScintillaEditor::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    const auto theme = SyntaxTheme::palette(dark);
    const auto defaultStyle = value(Scintilla::StylesCommon::Default);
    control_->send(message(Scintilla::Message::StyleSetFore), defaultStyle,
                   colour(theme.foreground));
    control_->send(message(Scintilla::Message::StyleSetBack), defaultStyle,
                   colour(theme.background));
    control_->send(message(Scintilla::Message::StyleClearAll));
    applyEditorChromeStyles();
    applyLanguageStyles();
    control_->send(message(Scintilla::Message::Colourise), 0, -1);
    applySemanticTokens();
}

bool ScintillaEditor::findNext(const QString& needle, bool matchCase, bool wholeWord,
                               bool regularExpression) {
    const QByteArray encoded = needle.toUtf8();
    if (encoded.isEmpty()) {
        return false;
    }
    const qint64 lower = findScope_ ? findScope_->first : 0;
    const qint64 upper =
        findScope_ ? findScope_->second : control_->send(message(Scintilla::Message::GetLength));
    const qint64 selectionEnd = qBound(
        lower, static_cast<qint64>(control_->send(message(Scintilla::Message::GetSelectionEnd))),
        upper);
    return findFrom(encoded, selectionEnd, upper, matchCase, wholeWord, regularExpression) ||
           findFrom(encoded, lower, upper, matchCase, wholeWord, regularExpression);
}

bool ScintillaEditor::findPrevious(const QString& needle, bool matchCase, bool wholeWord,
                                   bool regularExpression) {
    const QByteArray encoded = needle.toUtf8();
    if (encoded.isEmpty()) {
        return false;
    }
    const qint64 lower = findScope_ ? findScope_->first : 0;
    const qint64 upper =
        findScope_ ? findScope_->second : control_->send(message(Scintilla::Message::GetLength));
    const qint64 selectionStart = qBound(
        lower, static_cast<qint64>(control_->send(message(Scintilla::Message::GetSelectionStart))),
        upper);
    return findFrom(encoded, selectionStart, lower, matchCase, wholeWord, regularExpression) ||
           findFrom(encoded, upper, lower, matchCase, wholeWord, regularExpression);
}

bool ScintillaEditor::replaceNext(const QString& needle, const QString& replacement, bool matchCase,
                                  bool wholeWord, bool regularExpression) {
    const QByteArray encodedNeedle = needle.toUtf8();
    if (encodedNeedle.isEmpty()) {
        return false;
    }
    const auto start = control_->send(message(Scintilla::Message::GetSelectionStart));
    const auto end = control_->send(message(Scintilla::Message::GetSelectionEnd));
    if (findScope_ && (start < findScope_->first || end > findScope_->second))
        return findNext(needle, matchCase, wholeWord, regularExpression);
    QByteArray selected(static_cast<qsizetype>(end - start) + 1, '\0');
    control_->send(message(Scintilla::Message::GetSelText), 0,
                   reinterpret_cast<Scintilla::sptr_t>(selected.data()));
    selected.chop(1);
    const auto matches =
        matchCase ? selected == encodedNeedle
                  : QString::fromUtf8(selected).compare(needle, Qt::CaseInsensitive) == 0;
    if (regularExpression) {
        if (!findFrom(encodedNeedle, start, end, matchCase, wholeWord, true) ||
            control_->send(message(Scintilla::Message::GetTargetStart)) != start ||
            control_->send(message(Scintilla::Message::GetTargetEnd)) != end)
            return findNext(needle, matchCase, wholeWord, true);
    } else if (!matches ||
               (wholeWord && !findFrom(encodedNeedle, start, end, matchCase, true, false))) {
        return findNext(needle, matchCase, wholeWord, false);
    }

    const QByteArray encodedReplacement =
        regularExpression ? regexReplacement(replacement) : replacement.toUtf8();
    if (regularExpression) {
        control_->sends(message(Scintilla::Message::ReplaceTargetRE),
                        value(encodedReplacement.size()), encodedReplacement.constData());
    } else {
        control_->sends(message(Scintilla::Message::ReplaceSel), 0, encodedReplacement.constData());
    }
    // Match VS Code's Replace One flow by selecting the following match after replacement.
    findNext(needle, matchCase, wholeWord, regularExpression);
    return true;
}

int ScintillaEditor::replaceAll(const QString& needle, const QString& replacement, bool matchCase,
                                bool wholeWord, bool regularExpression) {
    const QByteArray encodedNeedle = needle.toUtf8();
    const QByteArray encodedReplacement =
        regularExpression ? regexReplacement(replacement) : replacement.toUtf8();
    if (encodedNeedle.isEmpty()) {
        return 0;
    }
    int count = 0;
    qint64 position = findScope_ ? findScope_->first : 0;
    control_->send(message(Scintilla::Message::BeginUndoAction));
    while (position <= (findScope_ ? findScope_->second
                                   : control_->send(message(Scintilla::Message::GetLength)))) {
        const auto length = findScope_ ? findScope_->second
                                       : control_->send(message(Scintilla::Message::GetLength));
        if (!findFrom(encodedNeedle, position, length, matchCase, wholeWord, regularExpression)) {
            break;
        }
        const auto targetStart = control_->send(message(Scintilla::Message::GetTargetStart));
        const auto targetEnd = control_->send(message(Scintilla::Message::GetTargetEnd));
        const auto replacedLength =
            control_->sends(message(regularExpression ? Scintilla::Message::ReplaceTargetRE
                                                      : Scintilla::Message::ReplaceTarget),
                            value(encodedReplacement.size()), encodedReplacement.constData());
        position = targetStart + replacedLength;
        if (targetStart == targetEnd) {
            const auto newLength = control_->send(message(Scintilla::Message::GetLength));
            if (position >= newLength) {
                ++count;
                break;
            }
            position = control_->send(message(Scintilla::Message::PositionAfter), position);
        }
        ++count;
    }
    control_->send(message(Scintilla::Message::EndUndoAction));
    return count;
}

FindMatchStatus ScintillaEditor::findMatchStatus(const QString& needle, bool matchCase,
                                                 bool wholeWord, bool regularExpression) const {
    if (cachedFindRevision_ == contentRevision_ && cachedFindNeedle_ == needle &&
        cachedFindMatchCase_ == matchCase && cachedFindWholeWord_ == wholeWord &&
        cachedFindRegularExpression_ == regularExpression) {
        FindMatchStatus result = cachedFindStatus_;
        result.current = 0;
        const auto selectionStart = control_->send(message(Scintilla::Message::GetSelectionStart));
        const auto selectionEnd = control_->send(message(Scintilla::Message::GetSelectionEnd));
        if (selectionStart != selectionEnd) {
            for (qsizetype index = 0; index < cachedFindRanges_.size(); ++index) {
                if (cachedFindRanges_.at(index).first == selectionStart &&
                    cachedFindRanges_.at(index).second == selectionEnd) {
                    result.current = static_cast<int>(index) + 1;
                    break;
                }
            }
        }
        return result;
    }

    const QByteArray encoded = needle.toUtf8();
    if (encoded.isEmpty())
        return {};

    auto flags = Scintilla::FindOption::None;
    if (matchCase)
        flags |= Scintilla::FindOption::MatchCase;
    if (wholeWord)
        flags |= Scintilla::FindOption::WholeWord;
    if (regularExpression)
        flags |= Scintilla::FindOption::RegExp | Scintilla::FindOption::Cxx11RegEx;
    control_->send(message(Scintilla::Message::SetSearchFlags), value(flags));

    const auto selectionStart = control_->send(message(Scintilla::Message::GetSelectionStart));
    const auto selectionEnd = control_->send(message(Scintilla::Message::GetSelectionEnd));
    const auto documentLength =
        findScope_ ? findScope_->second : control_->send(message(Scintilla::Message::GetLength));
    constexpr int maximumMatches = 19'999;
    int current = 0;
    int total = 0;
    qint64 position = findScope_ ? findScope_->first : 0;
    cachedFindRanges_.clear();
    cachedFindRanges_.reserve(maximumMatches);
    while (position <= documentLength && total < maximumMatches) {
        control_->send(message(Scintilla::Message::SetTargetStart), value(position));
        control_->send(message(Scintilla::Message::SetTargetEnd), value(documentLength));
        const auto found = control_->sends(message(Scintilla::Message::SearchInTarget),
                                           value(encoded.size()), encoded.constData());
        if (found < 0)
            break;
        const auto start = control_->send(message(Scintilla::Message::GetTargetStart));
        const auto end = control_->send(message(Scintilla::Message::GetTargetEnd));
        ++total;
        cachedFindRanges_.append({start, end});
        if (start == selectionStart && end == selectionEnd)
            current = total;
        position = end > start ? end : start + 1;
    }
    bool limitHit = false;
    if (total == maximumMatches && position <= documentLength) {
        control_->send(message(Scintilla::Message::SetTargetStart), value(position));
        control_->send(message(Scintilla::Message::SetTargetEnd), value(documentLength));
        limitHit = control_->sends(message(Scintilla::Message::SearchInTarget),
                                   value(encoded.size()), encoded.constData()) >= 0;
    }
    cachedFindNeedle_ = needle;
    cachedFindMatchCase_ = matchCase;
    cachedFindWholeWord_ = wholeWord;
    cachedFindRegularExpression_ = regularExpression;
    cachedFindRevision_ = contentRevision_;
    cachedFindStatus_ = {current, total, limitHit};
    return cachedFindStatus_;
}

void ScintillaEditor::setFindMode(bool enabled) {
    if (findMode_ == enabled)
        return;
    findMode_ = enabled;
    if (!enabled)
        clearFindHighlights();
    applyEditorChromeStyles();
}

void ScintillaEditor::setFindScope(std::optional<QPair<qint64, qint64>> scope) {
    const qint64 documentLength = control_->send(message(Scintilla::Message::GetLength));
    if (scope) {
        scope->first = qBound<qint64>(0, scope->first, documentLength);
        scope->second = qBound<qint64>(scope->first, scope->second, documentLength);
        if (scope->first == scope->second)
            scope.reset();
    }
    if (findScope_ == scope)
        return;
    findScope_ = scope;
    cachedFindRevision_ = 0;
    highlightedFindRevision_ = 0;
    clearFindHighlights();
}

FindMatchStatus ScintillaEditor::updateFindHighlights(const QString& needle, bool matchCase,
                                                      bool wholeWord, bool regularExpression) {
    if (needle.isEmpty()) {
        clearFindHighlights();
        return {};
    }
    const FindMatchStatus status = findMatchStatus(needle, matchCase, wholeWord, regularExpression);
    if (!findMode_)
        return status;

    constexpr int findIndicator = 11;
    const bool queryChanged =
        highlightedFindRevision_ != contentRevision_ || highlightedFindNeedle_ != needle ||
        highlightedFindMatchCase_ != matchCase || highlightedFindWholeWord_ != wholeWord ||
        highlightedFindRegularExpression_ != regularExpression;
    const int currentIndex = status.current - 1;
    control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(findIndicator));
    if (queryChanged) {
        control_->send(message(Scintilla::Message::IndicatorClearRange), 0,
                       control_->send(message(Scintilla::Message::GetLength)));
        for (qsizetype index = 0; index < cachedFindRanges_.size(); ++index) {
            if (index == currentIndex)
                continue;
            const auto [start, end] = cachedFindRanges_.at(index);
            if (end > start)
                control_->send(message(Scintilla::Message::IndicatorFillRange), value(start),
                               end - start);
        }
        highlightedFindNeedle_ = needle;
        highlightedFindRevision_ = contentRevision_;
        highlightedFindMatchCase_ = matchCase;
        highlightedFindWholeWord_ = wholeWord;
        highlightedFindRegularExpression_ = regularExpression;
    } else if (highlightedCurrentIndex_ != currentIndex) {
        if (highlightedCurrentIndex_ >= 0 && highlightedCurrentIndex_ < cachedFindRanges_.size()) {
            const auto [start, end] = cachedFindRanges_.at(highlightedCurrentIndex_);
            if (end > start)
                control_->send(message(Scintilla::Message::IndicatorFillRange), value(start),
                               end - start);
        }
        if (currentIndex >= 0 && currentIndex < cachedFindRanges_.size()) {
            const auto [start, end] = cachedFindRanges_.at(currentIndex);
            if (end > start)
                control_->send(message(Scintilla::Message::IndicatorClearRange), value(start),
                               end - start);
        }
    }
    highlightedCurrentIndex_ = currentIndex;
    return status;
}

void ScintillaEditor::clearFindHighlights() {
    constexpr int findIndicator = 11;
    control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(findIndicator));
    control_->send(message(Scintilla::Message::IndicatorClearRange), 0,
                   control_->send(message(Scintilla::Message::GetLength)));
    highlightedFindNeedle_.clear();
    highlightedFindRevision_ = 0;
    highlightedCurrentIndex_ = -1;
}

void ScintillaEditor::undo() { sendCommand(message(Scintilla::Message::Undo)); }
void ScintillaEditor::redo() { sendCommand(message(Scintilla::Message::Redo)); }
void ScintillaEditor::cut() { sendCommand(message(Scintilla::Message::Cut)); }
void ScintillaEditor::copy() { sendCommand(message(Scintilla::Message::Copy)); }
void ScintillaEditor::paste() { sendCommand(message(Scintilla::Message::Paste)); }
void ScintillaEditor::selectAll() { sendCommand(message(Scintilla::Message::SelectAll)); }

void ScintillaEditor::configure() {
    const auto theme = SyntaxTheme::palette(true);
    control_->send(message(Scintilla::Message::SetCodePage), 65001);
#ifdef Q_OS_WIN
    control_->send(message(Scintilla::Message::SetTechnology),
                   value(Scintilla::Technology::DirectWrite));
#else
    control_->send(message(Scintilla::Message::SetTechnology),
                   value(Scintilla::Technology::Default));
#endif
    configureWrapping(false);
    // Qt exposes this as an "as needed" scrollbar. Reset Scintilla's large default scroll
    // width and track actual line widths so ordinary files do not show an empty bottom bar.
    control_->send(message(Scintilla::Message::SetScrollWidth), 1);
    control_->send(message(Scintilla::Message::SetScrollWidthTracking), true);
    // LiteCode does not use a fixed-width column guide; keep Scintilla's edge renderer off.
    control_->send(message(Scintilla::Message::SetEdgeMode),
                   value(Scintilla::EdgeVisualStyle::None));
    setIndentWidth(4);
    control_->send(message(Scintilla::Message::SetUseTabs), false);
    control_->send(message(Scintilla::Message::SetIndentationGuides),
                   value(Scintilla::IndentView::None));
    control_->send(message(Scintilla::Message::SetMultipleSelection), true);
    control_->send(message(Scintilla::Message::SetAdditionalSelectionTyping), true);
    control_->send(message(Scintilla::Message::SetMultiPaste), value(Scintilla::MultiPaste::Each));

    const auto defaultStyle = value(Scintilla::StylesCommon::Default);
    control_->send(message(Scintilla::Message::StyleSetFore), defaultStyle,
                   colour(theme.foreground));
    control_->send(message(Scintilla::Message::StyleSetBack), defaultStyle,
                   colour(theme.background));
    control_->sends(message(Scintilla::Message::StyleSetFont), defaultStyle, "Consolas");
    control_->send(message(Scintilla::Message::StyleSetSizeFractional), defaultStyle,
                   qRound(14.0 * 72.0 / qMax(1, logicalDpiY()) * 100.0));
    control_->send(message(Scintilla::Message::SetExtraAscent), 1);
    control_->send(message(Scintilla::Message::SetExtraDescent), 1);
    control_->send(message(Scintilla::Message::StyleClearAll));
    applyEditorChromeStyles();
    control_->send(message(Scintilla::Message::SetMarginTypeN), 0,
                   value(Scintilla::MarginType::Number));
    control_->send(message(Scintilla::Message::SetMarginWidthN), 0, 48);
    control_->send(message(Scintilla::Message::SetCaretWidth), 2);
    control_->send(message(Scintilla::Message::SetCaretLineVisible), true);
    control_->send(message(Scintilla::Message::SetMouseDwellTime), 500);
    for (const auto [indicator, color] : {std::pair{8, QColor(QStringLiteral("#f14c4c"))},
                                          std::pair{9, QColor(QStringLiteral("#cca700"))},
                                          std::pair{10, QColor(QStringLiteral("#3794ff"))}}) {
        control_->send(message(Scintilla::Message::IndicSetStyle), value(indicator),
                       value(Scintilla::IndicatorStyle::Squiggle));
        control_->send(message(Scintilla::Message::IndicSetFore), value(indicator), colour(color));
    }
}

void ScintillaEditor::applyEditorChromeStyles() {
    const auto theme = SyntaxTheme::palette(darkTheme_);
    const auto defaultStyle = value(Scintilla::StylesCommon::Default);
    control_->send(message(Scintilla::Message::StyleSetFore), defaultStyle,
                   colour(theme.foreground));
    control_->send(message(Scintilla::Message::StyleSetBack), defaultStyle,
                   colour(theme.background));

    const auto lineStyle = value(Scintilla::StylesCommon::LineNumber);
    control_->send(message(Scintilla::Message::StyleSetFore), lineStyle, colour(theme.lineNumber));
    control_->send(message(Scintilla::Message::StyleSetBack), lineStyle, colour(theme.background));

    const auto indentStyle = value(Scintilla::StylesCommon::IndentGuide);
    control_->send(message(Scintilla::Message::StyleSetFore), indentStyle,
                   colour(theme.indentGuide));
    control_->send(message(Scintilla::Message::StyleSetBack), indentStyle,
                   colour(theme.background));

    const QColor findSelection = blendedOver(theme.findMatch, theme.background);
    const QColor selectionBackground = findMode_ ? findSelection : theme.selection;
    control_->send(message(Scintilla::Message::SetSelBack), true, colour(selectionBackground));
    for (const auto element : {Scintilla::Element::SelectionInactiveBack,
                               Scintilla::Element::SelectionInactiveAdditionalBack}) {
        const auto inactiveBackground = findMode_ ? selectionBackground : theme.inactiveSelection;
        const auto opaqueColor = static_cast<quint32>(colour(inactiveBackground)) | 0xff000000u;
        control_->send(message(Scintilla::Message::SetElementColour), value(element),
                       static_cast<Scintilla::sptr_t>(opaqueColor));
    }
    constexpr int findIndicator = 11;
    control_->send(message(Scintilla::Message::IndicSetStyle), value(findIndicator),
                   value(Scintilla::IndicatorStyle::FullBox));
    control_->send(message(Scintilla::Message::IndicSetFore), value(findIndicator),
                   colour(theme.findMatchHighlight));
    control_->send(message(Scintilla::Message::IndicSetAlpha), value(findIndicator),
                   theme.findMatchHighlight.alpha());
    control_->send(message(Scintilla::Message::IndicSetOutlineAlpha), value(findIndicator), 0);
    control_->send(message(Scintilla::Message::IndicSetUnder), value(findIndicator), true);
    control_->send(message(Scintilla::Message::SetCaretFore), colour(theme.caret));
    control_->send(message(Scintilla::Message::SetCaretLineBack), colour(theme.currentLine));
    control_->send(message(Scintilla::Message::SetCaretLineLayer),
                   value(Scintilla::Layer::UnderText));
    control_->send(message(Scintilla::Message::SetCaretLineBackAlpha),
                   value(Scintilla::Alpha::NoAlpha));
    // VS Code-style active-line cue: a one-pixel outline rather than a filled background.
    control_->send(message(Scintilla::Message::SetCaretLineFrame), 1);
}

void ScintillaEditor::configureCppLexer() {
    cppLexer_ = true;
    languageId_ = QStringLiteral("cpp");
    auto* lexer = CreateLexer("cpp");
    configureLexerProperties(lexer, QByteArrayLiteral("cpp"), languageId_);
    control_->send(message(Scintilla::Message::SetILexer), 0,
                   reinterpret_cast<Scintilla::sptr_t>(lexer));
    for (int index = 0; index < 9; ++index)
        control_->sends(message(Scintilla::Message::SetKeyWords), value(index), "");
    for (const auto& keywords : LanguageStyleCatalog::keywordSets(languageId_))
        control_->sends(message(Scintilla::Message::SetKeyWords), value(keywords.index),
                        keywords.words.constData());

    applyCppStyles();
    control_->send(message(Scintilla::Message::Colourise), 0, -1);
}

void ScintillaEditor::configureLanguageLexer(const QByteArray& lexerName,
                                             const QString& languageId) {
    auto* lexer = CreateLexer(lexerName.constData());
    if (!lexer) {
        clearLexer();
        return;
    }
    cppLexer_ = false;
    languageId_ = languageId;
    configureLexerProperties(lexer, lexerName, languageId_);
    if (languageId_ == QStringLiteral("markdown"))
        lexer->PropertySet("lexer.markdown.header.eolfill", "1");
    control_->send(message(Scintilla::Message::SetILexer), 0,
                   reinterpret_cast<Scintilla::sptr_t>(lexer));
    for (int index = 0; index < 9; ++index)
        control_->sends(message(Scintilla::Message::SetKeyWords), value(index), "");
    for (const auto& keywords : LanguageStyleCatalog::keywordSets(languageId_))
        control_->sends(message(Scintilla::Message::SetKeyWords), value(keywords.index),
                        keywords.words.constData());
    applyLanguageStyles();
    control_->send(message(Scintilla::Message::Colourise), 0, -1);
}

void ScintillaEditor::applyLanguageStyles() {
    if (languageId_.isEmpty())
        return;

    // Style slots survive lexer/theme transitions inside Scintilla. Clear every text decoration
    // first so a lexer state can never inherit an underline from a previously themed slot.
    for (int style = 0; style < 256; ++style) {
        control_->send(message(Scintilla::Message::StyleSetBold), value(style), false);
        control_->send(message(Scintilla::Message::StyleSetItalic), value(style), false);
        control_->send(message(Scintilla::Message::StyleSetUnderline), value(style), false);
    }

    // Lexilla publishes canonical metadata for every style exposed by the
    // active lexer. Theme that metadata first so newly registered languages
    // receive complete highlighting without adding another hand-written
    // switch. The small catalog below only supplies language-specific
    // corrections where a lexer style is more precise than its metadata.
    for (int style = 0; style < 256; ++style) {
        const QString name = lexerStyleMetadata(control_, Scintilla::Message::NameOfStyle, style);
        const QString tags = lexerStyleMetadata(control_, Scintilla::Message::TagsOfStyle, style);
        const QString description =
            lexerStyleMetadata(control_, Scintilla::Message::DescriptionOfStyle, style);
        if (name.isEmpty() && tags.isEmpty() && description.isEmpty())
            continue;
        const SyntaxRole role = LanguageStyleCatalog::roleForStyleMetadata(name, tags, description);
        control_->send(message(Scintilla::Message::StyleSetFore), value(style),
                       colour(SyntaxTheme::color(role, darkTheme_)));
    }
    for (const auto& rule : LanguageStyleCatalog::styleRules(languageId_)) {
        control_->send(message(Scintilla::Message::StyleSetFore), value(rule.style),
                       colour(SyntaxTheme::color(rule.role, darkTheme_)));
        control_->send(message(Scintilla::Message::StyleSetBold), value(rule.style), rule.bold);
        control_->send(message(Scintilla::Message::StyleSetItalic), value(rule.style), rule.italic);
        control_->send(message(Scintilla::Message::StyleSetUnderline), value(rule.style),
                       rule.underline);
    }
}

void ScintillaEditor::applySemanticTokens() {
    const quint64 generation = ++semanticTokenGeneration_;
    semanticTokenSpans_.clear();
    clearSemanticIndicators();
    if (semanticTokenData_.isEmpty() || semanticTokenTypes_.isEmpty())
        return;

    const QStringList tokenTypes = semanticTokenTypes_;
    const QJsonArray tokenData = semanticTokenData_;
    auto* watcher = new QFutureWatcher<QVector<SemanticTokenSpan>>(this);
    connect(watcher, &QFutureWatcher<QVector<SemanticTokenSpan>>::finished, this,
            [this, watcher, generation] {
                const QVector<SemanticTokenSpan> spans = watcher->result();
                watcher->deleteLater();
                if (generation != semanticTokenGeneration_)
                    return;
                semanticTokenSpans_ = spans;
                applySemanticTokenChunk(generation, 0);
            });
    watcher->setFuture(QtConcurrent::run(
        [tokenTypes, tokenData] { return decodeSemanticTokens(tokenTypes, tokenData); }));
}

void ScintillaEditor::clearSemanticIndicators() {
    constexpr int firstIndicator = 16;
    constexpr int lastIndicator = 23;
    const auto documentLength = control_->send(message(Scintilla::Message::GetLength));
    for (int indicator = firstIndicator; indicator <= lastIndicator; ++indicator) {
        control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(indicator));
        control_->send(message(Scintilla::Message::IndicatorClearRange), 0, documentLength);
        control_->send(message(Scintilla::Message::IndicSetStyle), value(indicator),
                       value(Scintilla::IndicatorStyle::TextFore));
    }

    const auto configure = [this](int indicator, const QColor& foreground) {
        control_->send(message(Scintilla::Message::IndicSetFore), value(indicator),
                       colour(foreground));
    };
    const auto theme = SyntaxTheme::palette(darkTheme_);
    configure(16, theme.type);       // namespaces
    configure(17, theme.type);       // types and classes
    configure(18, theme.parameter);  // parameters
    configure(19, theme.variable);   // variables
    configure(20, theme.variable);   // properties
    configure(21, theme.enumMember); // enum members
    configure(22, theme.function);   // functions and methods
    configure(23, theme.macro);      // macros and decorators
}

void ScintillaEditor::applySemanticTokenChunk(quint64 generation, qsizetype offset) {
    if (generation != semanticTokenGeneration_ || offset >= semanticTokenSpans_.size())
        return;

    constexpr qsizetype tokensPerChunk = 256;
    const qsizetype end = qMin(offset + tokensPerChunk, semanticTokenSpans_.size());
    const auto lineCount = control_->send(message(Scintilla::Message::GetLineCount));
    int cachedLine = -1;
    QString cachedLineText;
    for (qsizetype index = offset; index < end; ++index) {
        const SemanticTokenSpan& span = semanticTokenSpans_.at(index);
        if (span.line < 0 || span.line >= lineCount)
            continue;

        if (cachedLine != span.line) {
            cachedLine = span.line;
            cachedLineText = QString::fromUtf8(lineText(control_, span.line));
        }
        if (span.utf16Column > cachedLineText.size())
            continue;
        const int byteColumn = cachedLineText.left(span.utf16Column).toUtf8().size();
        const int byteLength =
            cachedLineText.mid(span.utf16Column, span.utf16Length).toUtf8().size();
        const auto lineStart =
            control_->send(message(Scintilla::Message::PositionFromLine), value(span.line));
        control_->send(message(Scintilla::Message::SetIndicatorCurrent), value(span.indicator));
        control_->send(message(Scintilla::Message::IndicatorFillRange), lineStart + byteColumn,
                       byteLength);
    }
    if (end < semanticTokenSpans_.size()) {
        QTimer::singleShot(0, this,
                           [this, generation, end] { applySemanticTokenChunk(generation, end); });
    }
}

void ScintillaEditor::applyCppStyles() { applyLanguageStyles(); }

void ScintillaEditor::clearLexer() {
    cppLexer_ = false;
    languageId_.clear();
    control_->send(message(Scintilla::Message::SetILexer), 0, 0);
    control_->send(message(Scintilla::Message::StyleClearAll));
}

bool ScintillaEditor::findFrom(const QByteArray& needle, qint64 start, qint64 end, bool matchCase,
                               bool wholeWord, bool regularExpression) {
    auto flags = Scintilla::FindOption::None;
    if (matchCase) {
        flags |= Scintilla::FindOption::MatchCase;
    }
    if (wholeWord) {
        flags |= Scintilla::FindOption::WholeWord;
    }
    if (regularExpression)
        flags |= Scintilla::FindOption::RegExp | Scintilla::FindOption::Cxx11RegEx;
    control_->send(message(Scintilla::Message::SetSearchFlags), value(flags));
    control_->send(message(Scintilla::Message::SetTargetStart), value(start));
    control_->send(message(Scintilla::Message::SetTargetEnd), value(end));
    const auto found = control_->sends(message(Scintilla::Message::SearchInTarget),
                                       value(needle.size()), needle.constData());
    if (found < 0) {
        return false;
    }
    const auto targetStart = control_->send(message(Scintilla::Message::GetTargetStart));
    const auto targetEnd = control_->send(message(Scintilla::Message::GetTargetEnd));
    control_->send(message(Scintilla::Message::SetSel), value(targetStart), targetEnd);
    control_->send(message(Scintilla::Message::ScrollCaret));
    return true;
}

void ScintillaEditor::updateBraceHighlight() {
    if (largeFileMode_)
        return;
    const auto caret = control_->send(message(Scintilla::Message::GetCurrentPos));
    qint64 brace = -1;
    const auto isBrace = [](int character) {
        return character == '(' || character == ')' || character == '[' || character == ']' ||
               character == '{' || character == '}';
    };
    if (isBrace(static_cast<int>(control_->send(message(Scintilla::Message::GetCharAt), caret)))) {
        brace = caret;
    } else if (caret > 0 && isBrace(static_cast<int>(control_->send(
                                message(Scintilla::Message::GetCharAt), caret - 1)))) {
        brace = caret - 1;
    }
    if (brace < 0) {
        control_->send(message(Scintilla::Message::BraceHighlight), value(-1), -1);
        return;
    }
    const auto match = control_->send(message(Scintilla::Message::BraceMatch), value(brace), 0);
    if (match >= 0) {
        control_->send(message(Scintilla::Message::BraceHighlight), value(brace), match);
    } else {
        control_->send(message(Scintilla::Message::BraceBadLight), value(brace));
    }
}

void ScintillaEditor::sendCommand(unsigned int command) const { control_->send(command); }

} // namespace litecode::editor
