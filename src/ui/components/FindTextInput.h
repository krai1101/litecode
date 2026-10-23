#pragma once

#include <QPlainTextEdit>
#include <QTextOption>

namespace litecode::ui::components {

class FindTextInput final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit FindTextInput(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
        setFrameShape(QFrame::NoFrame);
        document()->setDocumentMargin(0.0);
        setTabChangesFocus(true);
        setWordWrapMode(QTextOption::NoWrap);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    [[nodiscard]] QString text() const { return toPlainText(); }
    void setText(const QString& value) {
        if (toPlainText() != value)
            setPlainText(value);
    }
};

} // namespace litecode::ui::components
