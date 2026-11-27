#pragma once
#include "extensions/CommandApi.h"
#include "extensions/MarkdownApi.h"
#include "extensions/ThemeApi.h"
#include <optional>
#include <variant>
namespace litecode::extensions {
enum class StatusBarAlignment { Left = 1, Right = 2 };
struct AccessibilityInformation final {
    QString label;
    std::optional<QString> role;
};
using StatusBarTooltip = std::variant<QString, MarkdownString>;
using StatusBarColor = std::variant<QString, ThemeColor>;
using StatusBarCommand = std::variant<QString, Command>;
// Native accessors retain the stable property names. A same-name overload
// writes a property so updates can be observed without polling public fields.
class StatusBarItem {
  public:
    virtual ~StatusBarItem() = default;
    virtual QString id() const = 0;
    virtual StatusBarAlignment alignment() const = 0;
    virtual std::optional<double> priority() const = 0;
    virtual QString text() const = 0;
    virtual void text(QString value) = 0;
    virtual std::optional<QString> name() const = 0;
    virtual void name(std::optional<QString> value) = 0;
    virtual std::optional<StatusBarTooltip> tooltip() const = 0;
    virtual void tooltip(std::optional<StatusBarTooltip> value) = 0;
    virtual std::optional<StatusBarColor> color() const = 0;
    virtual void color(std::optional<StatusBarColor> value) = 0;
    virtual std::optional<ThemeColor> backgroundColor() const = 0;
    virtual void backgroundColor(std::optional<ThemeColor> value) = 0;
    virtual std::optional<StatusBarCommand> command() const = 0;
    virtual void command(std::optional<StatusBarCommand> value) = 0;
    virtual std::optional<AccessibilityInformation> accessibilityInformation() const = 0;
    virtual void accessibilityInformation(std::optional<AccessibilityInformation> value) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void dispose() = 0;
};
}
