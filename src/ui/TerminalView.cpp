#include "ui/TerminalView.h"

#include "ui/Theme.h"
#include "ui/TransientScrollBars.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace litecode::ui {
namespace {

QColor colorFromRgb(quint32 rgb) {
    return QColor(static_cast<int>((rgb >> 16U) & 0xffU), static_cast<int>((rgb >> 8U) & 0xffU),
                  static_cast<int>(rgb & 0xffU));
}

quint32 colorToRgb(const QColor& color) {
    return (static_cast<quint32>(color.red()) << 16U) |
           (static_cast<quint32>(color.green()) << 8U) | static_cast<quint32>(color.blue());
}

double linearColorChannel(int channel) {
    const double value = static_cast<double>(channel) / 255.0;
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color) {
    return 0.2126 * linearColorChannel(color.red()) + 0.7152 * linearColorChannel(color.green()) +
           0.0722 * linearColorChannel(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second) {
    const double lighter = std::max(relativeLuminance(first), relativeLuminance(second));
    const double darker = std::min(relativeLuminance(first), relativeLuminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

QColor adjustLuminance(const QColor& foreground, const QColor& background, double minimumRatio,
                       bool increase) {
    int red = foreground.red();
    int green = foreground.green();
    int blue = foreground.blue();
    while (contrastRatio(QColor(red, green, blue), background) < minimumRatio &&
           (increase ? red < 255 || green < 255 || blue < 255 : red > 0 || green > 0 || blue > 0)) {
        const auto adjust = [increase](int value) {
            return increase
                       ? std::min(255, value + static_cast<int>(std::ceil((255 - value) * 0.1)))
                       : value - static_cast<int>(std::ceil(value * 0.1));
        };
        red = adjust(red);
        green = adjust(green);
        blue = adjust(blue);
    }
    return QColor(red, green, blue);
}

bool glyphUsesForegroundAsBackground(const QString& text) {
    if (text.isEmpty())
        return false;
    const char32_t codepoint = text.toUcs4().constFirst();
    return (codepoint >= 0xe0a4 && codepoint <= 0xe0d6) ||
           (codepoint >= 0x2500 && codepoint <= 0x259f);
}

QColor withMinimumContrast(const QColor& foreground, const QColor& background,
                           const QString& text) {
    constexpr double minimumRatio = 4.5;
    if (glyphUsesForegroundAsBackground(text) ||
        contrastRatio(foreground, background) >= minimumRatio)
        return foreground;

    const bool increaseFirst = relativeLuminance(foreground) >= relativeLuminance(background);
    const QColor first = adjustLuminance(foreground, background, minimumRatio, increaseFirst);
    if (contrastRatio(first, background) >= minimumRatio)
        return first;
    const QColor second = adjustLuminance(foreground, background, minimumRatio, !increaseFirst);
    return contrastRatio(first, background) >= contrastRatio(second, background) ? first : second;
}

constexpr std::array<quint32, 16> darkAnsiPalette{
    0x000000U, 0xcd3131U, 0x0dbc79U, 0xe5e510U, 0x2472c8U, 0xbc3fbcU, 0x11a8cdU, 0xe5e5e5U,
    0x666666U, 0xf14c4cU, 0x23d18bU, 0xf5f543U, 0x3b8eeaU, 0xd670d6U, 0x29b8dbU, 0xe5e5e5U,
};

constexpr std::array<quint32, 16> lightAnsiPalette{
    0x000000U, 0xcd3131U, 0x107c10U, 0x949800U, 0x0451a5U, 0xbc05bcU, 0x0598bcU, 0x555555U,
    0x666666U, 0xcd3131U, 0x14ce14U, 0xb5ba00U, 0x0451a5U, 0xbc05bcU, 0x0598bcU, 0xa5a5a5U,
};

bool positionLess(const TerminalView::GridPosition& left, const TerminalView::GridPosition& right) {
    return left.row < right.row || (left.row == right.row && left.column < right.column);
}

#ifdef Q_OS_WIN
bool containsFullWindowsClear(const QByteArray& output, int previousBytes, int rows, int columns) {
    const QByteArray home = QByteArrayLiteral("\x1b[H");
    const QByteArray eraseLine = QByteArrayLiteral("\x1b[K");
    const QByteArray newline = QByteArrayLiteral("\r\n");
    int homeOffset = 0;
    while ((homeOffset = output.indexOf(home, homeOffset)) >= 0) {
        int offset = homeOffset + home.size();
        if (output.mid(offset, eraseLine.size()) != eraseLine) {
            homeOffset += home.size();
            continue;
        }
        offset += eraseLine.size();
        if (output.mid(offset, newline.size()) != newline) {
            homeOffset += home.size();
            continue;
        }
        offset += newline.size();

        // ConPTY may insert the freshly drawn prompt before the second erase-line.
        const int secondErase = output.indexOf(eraseLine, offset);
        if (secondErase < 0 || secondErase - offset > columns * 4 + 32) {
            homeOffset += home.size();
            continue;
        }
        offset = secondErase;
        int erasedRows = 1;
        while (output.mid(offset, eraseLine.size()) == eraseLine) {
            ++erasedRows;
            offset += eraseLine.size();
            if (output.mid(offset, newline.size()) != newline)
                break;
            offset += newline.size();
        }
        if (erasedRows >= std::max(2, rows - 1) && offset > previousBytes)
            return true;
        homeOffset += home.size();
    }
    return false;
}
#endif

terminal::TerminalModifiers terminalModifiers(Qt::KeyboardModifiers modifiers) {
    terminal::TerminalModifiers result = terminal::NoModifier;
    if (modifiers.testFlag(Qt::ShiftModifier))
        result |= terminal::ShiftModifier;
    if (modifiers.testFlag(Qt::AltModifier))
        result |= terminal::AltModifier;
    if (modifiers.testFlag(Qt::ControlModifier))
        result |= terminal::ControlModifier;
    return result;
}

} // namespace

TerminalView::TerminalView(QWidget* parent)
    : QAbstractScrollArea(parent), engine_(new terminal::VTermEngine(24, 80, this)) {
    searchRefreshTimer_ = new QTimer(this);
    searchRefreshTimer_->setSingleShot(true);
    searchRefreshTimer_->setInterval(50);
    connect(searchRefreshTimer_, &QTimer::timeout, this, [this] { refreshSearchMatches(); });
    setObjectName(QStringLiteral("terminalOutput"));
    setAccessibleName(tr("Terminal output and input"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    TransientScrollBars::install(this);

#ifdef Q_OS_WIN
    QFont terminalFont(QStringLiteral("Consolas"));
    terminalFont.setPixelSize(14);
#else
    QFont terminalFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    terminalFont.setPixelSize(14);
#endif
    terminalFont.setFixedPitch(true);
    terminalFont.setHintingPreference(QFont::PreferFullHinting);
    terminalFont.setStyleStrategy(QFont::PreferAntialias);
    setFont(terminalFont);
    updateMetrics();

    applyColorPalette(qApp->property("litecodeDarkTheme").toBool());

    connect(engine_, &terminal::VTermEngine::outputGenerated, this, &TerminalView::inputGenerated);
    connect(engine_, &terminal::VTermEngine::titleChanged, this,
            &TerminalView::terminalTitleChanged);
    connect(engine_, &terminal::VTermEngine::updated, this, [this] {
        const bool atBottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();
        updateScrollBar(!atBottom);
        if (!searchQuery_.isEmpty() && !searchRefreshTimer_->isActive())
            searchRefreshTimer_->start();
        viewport()->update();
    });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(),
            qOverload<>(&QWidget::update));
}

void TerminalView::feed(const QByteArray& bytes) {
    // ConPTY translates cmd.exe's CLS into home plus repeated erase-line commands.
    // An explicit ED2 clear and this Windows form both select a new live viewport.
    const int tailSize = recentOutputTail_.size();
    const QByteArray boundary = recentOutputTail_ + bytes.left(10);
    const auto containsNewClear = [&](const QByteArray& sequence) {
        if (bytes.contains(sequence))
            return true;
        const int offset = boundary.lastIndexOf(sequence);
        return offset >= 0 && offset < tailSize && offset + sequence.size() > tailSize;
    };
    const bool clearsViewport = containsNewClear(QByteArrayLiteral("\x1b[2J")) ||
                                containsNewClear(QByteArrayLiteral("\x1b[H\x1b[K")) ||
                                containsNewClear(QByteArrayLiteral("\x1b[1;1H\x1b[K"));
    const QByteArray output = recentOutputTail_ + bytes;
#ifdef Q_OS_WIN
    const bool clearsScrollback =
        containsFullWindowsClear(output, tailSize, engine_->rows(), engine_->columns());
#else
    const bool clearsScrollback = false;
#endif
    recentOutputTail_ =
        output.right(std::max(16, engine_->rows() * 5 + engine_->columns() * 4 + 64));
    engine_->feed(bytes);
    if (clearsScrollback)
        engine_->clearScrollback();
    if (clearsViewport)
        updateScrollBar(false);
}

void TerminalView::clearTerminal() {
    selectionActive_ = false;
    engine_->clear();
    updateScrollBar(false);
}

terminal::VTermEngine* TerminalView::engine() const { return engine_; }

QSize TerminalView::terminalSize() const { return QSize(engine_->columns(), engine_->rows()); }

QString TerminalView::plainText() const {
    QStringList lines;
    lines.reserve(engine_->totalLineCount());
    for (int row = 0; row < engine_->totalLineCount(); ++row)
        lines.append(engine_->lineText(row));
    while (!lines.isEmpty() && lines.constLast().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

QString TerminalView::selectedText() const {
    if (!selectionActive_)
        return {};
    const auto [start, end] = orderedSelection();
    QString result;
    for (int row = start.row; row <= end.row; ++row) {
        const int firstColumn = row == start.row ? start.column : 0;
        const int lastColumn = row == end.row ? end.column : engine_->columns();
        const QString line = engine_->lineText(row, false);
        result += line.mid(firstColumn, std::max(0, lastColumn - firstColumn));
        if (row != end.row)
            result += QLatin1Char('\n');
    }
    return result;
}

bool TerminalView::hasSelection() const { return selectionActive_; }

void TerminalView::setSearch(const QString& query, bool matchCase, bool wholeWord,
                             bool regularExpression) {
    const bool changed = searchQuery_ != query || searchMatchCase_ != matchCase ||
                         searchWholeWord_ != wholeWord ||
                         searchRegularExpression_ != regularExpression;
    searchQuery_ = query;
    searchMatchCase_ = matchCase;
    searchWholeWord_ = wholeWord;
    searchRegularExpression_ = regularExpression;
    searchRefreshTimer_->stop();
    refreshSearchMatches(!changed);
    if (changed && !searchMatches_.empty()) {
        currentSearchMatch_ = 0;
        revealCurrentSearchMatch();
        emit searchResultChanged(1, static_cast<int>(searchMatches_.size()));
    }
}

void TerminalView::findNext(bool backwards) {
    if (searchMatches_.empty()) {
        emit searchResultChanged(0, 0);
        return;
    }
    const int count = static_cast<int>(searchMatches_.size());
    currentSearchMatch_ =
        backwards ? (currentSearchMatch_ - 1 + count) % count : (currentSearchMatch_ + 1) % count;
    revealCurrentSearchMatch();
    emit searchResultChanged(currentSearchMatch_ + 1, count);
    viewport()->update();
}

void TerminalView::clearSearch() {
    searchRefreshTimer_->stop();
    searchQuery_.clear();
    searchMatches_.clear();
    currentSearchMatch_ = -1;
    emit searchResultChanged(0, 0);
    viewport()->update();
}

void TerminalView::setTerminalFont(const QFont& requestedFont) {
    QFont terminalFont(requestedFont);
    terminalFont.setFixedPitch(true);
    terminalFont.setHintingPreference(QFont::PreferFullHinting);
    terminalFont.setStyleStrategy(QFont::PreferAntialias);
    setFont(terminalFont);
    updateTerminalGeometry();
    viewport()->update();
}

void TerminalView::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    const ThemeTokens theme = Theme::tokens(dark);
    const QColor defaultForeground = theme.foreground;
    const QColor defaultBackground = theme.panelSurface;
    const QColor selectionBackground = theme.terminalSelectionBackground;
    const QColor searchBackground = theme.terminalSearchBackground;
    const QColor currentSearchBackground = theme.terminalCurrentSearchBackground;
    painter.fillRect(viewport()->rect(), defaultBackground);

    const int firstLine = verticalScrollBar()->value();
    const int visibleRows = std::max(1, (viewport()->height() - topPadding_) / cellHeight_);
    for (int viewRow = 0; viewRow < visibleRows; ++viewRow) {
        const int absoluteRow = firstLine + viewRow;
        if (absoluteRow >= engine_->totalLineCount())
            break;
        const int y = topPadding_ + viewRow * cellHeight_;
        for (int column = 0; column < engine_->columns(); ++column) {
            const terminal::TerminalCell cell = engine_->cellAt(absoluteRow, column);
            if (cell.width <= 0)
                continue;
            const QRect cellRect(leftPadding_ + column * cellWidth_, y,
                                 cellWidth_ * std::max(1, cell.width), cellHeight_);
            QColor foreground =
                cell.defaultForeground ? defaultForeground : colorFromRgb(cell.foreground);
            QColor background =
                cell.defaultBackground ? defaultBackground : colorFromRgb(cell.background);
            if (cell.reverse)
                std::swap(foreground, background);
            const int searchMatch = searchMatchAt(absoluteRow, column);
            if (searchMatch >= 0)
                background =
                    searchMatch == currentSearchMatch_ ? currentSearchBackground : searchBackground;
            if (isCellSelected(absoluteRow, column))
                background = selectionBackground;
            foreground = withMinimumContrast(foreground, background, cell.text);
            if (background != defaultBackground)
                painter.fillRect(cellRect, background);
            if (!cell.text.isEmpty()) {
                QFont drawFont = font();
                // U+26A0 otherwise falls through to the colored Windows emoji font, producing
                // an oversized icon that overlaps the next terminal cell. VS Code renders the
                // text-presentation warning glyph from a monochrome symbol font.
                if (cell.text.contains(QChar(0x26a0))) {
                    drawFont.setFamily(QStringLiteral("Segoe UI Symbol"));
                    drawFont.setPixelSize(font().pixelSize() > 0 ? font().pixelSize() : 14);
                }
                drawFont.setBold(cell.bold);
                drawFont.setItalic(cell.italic);
                drawFont.setUnderline(cell.underline);
                drawFont.setStrikeOut(cell.strike);
                painter.setFont(drawFont);
                painter.setPen(foreground);
                painter.drawText(cellRect.left(), y + ascent_, cell.text);
            }
        }
    }

    if (engine_->cursorVisible() &&
        verticalScrollBar()->value() == verticalScrollBar()->maximum()) {
        const QPoint cursor = engine_->cursorPosition();
        const int viewRow = cursor.y() - firstLine;
        if (viewRow >= 0 && viewRow < visibleRows) {
            const QRect cursorRect(leftPadding_ + cursor.x() * cellWidth_,
                                   topPadding_ + viewRow * cellHeight_, 2, cellHeight_);
            const QColor cursorColor = defaultForeground;
            if (hasFocus())
                painter.fillRect(cursorRect, cursorColor);
        }
    }
}

void TerminalView::changeEvent(QEvent* event) {
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        applyColorPalette(qApp->property("litecodeDarkTheme").toBool());
}

bool TerminalView::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const QKeyCombination pressed = key->keyCombination();
        bool matchesWorkbenchCommand = false;
        if (QWidget* workbench = window()) {
            for (const QAction* action : workbench->findChildren<QAction*>()) {
                if (!action->isEnabled())
                    continue;
                for (const QKeySequence& sequence : action->shortcuts()) {
                    if (!sequence.isEmpty() && sequence[0] == pressed) {
                        matchesWorkbenchCommand = true;
                        break;
                    }
                }
                if (matchesWorkbenchCommand)
                    break;
            }
        }
        const bool operatingSystemWindowKey =
            key->key() == Qt::Key_F4 && key->modifiers().testFlag(Qt::AltModifier);
        if (matchesWorkbenchCommand || operatingSystemWindowKey) {
            event->ignore();
            return false;
        }
        if (!matchesWorkbenchCommand && !operatingSystemWindowKey) {
            event->accept();
            return true;
        }
    }

    // QWidget normally consumes Tab/Backtab for focus traversal before keyPressEvent().
    // Interactive terminal programs use them for completion and menu navigation.
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            selectionActive_ = false;
            terminal::TerminalModifiers modifiers = modifiersFor(key);
            if (key->key() == Qt::Key_Backtab)
                modifiers |= terminal::ShiftModifier;
            engine_->sendKey(terminal::TerminalKey::Tab, modifiers);
            viewport()->update();
            event->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
}

void TerminalView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateTerminalGeometry();
}

void TerminalView::keyPressEvent(QKeyEvent* event) {
    const bool controlC = event->key() == Qt::Key_C &&
                          event->modifiers().testFlag(Qt::ControlModifier) &&
                          !event->modifiers().testFlag(Qt::ShiftModifier);
    if ((controlC || event->matches(QKeySequence::Copy)) && hasSelection()) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteClipboard();
        return;
    }
    if (event->key() == Qt::Key_C && event->modifiers().testFlag(Qt::ControlModifier) &&
        event->modifiers().testFlag(Qt::ShiftModifier)) {
        copySelection();
        return;
    }
    if (event->key() == Qt::Key_V && event->modifiers().testFlag(Qt::ControlModifier) &&
        event->modifiers().testFlag(Qt::ShiftModifier)) {
        pasteClipboard();
        return;
    }
    if (controlC) {
        selectionActive_ = false;
        engine_->sendText(QStringLiteral("c"), terminal::ControlModifier);
        viewport()->update();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::AltModifier)) {
        QString controlCharacter;
        switch (event->key()) {
        case Qt::Key_Space:
        case Qt::Key_2:
            controlCharacter = QStringLiteral("@");
            break;
        case Qt::Key_BracketLeft:
            controlCharacter = QStringLiteral("[");
            break;
        case Qt::Key_Backslash:
            controlCharacter = QStringLiteral("\\");
            break;
        case Qt::Key_BracketRight:
            controlCharacter = QStringLiteral("]");
            break;
        case Qt::Key_6:
            controlCharacter = QStringLiteral("^");
            break;
        case Qt::Key_Minus:
            controlCharacter = QStringLiteral("_");
            break;
        default:
            break;
        }
        if (!controlCharacter.isEmpty()) {
            engine_->sendText(controlCharacter, terminal::ControlModifier);
            selectionActive_ = false;
            viewport()->update();
            event->accept();
            return;
        }
    }
    const bool plainControlLetter = event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z &&
                                    event->modifiers().testFlag(Qt::ControlModifier) &&
                                    !event->modifiers().testFlag(Qt::AltModifier);
    if (plainControlLetter) {
        const QChar letter(static_cast<char16_t>(u'a' + event->key() - Qt::Key_A));
        engine_->sendText(QString(letter), terminal::ControlModifier);
        selectionActive_ = false;
        viewport()->update();
        event->accept();
        return;
    }

    terminal::TerminalKey key{};
    bool special = true;
    int functionNumber = 0;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        key = terminal::TerminalKey::Enter;
        break;
    case Qt::Key_Tab:
        key = terminal::TerminalKey::Tab;
        break;
    case Qt::Key_Backspace:
        key = terminal::TerminalKey::Backspace;
        break;
    case Qt::Key_Escape:
        key = terminal::TerminalKey::Escape;
        break;
    case Qt::Key_Up:
        key = terminal::TerminalKey::Up;
        break;
    case Qt::Key_Down:
        key = terminal::TerminalKey::Down;
        break;
    case Qt::Key_Left:
        key = terminal::TerminalKey::Left;
        break;
    case Qt::Key_Right:
        key = terminal::TerminalKey::Right;
        break;
    case Qt::Key_Insert:
        key = terminal::TerminalKey::Insert;
        break;
    case Qt::Key_Delete:
        key = terminal::TerminalKey::Delete;
        break;
    case Qt::Key_Home:
        key = terminal::TerminalKey::Home;
        break;
    case Qt::Key_End:
        key = terminal::TerminalKey::End;
        break;
    case Qt::Key_PageUp:
        key = terminal::TerminalKey::PageUp;
        break;
    case Qt::Key_PageDown:
        key = terminal::TerminalKey::PageDown;
        break;
    default:
        if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35) {
            key = terminal::TerminalKey::Function;
            functionNumber = event->key() - Qt::Key_F1 + 1;
        } else {
            special = false;
        }
        break;
    }
    if (special) {
        selectionActive_ = false;
        engine_->sendKey(key, modifiersFor(event), functionNumber);
    } else if (!event->text().isEmpty()) {
        selectionActive_ = false;
        engine_->sendText(event->text(), modifiersFor(event));
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
    viewport()->update();
}

void TerminalView::inputMethodEvent(QInputMethodEvent* event) {
    if (!event->commitString().isEmpty())
        engine_->sendText(event->commitString());
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle) {
        const QPoint cursor = engine_->cursorPosition();
        return QRect(leftPadding_ + cursor.x() * cellWidth_,
                     topPadding_ + (cursor.y() - verticalScrollBar()->value()) * cellHeight_, 2,
                     cellHeight_);
    }
    return QAbstractScrollArea::inputMethodQuery(query);
}

void TerminalView::focusInEvent(QFocusEvent* event) {
    QAbstractScrollArea::focusInEvent(event);
    engine_->sendFocus(true);
    viewport()->update();
}

void TerminalView::focusOutEvent(QFocusEvent* event) {
    engine_->sendFocus(false);
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

void TerminalView::mousePressEvent(QMouseEvent* event) {
    const auto mouseButtonNumber = [](Qt::MouseButton button) {
        return button == Qt::LeftButton     ? 1
               : button == Qt::MiddleButton ? 2
               : button == Qt::RightButton  ? 3
                                            : 0;
    };
    const int terminalButton = mouseButtonNumber(event->button());
    if (terminalButton != 0 && engine_->mouseTrackingEnabled() &&
        !event->modifiers().testFlag(Qt::ShiftModifier) &&
        !(event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ControlModifier) &&
          linkAt(gridPosition(event->position().toPoint())).has_value())) {
        setFocus();
        if (sendMousePosition(event->position().toPoint(), event->modifiers())) {
            engine_->sendMouseButton(terminalButton, true, terminalModifiers(event->modifiers()));
            terminalMouseButton_ = terminalButton;
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton) {
        setFocus();
        const GridPosition position = gridPosition(event->position().toPoint());
        if (event->modifiers().testFlag(Qt::ControlModifier) && linkAt(position).has_value()) {
            linkPressed_ = true;
            event->accept();
            return;
        }
        if (lastDoubleClickTimestamp_ != 0 &&
            event->timestamp() - lastDoubleClickTimestamp_ <=
                static_cast<quint64>(QApplication::doubleClickInterval())) {
            selectLineAt(position);
            lastDoubleClickTimestamp_ = 0;
            event->accept();
            return;
        }
        if (!event->modifiers().testFlag(Qt::ShiftModifier) || !selectionActive_)
            selectionAnchor_ = position;
        selectionCaret_ = position;
        selectionActive_ = false;
        selecting_ = true;
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        (!engine_->mouseTrackingEnabled() || event->modifiers().testFlag(Qt::ShiftModifier))) {
        selectWordAt(gridPosition(event->position().toPoint()));
        selecting_ = false;
        lastDoubleClickTimestamp_ = event->timestamp();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void TerminalView::mouseMoveEvent(QMouseEvent* event) {
    if (terminalMouseButton_ != 0 ||
        (engine_->mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier))) {
        if (sendMousePosition(event->position().toPoint(), event->modifiers())) {
            event->accept();
            return;
        }
    }
    if (selecting_) {
        if (event->position().y() < 0)
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        else if (event->position().y() >= viewport()->height())
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        selectionCaret_ = gridPosition(event->position().toPoint());
        selectionActive_ = selectionCaret_.row != selectionAnchor_.row ||
                           selectionCaret_.column != selectionAnchor_.column;
        viewport()->update();
        event->accept();
        return;
    }
    const bool link = event->modifiers().testFlag(Qt::ControlModifier) &&
                      linkAt(gridPosition(event->position().toPoint())).has_value();
    viewport()->setCursor(link ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && linkPressed_) {
        linkPressed_ = false;
        const auto link = linkAt(gridPosition(event->position().toPoint()));
        if (link)
            emit linkActivated(link->target, link->line, link->column, link->external);
        event->accept();
        return;
    }
    if (terminalMouseButton_ != 0) {
        sendMousePosition(event->position().toPoint(), event->modifiers());
        engine_->sendMouseButton(terminalMouseButton_, false,
                                 terminalModifiers(event->modifiers()));
        terminalMouseButton_ = 0;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && selecting_) {
        selecting_ = false;
        selectionCaret_ = gridPosition(event->position().toPoint());
        selectionActive_ = selectionCaret_.row != selectionAnchor_.row ||
                           selectionCaret_.column != selectionAnchor_.column;
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalView::wheelEvent(QWheelEvent* event) {
    if (engine_->mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const QPoint point = event->position().toPoint();
        if (sendMousePosition(point, event->modifiers())) {
            const int button = event->angleDelta().y() > 0 ? 4 : 5;
            const int steps = std::max(1, std::abs(event->angleDelta().y()) / 120);
            for (int index = 0; index < steps; ++index)
                engine_->sendMouseButton(button, true, terminalModifiers(event->modifiers()));
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(event);
}

void TerminalView::contextMenuEvent(QContextMenuEvent* event) {
    if (event->reason() == QContextMenuEvent::Mouse) {
        pasteClipboard();
        event->accept();
        return;
    }
    event->ignore();
}

void TerminalView::updateMetrics() {
    const QFontMetrics metrics(font());
    // Measure a terminal-cell glyph. Using 'M' makes the grid too wide whenever Qt falls back
    // to a proportional UI font while a style is being repolished.
    cellWidth_ = std::max(1, metrics.horizontalAdvance(QLatin1Char('0')));
    cellHeight_ = std::max(1, metrics.lineSpacing());
    ascent_ = metrics.ascent();
}

void TerminalView::applyColorPalette(bool dark) {
    const ThemeTokens theme = Theme::tokens(dark);
    engine_->setDefaultColors(colorToRgb(theme.foreground), colorToRgb(theme.panelSurface));
    engine_->setAnsiPalette(dark ? darkAnsiPalette : lightAnsiPalette);
}

void TerminalView::updateTerminalGeometry() {
    updateMetrics();
    const int columns = std::max(1, (viewport()->width() - leftPadding_) / cellWidth_);
    const int rows = std::max(1, (viewport()->height() - topPadding_) / cellHeight_);
    const QSize requested(columns, rows);
    if (requested == terminalSize())
        return;
    const bool wasAtBottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    engine_->resize(rows, columns);
    emit terminalSizeChanged(requested);
    updateScrollBar(!wasAtBottom);
}

void TerminalView::updateScrollBar(bool preservePosition) {
    const int oldValue = verticalScrollBar()->value();
    verticalScrollBar()->setPageStep(engine_->rows());
    verticalScrollBar()->setRange(0, engine_->historyLineCount());
    verticalScrollBar()->setValue(preservePosition
                                      ? std::min(oldValue, verticalScrollBar()->maximum())
                                      : verticalScrollBar()->maximum());
}

void TerminalView::copySelection() {
    if (hasSelection())
        QApplication::clipboard()->setText(selectedText());
}

void TerminalView::pasteClipboard() {
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    // Match VS Code's direct terminal paste behavior for this lightweight terminal: bracketed
    // paste support in the backend keeps multi-line input together as one paste operation.
    engine_->sendPaste(text);
}

void TerminalView::selectAllVisible() {
    selectionAnchor_ = {verticalScrollBar()->value(), 0};
    selectionCaret_ = {
        std::min(engine_->totalLineCount() - 1, verticalScrollBar()->value() + engine_->rows() - 1),
        engine_->columns()};
    selectionActive_ = true;
    viewport()->update();
}

void TerminalView::refreshSearchMatches(bool preserveCurrent) {
    GridPosition previousStart{-1, -1};
    if (preserveCurrent && currentSearchMatch_ >= 0 &&
        currentSearchMatch_ < static_cast<int>(searchMatches_.size())) {
        previousStart = searchMatches_.at(static_cast<size_t>(currentSearchMatch_)).start;
    }
    searchMatches_.clear();
    currentSearchMatch_ = -1;
    if (searchQuery_.isEmpty()) {
        emit searchResultChanged(0, 0);
        viewport()->update();
        return;
    }

    QString pattern =
        searchRegularExpression_ ? searchQuery_ : QRegularExpression::escape(searchQuery_);
    if (searchWholeWord_)
        pattern = QStringLiteral("(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])").arg(pattern);
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!searchMatchCase_)
        options |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression expression(pattern, options);
    if (!expression.isValid()) {
        emit searchResultChanged(0, 0);
        viewport()->update();
        return;
    }

    for (int row = 0; row < engine_->totalLineCount(); ++row) {
        const QString line = engine_->lineText(row, false);
        auto iterator = expression.globalMatch(line);
        while (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            if (match.capturedLength() > 0)
                searchMatches_.push_back({{row, static_cast<int>(match.capturedStart())},
                                          {row, static_cast<int>(match.capturedEnd())}});
        }
    }

    if (!searchMatches_.empty()) {
        currentSearchMatch_ = 0;
        if (previousStart.row >= 0) {
            const auto found = std::find_if(searchMatches_.cbegin(), searchMatches_.cend(),
                                            [previousStart](const SearchMatch& match) {
                                                return match.start.row == previousStart.row &&
                                                       match.start.column == previousStart.column;
                                            });
            if (found != searchMatches_.cend())
                currentSearchMatch_ =
                    static_cast<int>(std::distance(searchMatches_.cbegin(), found));
        }
    }
    emit searchResultChanged(currentSearchMatch_ + 1, static_cast<int>(searchMatches_.size()));
    viewport()->update();
}

void TerminalView::revealCurrentSearchMatch() {
    if (currentSearchMatch_ < 0 || currentSearchMatch_ >= static_cast<int>(searchMatches_.size()))
        return;
    const int row = searchMatches_.at(static_cast<size_t>(currentSearchMatch_)).start.row;
    const int first = verticalScrollBar()->value();
    const int last = first + std::max(1, engine_->rows()) - 1;
    if (row < first)
        verticalScrollBar()->setValue(row);
    else if (row > last)
        verticalScrollBar()->setValue(row - engine_->rows() + 1);
}

TerminalView::GridPosition TerminalView::gridPosition(const QPoint& position) const {
    return {std::clamp(verticalScrollBar()->value() + (position.y() - topPadding_) / cellHeight_, 0,
                       std::max(0, engine_->totalLineCount() - 1)),
            std::clamp((position.x() - leftPadding_) / cellWidth_, 0, engine_->columns())};
}

bool TerminalView::isCellSelected(int absoluteRow, int column) const {
    if (!selectionActive_)
        return false;
    const auto [start, end] = orderedSelection();
    const GridPosition cell{absoluteRow, column};
    const GridPosition cellEnd{absoluteRow, column + 1};
    return !positionLess(cellEnd, start) && positionLess(cell, end);
}

int TerminalView::searchMatchAt(int absoluteRow, int column) const {
    for (int index = 0; index < static_cast<int>(searchMatches_.size()); ++index) {
        const SearchMatch& match = searchMatches_.at(static_cast<size_t>(index));
        if (match.start.row == absoluteRow && column >= match.start.column &&
            column < match.end.column)
            return index;
    }
    return -1;
}

std::optional<TerminalView::TerminalLink> TerminalView::linkAt(const GridPosition& position) const {
    if (position.row < 0 || position.row >= engine_->totalLineCount())
        return std::nullopt;
    const QString text = engine_->lineText(position.row, false);
    static const QRegularExpression urlExpression(
        QStringLiteral(R"(([A-Za-z][A-Za-z0-9+.-]*://[^\s<>"']+))"));
    auto urlMatches = urlExpression.globalMatch(text);
    while (urlMatches.hasNext()) {
        const QRegularExpressionMatch match = urlMatches.next();
        if (position.column >= match.capturedStart(1) && position.column < match.capturedEnd(1)) {
            QString target = match.captured(1);
            while (!target.isEmpty() && QStringLiteral(".,;:)]}").contains(target.back()))
                target.chop(1);
            return TerminalLink{target, 0, 0, true};
        }
    }

    static const QRegularExpression fileExpression(
        QStringLiteral(R"(([^\s<>"']+?):(\d+)(?::(\d+))?)"));
    auto fileMatches = fileExpression.globalMatch(text);
    while (fileMatches.hasNext()) {
        const QRegularExpressionMatch match = fileMatches.next();
        if (position.column < match.capturedStart(0) || position.column >= match.capturedEnd(0))
            continue;
        QString target = match.captured(1);
        while (!target.isEmpty() && QStringLiteral("([{'").contains(target.front()))
            target.removeFirst();
        return TerminalLink{target, match.captured(2).toInt(), match.captured(3).toInt(), false};
    }
    return std::nullopt;
}

void TerminalView::selectWordAt(const GridPosition& position) {
    const QString line = engine_->lineText(position.row, false);
    if (line.isEmpty())
        return;
    const int column = std::clamp(position.column, 0, static_cast<int>(line.size()) - 1);
    const auto isSeparator = [](QChar character) {
        return character.isSpace() || QStringLiteral("\"'`()[]{}<>,;|").contains(character);
    };
    int start = column;
    int end = column;
    const bool separator = isSeparator(line.at(column));
    while (start > 0 && isSeparator(line.at(start - 1)) == separator)
        --start;
    while (end < static_cast<int>(line.size()) && isSeparator(line.at(end)) == separator)
        ++end;
    selectionAnchor_ = {position.row, start};
    selectionCaret_ = {position.row, end};
    selectionActive_ = end > start;
    viewport()->update();
}

void TerminalView::selectLineAt(const GridPosition& position) {
    selectionAnchor_ = {position.row, 0};
    selectionCaret_ = {position.row,
                       static_cast<int>(engine_->lineText(position.row, false).size())};
    selectionActive_ = selectionCaret_.column > 0;
    selecting_ = false;
    viewport()->update();
}

bool TerminalView::sendMousePosition(const QPoint& viewportPosition,
                                     Qt::KeyboardModifiers modifiers) {
    const GridPosition position = gridPosition(viewportPosition);
    const int screenRow = position.row - engine_->historyLineCount();
    if (screenRow < 0 || screenRow >= engine_->rows())
        return false;
    engine_->sendMouseMove(screenRow, position.column, terminalModifiers(modifiers));
    return true;
}

std::pair<TerminalView::GridPosition, TerminalView::GridPosition>
TerminalView::orderedSelection() const {
    if (positionLess(selectionCaret_, selectionAnchor_))
        return {selectionCaret_, selectionAnchor_};
    return {selectionAnchor_, selectionCaret_};
}

terminal::TerminalModifiers TerminalView::modifiersFor(QKeyEvent* event) const {
    return terminalModifiers(event->modifiers());
}

} // namespace litecode::ui
