#pragma once
#include <QPointer>
#include <QWidget>
class QLabel;
class QPushButton;
namespace litecode::extensions {
class ExtensionManager;
}
namespace litecode::ui {
class NativeMarkdownView;
class ExtensionDetailsPage final : public QWidget {
    Q_OBJECT
  public:
    ExtensionDetailsPage(extensions::ExtensionManager& manager, QString id,
                         QWidget* parent = nullptr);
    void setCodeFont(const QFont& font);
  signals:
    void enableRequested(bool enabled);
    void uninstallRequested();

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void refresh();
    QPointer<extensions::ExtensionManager> manager_;
    QString id_;
    QString documentedPackage_;
    QLabel *icon_{}, *name_{}, *identity_{}, *description_{}, *status_{}, *diagnostic_{}, *location_{};
    NativeMarkdownView* readme_{};
    QWidget* documentation_{};
    QPushButton *enable_{}, *uninstall_{};
    bool enabled_{};
};
} // namespace litecode::ui
