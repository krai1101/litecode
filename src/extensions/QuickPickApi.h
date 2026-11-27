#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/ThemeApi.h"
#include <QFuture>
#include <QVector>
#include <exception>
#include <functional>
#include <optional>
#include <variant>
namespace litecode::extensions {
enum class QuickPickItemKind { Separator = -1, Default = 0 };
enum class QuickInputButtonLocation { Title = 1, Inline = 2, Input = 3 };
struct QuickInputButton final {
    IconPath iconPath;
    std::optional<QString> tooltip;
    std::optional<QuickInputButtonLocation> location;
    struct Toggle {
        bool checked{};
    };
    std::optional<Toggle> toggle;
};
class QuickInputButtons final {
  public:
    static const QuickInputButton Back;

  private:
    QuickInputButtons() = delete;
};
inline const QuickInputButton QuickInputButtons::Back{
    ThemeIcon("arrow-left"), QString("Back"), QuickInputButtonLocation::Title, {}};
struct QuickPickItem {
    QString label;
    std::optional<QuickPickItemKind> kind;
    std::optional<IconPath> iconPath;
    std::optional<QString> description, detail;
    std::optional<QUrl> resourceUri;
    std::optional<bool> picked, alwaysShow;
    std::optional<QVector<QuickInputButton>> buttons;
};
struct QuickPickOptions final {
    std::optional<QString> title, placeHolder, prompt;
    std::optional<bool> matchOnDescription, matchOnDetail, ignoreFocusOut, canPickMany;
    std::function<void(const std::variant<QuickPickItem, QString>&)> onDidSelectItem;
};
// Native result models the runtime canPickMany switch without inventing a
// second public method/type name for multi-selection.
template <typename T> using QuickPickSelection = std::variant<T, QVector<T>>;
namespace detail {
struct QuickPickSession {
    QFuture<std::optional<QVector<int>>> result;
    std::function<void(QVector<QuickPickItem>)> setItems;
    std::function<void(std::exception_ptr)> fail;
};
} // namespace detail
} // namespace litecode::extensions
