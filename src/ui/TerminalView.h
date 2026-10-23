#pragma once

#include "terminal/VTermEngine.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

class QContextMenuEvent;
class QEvent;
class QFocusEvent;
class QFont;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;

namespace litecode::ui {

class TerminalView final : public QAbstractScrollArea {
    Q_OBJECT

  public:
    struct GridPosition final {
        int row{};
        int column{};
    };

    explicit TerminalView(QWidget* parent = nullptr);

    void feed(const QByteArray& bytes);
    void clearTerminal();
    [[nodiscard]] terminal::VTermEngine* engine() const;
    [[nodiscard]] QSize terminalSize() const;
    [[nodiscard]] QString plainText() const;
    [[nodiscard]] QString selectedText() const;
    [[nodiscard]] bool hasSelection() const;
    void setSearch(const QString& query, bool matchCase, bool wholeWord, bool regularExpression);
    void findNext(bool backwards = false);
    void clearSearch();
    void setTerminalFont(const QFont& font);

  signals:
    void inputGenerated(const QByteArray& bytes);
    void terminalSizeChanged(const QSize& characters);
    void terminalTitleChanged(const QString& title);
    void searchResultChanged(int current, int total);
    void linkActivated(const QString& target, int line, int column, bool external);

  protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

  private:
    void updateMetrics();
    void applyColorPalette(bool dark);
    void updateTerminalGeometry();
    void updateScrollBar(bool preservePosition = true);
    void copySelection();
    void pasteClipboard();
    void selectAllVisible();
    void refreshSearchMatches(bool preserveCurrent = true);
    void revealCurrentSearchMatch();
    [[nodiscard]] GridPosition gridPosition(const QPoint& viewportPosition) const;
    [[nodiscard]] bool isCellSelected(int absoluteRow, int column) const;
    [[nodiscard]] int searchMatchAt(int absoluteRow, int column) const;
    struct TerminalLink final {
        QString target;
        int line{};
        int column{};
        bool external{};
    };
    [[nodiscard]] std::optional<TerminalLink> linkAt(const GridPosition& position) const;
    void selectWordAt(const GridPosition& position);
    void selectLineAt(const GridPosition& position);
    bool sendMousePosition(const QPoint& viewportPosition, Qt::KeyboardModifiers modifiers);
    [[nodiscard]] std::pair<GridPosition, GridPosition> orderedSelection() const;
    [[nodiscard]] terminal::TerminalModifiers modifiersFor(QKeyEvent* event) const;

    terminal::VTermEngine* engine_{};
    int cellWidth_{8};
    int cellHeight_{16};
    int ascent_{12};
    int leftPadding_{16};
    int topPadding_{8};
    bool selecting_{};
    int terminalMouseButton_{};
    bool linkPressed_{};
    bool selectionActive_{};
    quint64 lastDoubleClickTimestamp_{};
    GridPosition selectionAnchor_;
    GridPosition selectionCaret_;
    struct SearchMatch final {
        GridPosition start;
        GridPosition end;
    };
    QString searchQuery_;
    QTimer* searchRefreshTimer_{};
    bool searchMatchCase_{};
    bool searchWholeWord_{};
    bool searchRegularExpression_{};
    std::vector<SearchMatch> searchMatches_;
    int currentSearchMatch_{-1};
    QByteArray recentOutputTail_;
};

} // namespace litecode::ui
