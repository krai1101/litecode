#pragma once
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QWidget>
class QVBoxLayout;
class QLabel;
class QTextBrowser;
namespace litecode::extensions {
class ExtensionManager;
}
namespace litecode::ui::chat {
// Workbench renderer, not an extension-facing widget/API. Native extensions
// send typed parts through the host bridge and never insert their own widgets.
class ParticipantResponseView final : public QWidget {
    Q_OBJECT
  public:
    ParticipantResponseView(extensions::ExtensionManager* manager, QWidget* parent);
    void appendPart(const QJsonObject& part);
    void finish(const QJsonObject& result, const QString& error);
  signals:
    void resourceActivated(const QUrl& uri, int line, int character);

  private:
    void activateResource(const QJsonValue& resource);
    void openLink(const QUrl& uri, const QJsonObject& markdown);
    void invokeCommand(const QString& id, const QJsonArray& arguments);
    QPointer<extensions::ExtensionManager> manager_;
    QVBoxLayout* content_;
    QLabel* progress_;
    QLabel* error_;
    QSet<quint64> commands_;
    QPointer<QTextBrowser> markdown_;
    QString markdownText_;
    QJsonObject markdownOptions_;
    bool finished_{};
};
} // namespace litecode::ui::chat
