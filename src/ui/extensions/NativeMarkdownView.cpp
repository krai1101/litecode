#include "ui/extensions/NativeMarkdownView.h"
#include "ui/Theme.h"
#include "ui/extensions/NativeCodeBlock.h"
#include "ui/extensions/NativeInlineCode.h"
#include "ui/extensions/NativeTranscriptView.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTimer>
#include <QtMath>
namespace litecode::ui {
namespace {
constexpr int codeObject = QTextFormat::UserObject + 27;
constexpr int codeIndex = QTextFormat::UserProperty + 27;
constexpr int codeBlock = QTextFormat::UserProperty + 28;
constexpr int inlineCode = NativeInlineCodeProperty;
// Qt can leave code-block metadata on the following list item. Use the
// actual fence boundaries to distinguish code from that inherited metadata.
QVector<QStringList> fencedLines(const QString& source) {
    static const QRegularExpression opening("^((?:[ \\t]*>[ \\t]?)*[ \\t]*)(`{3,}|~{3,})(.*)$");
    QVector<QStringList> result;
    QString fence, prefix;
    QStringList code;
    for (const auto& line : source.split('\n')) {
        const auto match = opening.match(line);
        if (fence.isEmpty()) {
            if (match.hasMatch()) {
                prefix = match.captured(1);
                fence = match.captured(2);
            }
            continue;
        }
        if (match.hasMatch() && match.captured(2).front() == fence.front() &&
            match.captured(2).size() >= fence.size() && match.captured(3).trimmed().isEmpty()) {
            while (!code.isEmpty() && code.last().isEmpty())
                code.removeLast();
            if (!code.isEmpty())
                result.append(code);
            code.clear();
            fence.clear();
        } else {
            int indent = line.startsWith(prefix) ? prefix.size() : 0;
            while (indent < prefix.size() && indent < line.size() && line[indent].isSpace())
                ++indent;
            code.append(line.mid(indent));
        }
    }
    if (!fence.isEmpty() && !code.isEmpty())
        result.append(code);
    return result;
}
} // namespace
NativeMarkdownView::NativeMarkdownView(bool contentSized, QWidget* parent,
                                       bool allowDocumentResources)
    : QTextBrowser(parent), codeFont_(QFontDatabase::systemFont(QFontDatabase::FixedFont)),
      contentSized_(contentSized), allowDocumentResources_(allowDocumentResources) {
    codeFont_.setPixelSize(QFontInfo(font()).pixelSize());
    setFrameShape(QFrame::NoFrame);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setMinimumWidth(0);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->documentLayout()->registerHandler(codeObject, this);
    heightUpdate_.setSingleShot(true);
    heightUpdate_.setInterval(0);
    connect(&heightUpdate_, &QTimer::timeout, this, [this] {
        updateHeight();
        positionCodeBlocks();
    });
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this] {
                if (!heightUpdate_.isActive())
                    heightUpdate_.start();
            });
    if (contentSized_) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}
QVariant NativeMarkdownView::loadResource(int type, const QUrl& name) {
    return allowDocumentResources_ ? QTextBrowser::loadResource(type, name) : QVariant{};
}
void NativeMarkdownView::setMarkdownText(const QString& text) {
    for (const auto& block : codeBlocks_)
        delete block.widget.data();
    codeBlocks_.clear();
    document()->setDocumentMargin(0);
    document()->setIndentWidth(QFontInfo(font()).pixelSize() * 1.5);
    document()->setMarkdown(text, QTextDocument::MarkdownNoHTML);
    createCodeBlocks(text);
    applyCodeFont();
    // Codex's prose CSS uses weight 600 for strong text, rather than Qt's
    // Markdown default of 700. Keep source emphasis without making it heavier.
    QVector<QPair<int, int>> strong;
    for (auto block = document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().fontWeight() >= QFont::Bold)
                strong.append({fragment.position(), fragment.length()});
        }
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (const auto& range : strong) {
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        QTextCharFormat weight;
        weight.setFontWeight(QFont::DemiBold);
        cursor.mergeCharFormat(weight);
    }
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        auto format = block.blockFormat();
        if (!format.boolProperty(codeBlock) && !format.nonBreakableLines()) {
            format.setTopMargin(format.headingLevel() ? 12 : 0);
            format.setBottomMargin(!block.next().isValid() ? 0 : block.textList() ? 4 : 12);
            if (format.headingLevel())
                format.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
            else
                format.setLineHeight(QFontInfo(font()).pixelSize() * 1.5,
                                     QTextBlockFormat::MinimumHeight);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }
    }
    cursor.endEditBlock();
    positionCodeBlocks();
}
void NativeMarkdownView::setCodeFont(const QFont& font) {
    if (codeFont_ == font)
        return;
    codeFont_ = font;
    for (auto& block : codeBlocks_) {
        block.height = NativeCodeBlock::heightForCode(block.text, codeFont_);
        if (block.widget)
            block.widget->setCodeFont(codeFont_);
    }
    applyCodeFont();
    document()->markContentsDirty(0, document()->characterCount());
    heightUpdate_.start();
}
void NativeMarkdownView::applyCodeFont() {
    // Remember inline code semantically so later editor-font changes still find
    // it, even when the user selects a font without a fixed-pitch flag.
    QVector<QPair<int, int>> ranges;
    for (auto block = document()->begin(); block.isValid(); block = block.next())
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const auto text = fragment.fragment();
            const auto format = text.charFormat();
            if (text.isValid() && format.objectType() == QTextFormat::NoObject &&
                (format.fontFixedPitch() || format.boolProperty(inlineCode)))
                ranges.append({text.position(), text.length()});
        }
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (const auto& range : ranges) {
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        auto inlineFont = codeFont_;
        inlineFont.setPixelSize(qMax(10, qRound(QFontInfo(font()).pixelSize() * .9)));
        format.setFont(inlineFont);
        format.setProperty(inlineCode, true);
        cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
}
void NativeMarkdownView::createCodeBlocks(const QString& source) {
    QTextCursor batch(document());
    batch.beginEditBlock();
    struct CodeRange {
        int start, end, indent;
        QTextBlockFormat format;
        QString text, language;
    };
    QVector<CodeRange> ranges;
    const auto fences = fencedLines(source);
    int fence = 0;
    for (auto block = document()->begin(); block.isValid();) {
        auto format = block.blockFormat();
        if (!format.hasProperty(QTextFormat::BlockCodeLanguage)) {
            block = block.next();
            continue;
        }
        if (format.hasProperty(QTextFormat::BlockCodeFence) &&
            (fence >= fences.size() || block.text() != fences[fence].first())) {
            format.clearProperty(QTextFormat::BlockCodeLanguage);
            format.clearProperty(QTextFormat::BlockCodeFence);
            format.setNonBreakableLines(false);
            QTextCursor(block).setBlockFormat(format);
            block = block.next();
            continue;
        }
        CodeRange range{block.position(),
                        0,
                        block.textList() ? qMax(1, block.textList()->format().indent())
                                         : format.indent(),
                        format,
                        {},
                        format.stringProperty(QTextFormat::BlockCodeLanguage)};
        const bool fenced = format.hasProperty(QTextFormat::BlockCodeFence);
        int remaining = fenced ? fences[fence++].size() : document()->blockCount();
        QStringList lines;
        while (remaining-- > 0 && block.isValid() &&
               block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) {
            lines.append(block.text());
            range.end = block.position() + block.length() - 1;
            if (auto* list = block.textList())
                list->remove(block);
            block = block.next();
        }
        range.text = lines.join('\n');
        ranges.append(range);
        codeBlocks_.append({range.text,
                            range.language,
                            0,
                            NativeCodeBlock::heightForCode(range.text, codeFont_),
                            range.indent,
                            {}});
    }
    for (int index = ranges.size() - 1; index >= 0; --index) {
        const auto& range = ranges[index];
        QTextCursor cursor(document());
        cursor.setPosition(range.start);
        cursor.setPosition(range.end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        auto format = range.format;
        format.clearProperty(QTextFormat::ObjectIndex);
        format.clearProperty(QTextFormat::BlockCodeLanguage);
        format.clearProperty(QTextFormat::BlockCodeFence);
        format.setProperty(codeBlock, true);
        format.setNonBreakableLines(false);
        format.setIndent(range.indent);
        format.setTopMargin(12);
        format.setBottomMargin(12);
        format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        cursor.setBlockFormat(format);
        QTextCharFormat object;
        object.setObjectType(codeObject);
        object.setProperty(codeIndex, index);
        object.setVerticalAlignment(QTextCharFormat::AlignTop);
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), object);
    }
    batch.endEditBlock();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (!block.blockFormat().boolProperty(codeBlock))
            continue;
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const int index = cursor.charFormat().intProperty(codeIndex);
        if (index >= 0 && index < codeBlocks_.size())
            codeBlocks_[index].position = block.position();
    }
}
QSizeF NativeMarkdownView::intrinsicSize(QTextDocument* document, int position,
                                         const QTextFormat& format) {
    const int index = format.intProperty(codeIndex);
    if (index < 0 || index >= codeBlocks_.size())
        return {};
    const auto block = document->findBlock(position).blockFormat();
    const int width = qMax(40, viewport()->width() -
                                   qCeil(block.leftMargin() + block.rightMargin() +
                                         codeBlocks_[index].indent * document->indentWidth() +
                                         2 * document->documentMargin()) -
                                   2);
    return QSizeF(width, codeBlocks_[index].height);
}
void NativeMarkdownView::positionCodeBlocks() {
    if (codeBlocks_.isEmpty())
        return;
    QRect visible = viewport()->rect();
    for (auto* ancestor = viewport()->parentWidget(); ancestor;
         ancestor = ancestor->parentWidget()) {
        if (!ancestor->isVisible()) {
            visible = {};
            break;
        }
        visible =
            visible.intersected(QRect(viewport()->mapFrom(ancestor, QPoint{}), ancestor->size()));
    }
    for (int index = 0; index < codeBlocks_.size(); ++index) {
        auto& block = codeBlocks_[index];
        if (visible.isEmpty()) {
            if (block.widget)
                block.widget->hide();
            continue;
        }
        QTextCursor cursor(document());
        cursor.setPosition(block.position);
        const auto format = document()->findBlock(block.position).blockFormat();
        const int width = qMax(40, viewport()->width() -
                                       qCeil(format.leftMargin() + format.rightMargin() +
                                             block.indent * document()->indentWidth() +
                                             2 * document()->documentMargin()) -
                                       2);
        auto topLeft = cursorRect(cursor).topLeft();
        topLeft.setX(
            qMax(topLeft.x(), qCeil(format.leftMargin() + block.indent * document()->indentWidth() +
                                    document()->documentMargin())));
        const QRect rect(topLeft, QSize(width, block.height));
        if (rect.intersects(visible)) {
            if (!block.widget) {
                block.widget = new NativeCodeBlock(block.text, block.language, viewport());
                block.widget->setCodeFont(codeFont_);
            }
            block.widget->setGeometry(rect);
            block.widget->show();
        } else if (block.widget)
            block.widget->hide();
    }
}
void NativeMarkdownView::paintEvent(QPaintEvent* event) {
    {
        QPainter painter(viewport());
        paintInlineCodeBackgrounds(
            painter, *document(),
            QPointF(-horizontalScrollBar()->value(), -verticalScrollBar()->value()),
            Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).hover);
    }
    QTextBrowser::paintEvent(event);
    positionCodeBlocks();
}
void NativeMarkdownView::wheelEvent(QWheelEvent* event) {
    if (contentSized_ && NativeTranscriptView::forwardWheelAtEdge(this, event))
        return;
    QTextBrowser::wheelEvent(event);
}
void NativeMarkdownView::scrollContentsBy(int dx, int dy) {
    QTextBrowser::scrollContentsBy(dx, dy);
    positionCodeBlocks();
}
QMimeData* NativeMarkdownView::createMimeDataFromSelection() const {
    auto* mime = QTextBrowser::createMimeDataFromSelection();
    const auto selected = textCursor();
    QString text;
    bool containsCode = false;
    for (int position = selected.selectionStart(); position < selected.selectionEnd(); ++position) {
        const auto character = document()->characterAt(position);
        if (character == QChar::ObjectReplacementCharacter) {
            QTextCursor cursor(document());
            cursor.setPosition(position);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            const auto format = cursor.charFormat();
            const int index = format.intProperty(codeIndex);
            if (format.objectType() == codeObject && index >= 0 && index < codeBlocks_.size()) {
                text += codeBlocks_[index].text;
                containsCode = true;
                continue;
            }
        }
        text += character == QChar::ParagraphSeparator ? QChar('\n') : character;
    }
    if (containsCode) {
        delete mime;
        mime = new QMimeData;
        mime->setText(text);
    }
    return mime;
}
void NativeMarkdownView::resizeEvent(QResizeEvent* event) {
    QTextBrowser::resizeEvent(event);
    if (!codeBlocks_.isEmpty())
        document()->markContentsDirty(0, document()->characterCount());
    positionCodeBlocks();
    if (contentSized_ && !heightUpdate_.isActive())
        heightUpdate_.start();
}
void NativeMarkdownView::updateHeight() {
    if (!contentSized_)
        return;
    const int height = qBound(24, qCeil(document()->size().height()), 32768);
    if (minimumHeight() != height)
        setFixedHeight(height);
}
} // namespace litecode::ui
