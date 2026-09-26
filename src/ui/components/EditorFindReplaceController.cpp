#include "ui/components/EditorFindReplaceController.h"

#include <QAbstractButton>
#include <QFrame>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

namespace litecode::ui::components {

EditorFindReplaceController::EditorFindReplaceController(EditorFindReplaceBindings bindings,
                                                         QObject* parent)
    : QObject(parent) {
    connect(&bindings.findInput, &QPlainTextEdit::textChanged, this,
            [findInputChanged = bindings.findInputChanged] { findInputChanged(); });
    connect(&bindings.previousButton, &QToolButton::clicked, this, bindings.findPrevious);
    connect(&bindings.nextButton, &QToolButton::clicked, this, bindings.findNext);
    connect(&bindings.replaceButton, &QToolButton::clicked, this, bindings.replaceNext);
    connect(&bindings.replaceAllButton, &QToolButton::clicked, this, bindings.replaceAll);
    connect(&bindings.closeButton, &QAbstractButton::clicked, &bindings.bar, &QFrame::hide);
    connect(&bindings.replaceToggle, &QToolButton::toggled, this,
            [setReplaceVisible = bindings.setReplaceVisible](bool visible) {
                setReplaceVisible(visible);
            });
}

} // namespace litecode::ui::components
