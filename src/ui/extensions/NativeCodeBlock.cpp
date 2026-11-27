#include "ui/extensions/NativeCodeBlock.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/IconLabelButton.h"
#include "ui/extensions/NativeTranscriptView.h"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>
namespace litecode::ui {
namespace {
class CodeEdit final : public QPlainTextEdit {
  public:
    using QPlainTextEdit::QPlainTextEdit;

  protected:
    void wheelEvent(QWheelEvent* event) override {
        if (!NativeTranscriptView::forwardWheelAtEdge(this, event))
            QPlainTextEdit::wheelEvent(event);
    }
};
} // namespace
NativeCodeBlock::NativeCodeBlock(const QString& code, const QString& language, QWidget* parent)
    : QFrame(parent) {
    setObjectName("nativeCodeBlock");
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);
    auto* header = new QHBoxLayout;
    auto* label = new QLabel(language.isEmpty() || language == "text" || language == "plaintext"
                                 ? tr("Plain text")
                                 : language.left(40),
                             this);
    label->setTextFormat(Qt::PlainText);
    header->addWidget(label, 1);
    auto* copy = new components::IconLabelButton(this);
    copy->setIconLabel("$(copy)");
    copy->setAccessibleName(tr("Copy code"));
    copy->setFixedSize(24, 24);
    header->addWidget(copy);
    layout->addLayout(header);
    editor_ = new CodeEdit(this);
    editor_->setObjectName("nativeCodeText");
    editor_->setAccessibleName(tr("Code block"));
    editor_->setReadOnly(true);
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setPlainText(code);
    auto fallback = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fallback.setPixelSize(QFontInfo(font()).pixelSize());
    setCodeFont(fallback);
    layout->addWidget(editor_);
    TransientScrollBars::install(editor_, true);
    connect(copy, &QAbstractButton::clicked, this,
            [this] { QApplication::clipboard()->setText(editor_->toPlainText()); });
}
void NativeCodeBlock::setCodeFont(const QFont& font) {
    editor_->setFont(font);
    editor_->setFixedHeight(heightForCode(editor_->toPlainText(), font) - 44);
    updateGeometry();
}
QString NativeCodeBlock::code() const { return editor_->toPlainText(); }
int NativeCodeBlock::heightForCode(const QString& code, const QFont& font) {
    return 44 + qBound(48, QFontMetrics(font).lineSpacing() * (code.count('\n') + 1) + 16, 320);
}
QSize NativeCodeBlock::sizeHint() const { return {160, editor_->height() + 44}; }
} // namespace litecode::ui
