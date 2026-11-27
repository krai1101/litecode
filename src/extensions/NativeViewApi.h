#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/ProviderApi.h"
#include <QMargins>
#include <QStringList>
#include <QVector>
#include <optional>

namespace litecode::extensions {
// Native C++/Qt mapping of VS Code's contributed view/provider boundary.
// These are extension-owned UI values, not QWidget pointers or HTML documents.
// Providers, view methods and event callbacks run on the extension host's Qt
// thread. Marshal background results to that thread before updating a view.
enum class NativeControlKind {
    Column,
    Row,
    Scroll,
    Label,
    Markdown,
    Button,
    Input,
    TextEdit,
    ComboBox,
    CheckBox,
    List,
    Progress,
    Separator,
    Dialog,       // Native pane dialog: text is the title, children its content; Escape emits
                  // "dismissed".
    ChoiceSlider, // items/currentIndex form discrete choices; value is the default index.
                  // text is descriptive context; child combo boxes are popup context selectors;
                  // other children are leading actions.
    Image         // iconPath is a local full-colour image, text is its accessible description,
                  // and value is the thumbnail extent (default 80 logical pixels).
};
enum class NativeButtonStyle { Secondary, Primary, Ghost, Danger };
enum class NativeControlAppearance {
    Default,
    Card,
    Composer,
    Muted,
    Heading,
    Transcript,
    Added,
    Removed,
    Plain,
    HoverGroup,
    HoverActions,
    Surface, // A bordered content surface, independent of hover or selection.
    Shimmer  // Muted, body-size text with a native horizontal loading shimmer.
};
struct NativeControl {
    QString id;
    NativeControlKind kind{NativeControlKind::Column};
    NativeButtonStyle buttonStyle{NativeButtonStyle::Secondary};
    NativeControlAppearance appearance{NativeControlAppearance::Default};
    QString text, placeholder, toolTip;
    QString accessibleName;
    // Extension-owned local image for a button; theme icons in text remain
    // available when this is empty. Size is independent of the hit target.
    QString iconPath;
    int iconSize{16};
    // Button items form an anchored popup menu; selection emits "selected"
    // with the item's text/index. Empty items preserve the normal click event.
    // List items may use a tab to separate a title from trailing metadata.
    QStringList items;
    // Button children describe a structured menu: Button actions/submenus,
    // disabled Label headers and Separator rows. Leaf actions emit "clicked"
    // with their own control id. Menus are mutually exclusive with items.
    QStringList itemIconPaths, itemColors;
    QString helpText, helpUrl;
    QVector<NativeControl> children;
    // Input.password maps to a masked native line edit. Providers should leave
    // text empty and consume input events without persisting the secret in layouts.
    bool enabled{true}, visible{true}, readOnly{}, checked{}, password{};
    int spacing{6}, margin{8}, stretch{}, currentIndex{-1}, value{};
    // Optional per-edge Qt layout margins; otherwise margin applies to all edges.
    std::optional<QMargins> contentsMargins;
};
struct NativeViewEvent {
    QString controlId, type, text;
    int index{-1};
    bool checked{};
};
class NativeView {
  public:
    virtual ~NativeView() = default;
    virtual QString viewType() const = 0;
    virtual bool visible() const = 0;
    virtual void setContent(const NativeControl& root) = 0;
    virtual void setText(const QString& controlId, const QString& text) = 0;
    virtual void setEnabled(const QString& controlId, bool enabled) = 0;
    virtual void scrollToEnd(const QString& controlId) = 0;
    virtual void show(bool preserveFocus = false) = 0;
    virtual Event<NativeViewEvent> onDidReceiveMessage() const = 0;
    virtual Event<std::monostate> onDidChangeVisibility() const = 0;
    virtual Event<std::monostate> onDidDispose() const = 0;
};
class NativeViewProvider {
  public:
    virtual ~NativeViewProvider() = default;
    virtual ProviderResult<void> resolveView(std::shared_ptr<NativeView> view,
                                             CancellationToken token) = 0;
};
} // namespace litecode::extensions
