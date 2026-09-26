#pragma once

class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

namespace litecode::ui::components {

struct TerminalFindControls final {
    QFrame* bar{};
    QLineEdit* input{};
    QLabel* countLabel{};
    QToolButton* matchCase{};
    QToolButton* wholeWord{};
    QToolButton* regularExpression{};
    QToolButton* previousButton{};
    QToolButton* nextButton{};
    QToolButton* closeButton{};
};

[[nodiscard]] TerminalFindControls createTerminalFindControls(QWidget* parent);

} // namespace litecode::ui::components
