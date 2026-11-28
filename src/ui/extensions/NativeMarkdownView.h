#pragma once
#include <QPointer>
#include <QTextBrowser>
#include <QTextObjectInterface>
#include <QTimer>
namespace litecode::ui {
class NativeCodeBlock;
class NativeMarkdownView final : public QTextBrowser, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
  public:
    NativeMarkdownView(bool contentSized, QWidget* parent, bool allowDocumentResources = true);
    void setMarkdownText(const QString& text);
    void setCodeFont(const QFont& font);
    QSizeF intrinsicSize(QTextDocument* document, int position, const QTextFormat& format) override;
    void drawObject(QPainter*, const QRectF&, QTextDocument*, int, const QTextFormat&) override {}

  protected:
    QVariant loadResource(int type, const QUrl& name) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    QMimeData* createMimeDataFromSelection() const override;

  private:
    void updateHeight();
    void applyCodeFont();
    void positionCodeBlocks();
    void createCodeBlocks(const QString& source);
    struct CodeBlock {
        QString text, language;
        int position{}, height{}, indent{};
        QPointer<NativeCodeBlock> widget;
    };
    QVector<CodeBlock> codeBlocks_;
    QFont codeFont_;
    bool contentSized_{};
    bool allowDocumentResources_{};
    QTimer heightUpdate_;
};
} // namespace litecode::ui
