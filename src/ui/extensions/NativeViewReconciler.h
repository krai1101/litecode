#pragma once
#include "extensions/NativeViewApi.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>
namespace litecode::ui {
// Keeps stable control IDs attached to their existing native widgets.
class NativeViewReconciler {
  public:
    using Widgets = QHash<QString, QPointer<QWidget>>;
    bool unchanged(const extensions::NativeControl& root, const Widgets& widgets) const;
    void begin(const extensions::NativeControl& root, const Widgets& widgets);
    QWidget* take(const extensions::NativeControl& node, QWidget* parent, Widgets& next,
                  bool& exact);
    QVector<QPointer<QWidget>> finish();
    bool reusedExactly(const QString& id) const { return exact_.contains(id); }
    bool textChanged(const QString& id, const QString& text);
    bool enabledChanged(const QString& id, bool enabled);
    void input(const extensions::NativeViewEvent& event);
    void reset();

  private:
    void invalidateAncestors(const QString& id);
    void consume(const extensions::NativeControl& node, Widgets& next);
    QHash<QString, extensions::NativeControl> nodes_, pending_;
    QHash<QString, QString> parents_, pendingParents_;
    QSet<QString> dirty_;
    QSet<QString> exact_;
    Widgets remaining_;
};
} // namespace litecode::ui
