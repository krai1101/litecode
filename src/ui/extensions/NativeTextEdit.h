#pragma once
#include <QImage>
#include <QPlainTextEdit>
namespace litecode::ui {
// Native multiline control with bounded, content-driven height. No extension
// IDs, language integrations or chat behavior belong in this shared widget.
class NativeTextEdit final : public QPlainTextEdit {
    Q_OBJECT
  public:
    NativeTextEdit(const QString& text, bool readOnly, QWidget* parent);
    void setVerticalPadding(int pixels);

  signals:
    void filesPasted(const QStringList& paths);
    void imagePasted(const QImage& image);

  protected:
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void updateHeight();
    int verticalPadding_{20};
};
} // namespace litecode::ui
