#pragma once

#include <QObject>

#include <functional>

class QFrame;
class QPlainTextEdit;
class QAbstractButton;
class QToolButton;

namespace litecode::ui::components {

struct EditorFindReplaceBindings final {
    QFrame& bar;
    QPlainTextEdit& findInput;
    QToolButton& previousButton;
    QToolButton& nextButton;
    QToolButton& replaceButton;
    QToolButton& replaceAllButton;
    QAbstractButton& closeButton;
    QToolButton& replaceToggle;
    std::function<void()> findNext;
    std::function<void()> findInputChanged;
    std::function<void()> findPrevious;
    std::function<void()> replaceNext;
    std::function<void()> replaceAll;
    std::function<void(bool)> setReplaceVisible;
};

class EditorFindReplaceController final : public QObject {
    Q_OBJECT

  public:
    explicit EditorFindReplaceController(EditorFindReplaceBindings bindings,
                                         QObject* parent = nullptr);
};

} // namespace litecode::ui::components
