#pragma once
#include "extensions/NativeViewApi.h"
#include "ui/extensions/NativeViewReconciler.h"
#include <QHash>
#include <QPointer>
#include <QTemporaryDir>
#include <QWidget>
#include <functional>
class QVBoxLayout;
class QAction;
namespace litecode::ui {
class NativeViewWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit NativeViewWidget(QWidget* parent = nullptr);
    void setContent(const extensions::NativeControl& root, quint64 revision);
    void setText(const QString& id, const QString& text);
    void setControlEnabled(const QString& id, bool enabled);
    void scrollToEnd(const QString& id);
    void showError(const QString& error);
    void setCodeFont(const QFont& font);
  signals:
    void input(quint64 revision, const litecode::extensions::NativeViewEvent& event);

  protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QWidget* build(const extensions::NativeControl& node, QWidget* parent);
    void publish(const QString& id, const QString& type, const QString& text = {}, int index = -1,
                 bool checked = false);
    void clear();
    void applyTheme();
    void bindHoverActions(const extensions::NativeControl& node, QWidget* widget);
    bool updateMenuItem(const QString& id,
                        const std::function<void(extensions::NativeControl&, QAction*)>& update);
    QFont codeFont_;
    QTemporaryDir pastedImages_;
    QVBoxLayout* layout_{};
    QHash<QString, QPointer<QWidget>> controls_;
    QHash<QString, extensions::NativeControl> menus_;
    NativeViewReconciler reconciler_;
    quint64 revision_{};
    bool building_{};
};
} // namespace litecode::ui
