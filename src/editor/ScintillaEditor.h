#pragma once

#include "editor/EditorBackend.h"
#include <QJsonArray>
#include <QPair>
#include <QStringList>
#include <QSet>
#include <QVector>
#include <QHash>
#include <limits>
#include <optional>

class ScintillaEditBase;
class QTimer;
class QPainter;
class QSvgRenderer;

namespace litecode::editor {

struct DiagnosticRange {
    int startLine{};
    int startColumn{};
    int endLine{};
    int endColumn{};
    int severity{1};
    QString message;
};

struct SemanticTokenSpan {
    int line{};
    int utf16Column{};
    int utf16Length{};
    int indicator{};
};

struct FindMatchStatus final {
    int current{};
    int total{};
    bool limitHit{};
};

struct EditorByteSelection final { qint64 anchor{}, active{}; };
struct EditorSelectionState final {
    QVector<EditorByteSelection> selections;
    int mainSelection{};
};

class ScintillaEditor final : public EditorBackend {
    Q_OBJECT

  public:
    explicit ScintillaEditor(QWidget* parent = nullptr);
    ~ScintillaEditor() override;

    void setText(const QByteArray& text) override;
    void reloadText(const QByteArray& text) override;
    void setFilePath(const QString& filePath) override;
    void setLineEnding(core::LineEnding lineEnding) override;
    void setLargeFileMode(bool enabled) override;
    [[nodiscard]] QByteArray text() const override;
    void markSaved() override;
    [[nodiscard]] bool isModified() const override;
    [[nodiscard]] QString selectedText() const override;
    [[nodiscard]] QString wordAtCaret() const;
    void setEditorFont(const QString& family, int pixelSize, int weight) override;
    void resetZoom() override;
    void setIndentWidth(int width) override;
    void setDarkTheme(bool dark) override;
    void setReadOnly(bool readOnly) override;
    [[nodiscard]] bool isReadOnly() const override { return readOnly_; }
    void goToLine(int line) override;
    void goToMatch(int line, int column, int length);
    void selectByteRange(qint64 start, qint64 end);
    [[nodiscard]] QPair<qint64, qint64> selectionByteRange() const;
    [[nodiscard]] EditorSelectionState selectionState() const;
    // Workbench transaction recovery; unlike user navigation this must not
    // insert an undo stop. Coordinates are validated before changing the control.
    bool restoreExtensionSelections(const EditorSelectionState& state);
    void setDiagnostics(const QVector<DiagnosticRange>& diagnostics);
    void setSemanticTokens(const QStringList& tokenTypes, const QJsonArray& tokenData);
    bool findNext(const QString& needle, bool matchCase, bool wholeWord,
                  bool regularExpression = false) override;
    bool findPrevious(const QString& needle, bool matchCase, bool wholeWord,
                      bool regularExpression = false) override;
    bool replaceNext(const QString& needle, const QString& replacement, bool matchCase,
                     bool wholeWord, bool regularExpression = false) override;
    int replaceAll(const QString& needle, const QString& replacement, bool matchCase,
                   bool wholeWord, bool regularExpression = false) override;
    [[nodiscard]] FindMatchStatus findMatchStatus(const QString& needle, bool matchCase,
                                                  bool wholeWord,
                                                  bool regularExpression = false) const;
    void setFindMode(bool enabled);
    void setFindScope(std::optional<QPair<qint64, qint64>> scope);
    [[nodiscard]] std::optional<QPair<qint64, qint64>> findScope() const { return findScope_; }
    [[nodiscard]] FindMatchStatus updateFindHighlights(const QString& needle, bool matchCase,
                                                       bool wholeWord,
                                                       bool regularExpression = false);
    void clearFindHighlights();
    [[nodiscard]] bool isLargeFileMode() const noexcept { return largeFileMode_; }
    [[nodiscard]] quint64 contentRevision() const noexcept { return contentRevision_; }
    // Diff view decorations are display-only: padding never changes document bytes.
    void configureDiffView();
    void setDiffDecorations(const QVector<int>& changedLines, const QVector<QPair<int, int>>& gaps,
                            const QVector<QPair<qint64, qint64>>& spans, bool added);
    void setInlineDiffDecorations(const QStringList& labels, const QVector<int>& removed,
                                  const QVector<int>& added,
                                  const QVector<QPair<qint64, qint64>>& removedSpans,
                                  const QVector<QPair<qint64, qint64>>& addedSpans);
    int displayLine() const;
    int diffLineHeight() const;
    int diffLineY(int line) const;
    void setDisplayLine(int line);
    void replaceBytes(qint64 position, qint64 length, const QByteArray& replacement);
    void beginUndoGroup();
    void endUndoGroup();
    bool beginExtensionEdit(bool undoStopBefore);
    void endExtensionEdit(bool undoStopAfter);
    void pushUndoStop();
    int beginLineEndingUndo(core::LineEnding before, core::LineEnding after);
    [[nodiscard]] bool canRecordLineEndingUndo() const { return nextEolUndoToken_ <= std::numeric_limits<int>::max() - 2; }
    void endLineEndingUndo(int token);
    void convertLineEndings(core::LineEnding eol);
    void setHorizontalOffset(int offset);
    void setDebugBreakpoints(const QSet<int>& lines);
    void setBreakpointEnabled(bool enabled);
    void setDebugExecutionLine(int line);

  public slots:
    void undo() override;
    void redo() override;
    void cut() override;
    void copy() override;
    void paste() override;
    void selectAll() override;

  signals:
    void selectionChanged();
    void contextMenuRequested(const QPoint& position);
    void diffScrolled(int displayLine, int horizontalOffset);
    void breakpointToggleRequested(int line);

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void configure();
    void updateGutterWidths();
    void setBreakpointHintLine(int line);
    void paintBreakpointDots(QPainter& painter);
    [[nodiscard]] int gutterLineAt(int y) const;
    [[nodiscard]] int gutterLineAtPoint(const QPoint& point) const;
    bool updateBreakpointHintAt(const QPoint& point);
    void configureWrapping(bool wordWrap);
    void configureCppLexer();
    void configureLanguageLexer(const QByteArray& lexerName, const QString& languageId);
    void applyEditorChromeStyles();
    void applyDebugExecutionMarkerStyle();
    void updateDebugExecutionArrow();
    void applyLanguageStyles();
    void applySemanticTokens();
    void clearSemanticIndicators();
    void applySemanticTokenChunk(quint64 generation, qsizetype offset);
    void applyCppStyles();
    void clearLexer();
    bool findFrom(const QByteArray& needle, qint64 start, qint64 end, bool matchCase,
                  bool wholeWord, bool regularExpression);
    void updateBraceHighlight();
    void sendCommand(unsigned int command) const;

    ScintillaEditBase* control_{};
    QSvgRenderer* breakpointIcon_{};
    QSvgRenderer* activeBreakpointLightIcon_{};
    QSvgRenderer* activeBreakpointDarkIcon_{};
    QTimer* breakpointHoverTimer_{};
    bool modified_{false};
    bool extensionUndoOpen_{};
    bool darkTheme_{true};
    bool cppLexer_{};
    QString languageId_;
    QString filePath_;
    QVector<DiagnosticRange> diagnostics_;
    QStringList semanticTokenTypes_;
    QJsonArray semanticTokenData_;
    QVector<SemanticTokenSpan> semanticTokenSpans_;
    quint64 semanticTokenGeneration_{};
    bool readOnly_{};
    bool diffView_{};
    bool largeFileMode_{};
    mutable QString cachedFindNeedle_;
    mutable bool cachedFindMatchCase_{};
    mutable bool cachedFindWholeWord_{};
    mutable bool cachedFindRegularExpression_{};
    mutable quint64 cachedFindRevision_{};
    mutable FindMatchStatus cachedFindStatus_;
    mutable QVector<QPair<qint64, qint64>> cachedFindRanges_;
    QString highlightedFindNeedle_;
    quint64 highlightedFindRevision_{};
    bool highlightedFindMatchCase_{};
    bool highlightedFindWholeWord_{};
    bool highlightedFindRegularExpression_{};
    int highlightedCurrentIndex_{-1};
    bool findMode_{};
    std::optional<QPair<qint64, qint64>> findScope_;
    quint64 contentRevision_{1};
    int debugExecutionLine_{};
    int breakpointHintLine_{};
    bool gutterPressHandled_{};
    bool breakpointEnabled_{true};
    int gutterDigits_{3};
    struct EolUndoAction { core::LineEnding before, after; bool start; };
    QHash<int, EolUndoAction> eolUndoActions_;
    int nextEolUndoToken_{};
    bool replayingEol_{};
    bool skipReplayCompletionEdit_{};
    std::optional<QPair<core::LineEnding, core::DocumentEditReason>> pendingEolReplay_;
};

} // namespace litecode::editor
