#include "ui/extensions/NativeTextEdit.h"
#include <QMimeData>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
namespace litecode::ui {
NativeTextEdit::NativeTextEdit(const QString& text, bool readOnly, QWidget* parent)
    : QPlainTextEdit(text, parent) {
    setReadOnly(readOnly);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Document signals still fire when provider updates block widget input signals.
    connect(document(), &QTextDocument::contentsChanged, this, [this] { updateHeight(); });
    updateHeight();
}
bool NativeTextEdit::canInsertFromMimeData(const QMimeData* source) const {
    return source && (source->hasImage() || source->hasUrls() ||
                      QPlainTextEdit::canInsertFromMimeData(source));
}
void NativeTextEdit::insertFromMimeData(const QMimeData* source) {
    if (!source || isReadOnly())
        return;
    if (source->hasUrls()) {
        QStringList paths;
        for (const auto& url : source->urls())
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        if (!paths.isEmpty()) {
            emit filesPasted(paths);
            return;
        }
    }
    if (source->hasImage()) {
        const auto image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull() && qint64(image.width()) * image.height() <= 32 * 1024 * 1024) {
            emit imagePasted(image);
            return;
        }
    }
    QPlainTextEdit::insertFromMimeData(source);
}
void NativeTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { updateHeight(); });
}
void NativeTextEdit::setVerticalPadding(int pixels) {
    verticalPadding_ = qMax(0, pixels);
    updateHeight();
}
void NativeTextEdit::updateHeight() {
    int lines = 0;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        lines += qMax(1, block.layout()->lineCount());
        if (lines >= 8)
            break;
    }
    const int rows = isReadOnly() ? 4 : qBound(2, lines, 8);
    const int height = rows * fontMetrics().lineSpacing() + verticalPadding_;
    if (minimumHeight() != height || maximumHeight() != height)
        setFixedHeight(height);
}
} // namespace litecode::ui
