#include "CodexViewIcons.h"
#include <QHash>
#include <QRegularExpression>
namespace litecode::codex {
namespace {
const QHash<QString, QString>& icons() {
    static const QHash<QString, QString> result{{"terminal", "square-terminal"},
                                                {"edit", "pencil"},
                                                {"file-add", "square-plus"},
                                                {"copy", "copy"},
                                                {"check", "check"},
                                                {"history", "rotate-ccw-clock"},
                                                {"settings", "settings"},
                                                {"refresh", "rotate-ccw"},
                                                {"ellipsis", "ellipsis"},
                                                {"arrow-left", "arrow-left"},
                                                {"new-file", "square-pen"},
                                                {"add", "plus"},
                                                {"device-desktop", "laptop-minimal"},
                                                {"target", "target"},
                                                {"search", "search"},
                                                {"file-media", "image"},
                                                {"tools", "wrench"},
                                                {"context-compact", "list-collapse"},
                                                {"lightbulb", "lightbulb"},
                                                {"arrow-up", "arrow-up"},
                                                {"primitive-square", "square"},
                                                {"debug-stop", "square"},
                                                {"shield", "shield-check"},
                                                {"shield-check", "shield-check"},
                                                {"warning", "shield-alert"},
                                                {"zap", "zap"},
                                                {"attach", "paperclip"},
                                                {"close", "x"}};
    return result;
}
QRegularExpressionMatch leadingIcon(const QString& text) {
    static const QRegularExpression pattern(R"(^\$\(([a-z-]+)\)\s*)");
    return pattern.match(text);
}
} // namespace
QString codexViewIconText(const QString& text) {
    const auto match = leadingIcon(text);
    return match.hasMatch() && icons().contains(match.captured(1))
               ? text.mid(match.capturedLength())
               : text;
}
void applyCodexViewIcons(extensions::NativeControl& control) {
    if (control.kind == extensions::NativeControlKind::Button) {
        if (control.appearance == extensions::NativeControlAppearance::Plain ||
            control.appearance == extensions::NativeControlAppearance::Muted)
            control.iconSize = 12;
        const auto match = leadingIcon(control.text);
        const auto name = icons().value(match.captured(1));
        if (!name.isEmpty()) {
            control.iconPath = "icons/" + name + ".svg";
            const bool messageAction = control.id.endsWith(".copyPrompt") ||
                                       control.id.endsWith(".copy") || control.id.endsWith(".edit");
            if (messageAction)
                control.iconSize = 12;
            else if (control.appearance != extensions::NativeControlAppearance::Plain &&
                     control.appearance != extensions::NativeControlAppearance::Muted)
                control.iconSize = 14;
            control.text = control.text.mid(match.capturedLength());
        }
    }
    for (auto& child : control.children)
        applyCodexViewIcons(child);
}
} // namespace litecode::codex
