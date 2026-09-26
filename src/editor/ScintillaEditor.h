#pragma once

#include "editor/EditorBackend.h"
#include <QJsonArray>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <optional>

class ScintillaEditBase;

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

class ScintillaEditor final : public EditorBackend {
    Q_OBJECT

  public:
    explicit ScintillaEditor(QWidget* parent = nullptr);

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

  public slots:
    void undo() override;
    void redo() override;
    void cut() override;
    void copy() override;
    void paste() override;
    void selectAll() override;

  signals:
    void contextMenuRequested(const QPoint& position);

  private:
    void configure();
    void configureWrapping(bool wordWrap);
    void configureCppLexer();
    void configureLanguageLexer(const QByteArray& lexerName, const QString& languageId);
    void applyEditorChromeStyles();
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
    bool modified_{false};
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
};

} // namespace litecode::editor
