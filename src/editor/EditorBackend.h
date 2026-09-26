#pragma once

#include "core/DocumentTypes.h"

#include <QByteArray>
#include <QString>
#include <QWidget>

namespace litecode::editor {

class EditorBackend : public QWidget {
    Q_OBJECT

  public:
    using QWidget::QWidget;
    ~EditorBackend() override = default;

    virtual void setText(const QByteArray& text) = 0;
    virtual void reloadText(const QByteArray& text) = 0;
    virtual void setFilePath(const QString& filePath) = 0;
    virtual void setLineEnding(core::LineEnding lineEnding) = 0;
    virtual void setLargeFileMode(bool enabled) = 0;
    virtual void setReadOnly(bool readOnly) = 0;
    [[nodiscard]] virtual bool isReadOnly() const = 0;
    [[nodiscard]] virtual QByteArray text() const = 0;
    virtual void markSaved() = 0;
    [[nodiscard]] virtual bool isModified() const = 0;
    [[nodiscard]] virtual QString selectedText() const = 0;
    virtual void setEditorFont(const QString& family, int pixelSize, int weight) = 0;
    virtual void resetZoom() = 0;
    virtual void setIndentWidth(int width) = 0;
    virtual void setDarkTheme(bool dark) = 0;
    virtual void goToLine(int line) = 0;
    virtual bool findNext(const QString& needle, bool matchCase, bool wholeWord,
                          bool regularExpression = false) = 0;
    virtual bool findPrevious(const QString& needle, bool matchCase, bool wholeWord,
                              bool regularExpression = false) = 0;
    virtual bool replaceNext(const QString& needle, const QString& replacement, bool matchCase,
                             bool wholeWord, bool regularExpression = false) = 0;
    virtual int replaceAll(const QString& needle, const QString& replacement, bool matchCase,
                           bool wholeWord, bool regularExpression = false) = 0;

  public slots:
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void cut() = 0;
    virtual void copy() = 0;
    virtual void paste() = 0;
    virtual void selectAll() = 0;

  signals:
    void modificationChanged(bool modified);
    void textChanged();
    void textEdited(qint64 position, qint64 removedLength, const QByteArray& insertedText,
                    const core::TextRange& range);
    void cursorPositionChanged(int line, int column);
};

} // namespace litecode::editor
