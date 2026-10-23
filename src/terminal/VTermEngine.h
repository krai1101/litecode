#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

#include <array>
#include <memory>

namespace litecode::terminal {

inline constexpr int terminalScrollbackLineLimit = 1'000;
inline constexpr qsizetype terminalTitleCharacterLimit = 4'096;

struct TerminalCell final {
    QString text;
    quint32 foreground{};
    quint32 background{};
    int width{1};
    bool defaultForeground{true};
    bool defaultBackground{true};
    bool bold{};
    bool italic{};
    bool underline{};
    bool strike{};
    bool reverse{};
};

enum class TerminalKey {
    Enter,
    Tab,
    Backspace,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Function
};

enum TerminalModifier { NoModifier = 0, ShiftModifier = 1, AltModifier = 2, ControlModifier = 4 };
Q_DECLARE_FLAGS(TerminalModifiers, TerminalModifier)

class VTermEngine final : public QObject {
    Q_OBJECT

  public:
    explicit VTermEngine(int rows = 24, int columns = 80, QObject* parent = nullptr);
    ~VTermEngine() override;

    VTermEngine(const VTermEngine&) = delete;
    VTermEngine& operator=(const VTermEngine&) = delete;

    void feed(const QByteArray& bytes);
    void resize(int rows, int columns);
    void clear();
    void clearScrollback();
    void setDefaultColors(quint32 foreground, quint32 background);
    void setAnsiPalette(const std::array<quint32, 16>& colors);

    void sendText(const QString& text, TerminalModifiers modifiers = NoModifier);
    void sendKey(TerminalKey key, TerminalModifiers modifiers = NoModifier, int functionNumber = 0);
    void sendPaste(const QString& text);
    void sendMouseMove(int row, int column, TerminalModifiers modifiers = NoModifier);
    void sendMouseButton(int button, bool pressed, TerminalModifiers modifiers = NoModifier);
    void sendFocus(bool focused);

    [[nodiscard]] int rows() const;
    [[nodiscard]] int columns() const;
    [[nodiscard]] int historyLineCount() const;
    [[nodiscard]] int totalLineCount() const;
    [[nodiscard]] TerminalCell cellAt(int absoluteRow, int column) const;
    [[nodiscard]] QString lineText(int absoluteRow, bool trimRight = true) const;
    [[nodiscard]] QPoint cursorPosition() const;
    [[nodiscard]] bool cursorVisible() const;
    [[nodiscard]] bool mouseTrackingEnabled() const;
    [[nodiscard]] QString title() const;

  signals:
    void updated();
    void outputGenerated(const QByteArray& bytes);
    void titleChanged(const QString& title);
    void bell();

  private:
    struct Private;
    std::unique_ptr<Private> d_;
};

} // namespace litecode::terminal

Q_DECLARE_OPERATORS_FOR_FLAGS(litecode::terminal::TerminalModifiers)
