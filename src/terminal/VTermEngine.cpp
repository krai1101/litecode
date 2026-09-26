#include "terminal/VTermEngine.h"

#include <vterm.h>

#include <QByteArray>
#include <QChar>

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <new>
#include <vector>

namespace litecode::terminal {
namespace {

VTermModifier toVTermModifiers(TerminalModifiers modifiers) {
    int result = VTERM_MOD_NONE;
    if (modifiers.testFlag(ShiftModifier))
        result |= VTERM_MOD_SHIFT;
    if (modifiers.testFlag(AltModifier))
        result |= VTERM_MOD_ALT;
    if (modifiers.testFlag(ControlModifier))
        result |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(result);
}

quint32 rgbValue(const VTermColor& color) {
    return (static_cast<quint32>(color.rgb.red) << 16U) |
           (static_cast<quint32>(color.rgb.green) << 8U) | static_cast<quint32>(color.rgb.blue);
}

VTermColor colorFromRgb(quint32 rgb) {
    VTermColor color{};
    vterm_color_rgb(&color, static_cast<uint8_t>((rgb >> 16U) & 0xffU),
                    static_cast<uint8_t>((rgb >> 8U) & 0xffU), static_cast<uint8_t>(rgb & 0xffU));
    return color;
}

} // namespace

struct VTermEngine::Private final {
    explicit Private(VTermEngine* owner, int requestedRows, int requestedColumns)
        : q(owner), rows(std::max(1, requestedRows)), columns(std::max(1, requestedColumns)) {
        terminal = vterm_new(rows, columns);
        if (terminal == nullptr)
            throw std::bad_alloc();
        vterm_set_utf8(terminal, 1);
        vterm_output_set_callback(terminal, &Private::outputCallback, this);
        VTermState* state = vterm_obtain_state(terminal);
        vterm_state_set_bold_highbright(state, 1);
        screen = vterm_obtain_screen(terminal);
        vterm_screen_enable_altscreen(screen, 1);
        vterm_screen_enable_reflow(screen, true);
        vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCREEN);
        static constexpr VTermScreenCallbacks callbacks{
            &Private::damageCallback,     &Private::moveRectCallback,
            &Private::moveCursorCallback, &Private::termPropertyCallback,
            &Private::bellCallback,       &Private::resizeCallback,
            &Private::scrollbackPush,     &Private::scrollbackPop,
            &Private::scrollbackClear};
        vterm_screen_set_callbacks(screen, &callbacks, this);
        vterm_screen_reset(screen, 1);
    }

    ~Private() {
        if (terminal != nullptr)
            vterm_free(terminal);
    }

    TerminalCell convertCell(VTermScreenCell source) const {
        TerminalCell result;
        if (source.chars[0] == std::numeric_limits<uint32_t>::max()) {
            result.width = 0;
            return result;
        }
        QString text;
        for (uint32_t character : source.chars) {
            if (character == 0)
                break;
            const char32_t scalar = static_cast<char32_t>(character);
            text.append(QString::fromUcs4(&scalar, 1));
        }
        result.text = text;
        result.width = static_cast<int>(source.width);
        result.defaultForeground = VTERM_COLOR_IS_DEFAULT_FG(&source.fg);
        result.defaultBackground = VTERM_COLOR_IS_DEFAULT_BG(&source.bg);
        vterm_screen_convert_color_to_rgb(screen, &source.fg);
        vterm_screen_convert_color_to_rgb(screen, &source.bg);
        result.foreground = rgbValue(source.fg);
        result.background = rgbValue(source.bg);
        result.bold = source.attrs.bold;
        result.italic = source.attrs.italic;
        result.underline = source.attrs.underline != VTERM_UNDERLINE_OFF;
        result.strike = source.attrs.strike;
        result.reverse = source.attrs.reverse;
        return result;
    }

    const VTermScreenCell* historicalCell(int absoluteRow, int column) const {
        if (absoluteRow < 0 || absoluteRow >= static_cast<int>(scrollback.size()) || column < 0 ||
            column >= columns)
            return nullptr;
        const auto& line = scrollback.at(static_cast<size_t>(absoluteRow));
        return column < static_cast<int>(line.size()) ? &line.at(static_cast<size_t>(column))
                                                      : nullptr;
    }

    static void outputCallback(const char* bytes, size_t length, void* user) {
        auto* self = static_cast<Private*>(user);
        emit self->q->outputGenerated(QByteArray(bytes, static_cast<qsizetype>(length)));
    }

    static int damageCallback(VTermRect, void* user) {
        emit static_cast<Private*>(user)->q->updated();
        return 1;
    }

    static int moveRectCallback(VTermRect, VTermRect, void*) { return 1; }

    static int moveCursorCallback(VTermPos position, VTermPos, int visible, void* user) {
        auto* self = static_cast<Private*>(user);
        self->cursor = position;
        self->cursorShown = visible != 0;
        emit self->q->updated();
        return 1;
    }

    static int termPropertyCallback(VTermProp property, VTermValue* value, void* user) {
        auto* self = static_cast<Private*>(user);
        if (property == VTERM_PROP_CURSORVISIBLE) {
            self->cursorShown = value->boolean != 0;
            emit self->q->updated();
        } else if (property == VTERM_PROP_TITLE) {
            if (value->string.initial)
                self->pendingTitle.clear();
            const qsizetype remaining = terminalTitleCharacterLimit - self->pendingTitle.size();
            if (remaining > 0) {
                self->pendingTitle.append(
                    QString::fromUtf8(value->string.str, static_cast<qsizetype>(value->string.len))
                        .left(remaining));
            }
            if (value->string.final && self->title != self->pendingTitle) {
                self->title = self->pendingTitle;
                emit self->q->titleChanged(self->title);
            }
        } else if (property == VTERM_PROP_MOUSE) {
            self->mouseMode = value->number;
        } else if (property == VTERM_PROP_FOCUSREPORT) {
            self->focusReporting = value->boolean != 0;
        }
        return 1;
    }

    static int bellCallback(void* user) {
        emit static_cast<Private*>(user)->q->bell();
        return 1;
    }

    static int resizeCallback(int newRows, int newColumns, void* user) {
        auto* self = static_cast<Private*>(user);
        self->rows = newRows;
        self->columns = newColumns;
        emit self->q->updated();
        return 1;
    }

    static int scrollbackPush(int count, const VTermScreenCell* cells, void* user) {
        auto* self = static_cast<Private*>(user);
        self->scrollback.emplace_back(cells, cells + count);
        while (static_cast<int>(self->scrollback.size()) > terminalScrollbackLineLimit)
            self->scrollback.pop_front();
        return 1;
    }

    static int scrollbackPop(int count, VTermScreenCell* cells, void* user) {
        auto* self = static_cast<Private*>(user);
        if (self->scrollback.empty())
            return 0;
        const auto line = std::move(self->scrollback.back());
        self->scrollback.pop_back();
        std::fill(cells, cells + count, VTermScreenCell{});
        std::copy_n(line.begin(), std::min(count, static_cast<int>(line.size())), cells);
        return 1;
    }

    static int scrollbackClear(void* user) {
        static_cast<Private*>(user)->scrollback.clear();
        return 1;
    }

    VTermEngine* q{};
    VTerm* terminal{};
    VTermScreen* screen{};
    int rows{};
    int columns{};
    VTermPos cursor{};
    bool cursorShown{true};
    int mouseMode{VTERM_PROP_MOUSE_NONE};
    bool focusReporting{};
    std::deque<std::vector<VTermScreenCell>> scrollback;
    QString title;
    QString pendingTitle;
};

VTermEngine::VTermEngine(int rows, int columns, QObject* parent)
    : QObject(parent), d_(std::make_unique<Private>(this, rows, columns)) {}

VTermEngine::~VTermEngine() = default;

void VTermEngine::feed(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return;
    vterm_input_write(d_->terminal, bytes.constData(), static_cast<size_t>(bytes.size()));
    vterm_screen_flush_damage(d_->screen);
}

void VTermEngine::resize(int rows, int columns) {
    rows = std::max(1, rows);
    columns = std::max(1, columns);
    if (rows == d_->rows && columns == d_->columns)
        return;
    vterm_set_size(d_->terminal, rows, columns);
    vterm_screen_flush_damage(d_->screen);
}

void VTermEngine::clear() {
    d_->scrollback.clear();
    vterm_screen_reset(d_->screen, 1);
    vterm_screen_flush_damage(d_->screen);
    emit updated();
}

void VTermEngine::clearScrollback() {
    if (d_->scrollback.empty())
        return;
    d_->scrollback.clear();
    emit updated();
}

void VTermEngine::setDefaultColors(quint32 foreground, quint32 background) {
    VTermColor fg = colorFromRgb(foreground);
    VTermColor bg = colorFromRgb(background);
    vterm_screen_set_default_colors(d_->screen, &fg, &bg);
    emit updated();
}

void VTermEngine::setAnsiPalette(const std::array<quint32, 16>& colors) {
    VTermState* state = vterm_obtain_state(d_->terminal);
    for (size_t index = 0; index < colors.size(); ++index) {
        const VTermColor color = colorFromRgb(colors.at(index));
        vterm_state_set_palette_color(state, static_cast<int>(index), &color);
    }
    emit updated();
}

void VTermEngine::sendText(const QString& text, TerminalModifiers modifiers) {
    const auto characters = text.toUcs4();
    for (char32_t character : characters)
        vterm_keyboard_unichar(d_->terminal, static_cast<uint32_t>(character),
                               toVTermModifiers(modifiers));
}

void VTermEngine::sendKey(TerminalKey key, TerminalModifiers modifiers, int functionNumber) {
    VTermKey translated = VTERM_KEY_NONE;
    switch (key) {
    case TerminalKey::Enter:
        translated = VTERM_KEY_ENTER;
        break;
    case TerminalKey::Tab:
        translated = VTERM_KEY_TAB;
        break;
    case TerminalKey::Backspace:
        translated = VTERM_KEY_BACKSPACE;
        break;
    case TerminalKey::Escape:
        translated = VTERM_KEY_ESCAPE;
        break;
    case TerminalKey::Up:
        translated = VTERM_KEY_UP;
        break;
    case TerminalKey::Down:
        translated = VTERM_KEY_DOWN;
        break;
    case TerminalKey::Left:
        translated = VTERM_KEY_LEFT;
        break;
    case TerminalKey::Right:
        translated = VTERM_KEY_RIGHT;
        break;
    case TerminalKey::Insert:
        translated = VTERM_KEY_INS;
        break;
    case TerminalKey::Delete:
        translated = VTERM_KEY_DEL;
        break;
    case TerminalKey::Home:
        translated = VTERM_KEY_HOME;
        break;
    case TerminalKey::End:
        translated = VTERM_KEY_END;
        break;
    case TerminalKey::PageUp:
        translated = VTERM_KEY_PAGEUP;
        break;
    case TerminalKey::PageDown:
        translated = VTERM_KEY_PAGEDOWN;
        break;
    case TerminalKey::Function:
        translated = static_cast<VTermKey>(VTERM_KEY_FUNCTION(std::clamp(functionNumber, 1, 255)));
        break;
    }
    vterm_keyboard_key(d_->terminal, translated, toVTermModifiers(modifiers));
}

void VTermEngine::sendPaste(const QString& text) {
    vterm_keyboard_start_paste(d_->terminal);
    sendText(text);
    vterm_keyboard_end_paste(d_->terminal);
}

void VTermEngine::sendMouseMove(int row, int column, TerminalModifiers modifiers) {
    vterm_mouse_move(d_->terminal, std::clamp(row, 0, d_->rows - 1),
                     std::clamp(column, 0, d_->columns - 1), toVTermModifiers(modifiers));
}

void VTermEngine::sendMouseButton(int button, bool pressed, TerminalModifiers modifiers) {
    vterm_mouse_button(d_->terminal, button, pressed, toVTermModifiers(modifiers));
}

void VTermEngine::sendFocus(bool focused) {
    if (!d_->focusReporting)
        return;
    VTermState* state = vterm_obtain_state(d_->terminal);
    focused ? vterm_state_focus_in(state) : vterm_state_focus_out(state);
}

int VTermEngine::rows() const { return d_->rows; }
int VTermEngine::columns() const { return d_->columns; }
int VTermEngine::historyLineCount() const { return static_cast<int>(d_->scrollback.size()); }
int VTermEngine::totalLineCount() const { return historyLineCount() + rows(); }

TerminalCell VTermEngine::cellAt(int absoluteRow, int column) const {
    if (const VTermScreenCell* historical = d_->historicalCell(absoluteRow, column))
        return d_->convertCell(*historical);
    const int screenRow = absoluteRow - historyLineCount();
    if (screenRow < 0 || screenRow >= rows() || column < 0 || column >= columns())
        return {};
    VTermScreenCell cell{};
    if (!vterm_screen_get_cell(d_->screen, {screenRow, column}, &cell))
        return {};
    return d_->convertCell(cell);
}

QString VTermEngine::lineText(int absoluteRow, bool trimRight) const {
    QString result;
    for (int column = 0; column < columns(); ++column) {
        const TerminalCell cell = cellAt(absoluteRow, column);
        if (cell.width > 0)
            result += cell.text.isEmpty() ? QStringLiteral(" ") : cell.text;
    }
    if (trimRight) {
        while (!result.isEmpty() && result.at(result.size() - 1).isSpace())
            result.chop(1);
    }
    return result;
}

QPoint VTermEngine::cursorPosition() const {
    return QPoint(d_->cursor.col, historyLineCount() + d_->cursor.row);
}

bool VTermEngine::cursorVisible() const { return d_->cursorShown; }

bool VTermEngine::mouseTrackingEnabled() const { return d_->mouseMode != VTERM_PROP_MOUSE_NONE; }
QString VTermEngine::title() const { return d_->title; }

} // namespace litecode::terminal
