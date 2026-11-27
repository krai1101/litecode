#pragma once
#include <QScrollBar>
#include <QString>
#include <QTextCursor>
namespace litecode::ui {
struct NativeViewTextState {
    int anchor{}, position{}, scroll{};
    bool atBottom{};
    template <class Editor> static NativeViewTextState capture(const Editor* editor) {
        const auto cursor = editor->textCursor();
        const auto* bar = editor->verticalScrollBar();
        return {cursor.anchor(), cursor.position(), bar->value(),
                bar->value() >= bar->maximum() - 2};
    }
    template <class Editor> void restore(Editor* editor) const {
        auto cursor = editor->textCursor();
        const auto last = qMax(0, editor->document()->characterCount() - 1);
        cursor.setPosition(qMin(anchor, last));
        cursor.setPosition(qMin(position, last), QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        auto* bar = editor->verticalScrollBar();
        bar->setValue(atBottom && editor->isReadOnly() ? bar->maximum() : scroll);
    }
};
} // namespace litecode::ui
