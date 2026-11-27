#pragma once
#include "editor/DiffModel.h"
#include "workspace/GitService.h"
#include <QPointer>
#include <QWidget>
class QLabel;
class QScrollBar;
class QTimer;
class QToolButton;
class QLineEdit;
class QSplitter;
namespace litecode::editor {
class ScintillaEditor;
}
namespace litecode::ui {
class DiffEditorWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit DiffEditorWidget(const workspace::GitComparison& comparison, QWidget* parent = nullptr,
                              bool openFileAvailable = true);
    ~DiffEditorWidget() override;
    void bindWorkingEditor(editor::ScintillaEditor* model);
    void setEditorFont(const QString& family, int size, int weight);
    void setDarkTheme(bool dark);
    void setInlineMode(bool enabled);
    bool inlineMode() const { return inlineMode_; }
    void navigate(int direction);
    void revertCurrentChange();
    void showFind(bool replace = false);
    void undo();
    void redo();
    QString selectedText() const;
    QString workingPath() const;
    editor::ScintillaEditor* workingEditor() const;
    editor::ScintillaEditor* activeEditor() const;
    QVector<editor::DiffHunk> hunks() const { return hunks_; }
  signals:
    void openFileRequested(const QString& path);
    void saveRequested();
    void modificationChanged(bool modified);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void recompute();
    void decorate();
    void decorateInline();
    void scrollTo(int row);
    void finishUndoGroup();
    workspace::GitComparison comparison_;
    editor::ScintillaEditor *original_{}, *modified_{};
    editor::ScintillaEditor* inline_{};
    QSplitter* splitter_{};
    QVector<int> inlineChangeRows_;
    bool inlineMode_{};
    QPointer<editor::ScintillaEditor> model_;
    QPointer<editor::ScintillaEditor> active_;
    QWidget *originalGap_{}, *modifiedGap_{};
    QLabel *counter_{}, *originalLabel_{}, *modifiedLabel_{};
    QScrollBar* scroll_{};
    QTimer* updateTimer_{};
    QToolButton* revert_{};
    QWidget* findBar_{};
    QLineEdit *findInput_{}, *replaceInput_{};
    QVector<editor::DiffHunk> hunks_;
    QVector<int> changeRows_;
    QByteArray comparedModified_;
    int originalTop_{}, modifiedTop_{}, totalRows_{}, currentChange_{-1};
    bool synchronizing_{}, mirroring_{}, dark_{};
    bool undoGroup_{};
    quint64 generation_{};
};
} // namespace litecode::ui
