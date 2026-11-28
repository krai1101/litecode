#pragma once
#include "extensions/NativeViewApi.h"
#include "extensions/NativeViewInputLedger.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
namespace litecode::extensions {
struct NativeViewContribution {
    QString id, title, container, containerTitle, location, iconPath, owner, extensionRoot;
};
class WorkbenchNativeViews final : public QObject {
    Q_OBJECT
  public:
    void setContributions(const QVector<NativeViewContribution>& contributions);
    QVector<NativeViewContribution> contributions() const;
    bool accept(const QString& owner, const QJsonObject& message);
    void removeOwner(const QString& owner,
                     const QString& error = QStringLiteral(
                         "Extension host stopped. Re-enable the extension to restart it."));
    void setVisible(const QString& id, bool visible);
    bool reveal(const QString& id);
    void input(const QString& id, quint64 revision, const NativeViewEvent& event);
    quint64 revision(const QString& id) const;
  signals:
    void contributionsChanged();
    void activationRequested(const QString& owner);
    void sendRequested(const QString& owner, const QJsonObject& message);
    void contentChanged(const QString& id, const litecode::extensions::NativeControl& root,
                        quint64 revision);
    void textChanged(const QString& id, const QString& control, const QString& text);
    void enabledChanged(const QString& id, const QString& control, bool enabled);
    void scrollToEndRequested(const QString& id, const QString& control);
    void showRequested(const QString& id, bool preserveFocus);
    void unavailable(const QString& id, const QString& error);

  private:
    struct Entry {
        NativeViewContribution contribution;
        QString handle;
        QHash<QString, NativeControlKind> controls;
        bool visible{}, resolved{};
        quint64 revision{};
        NativeViewInputLedger inputs;
    };
    void send(Entry& entry, QJsonObject message);
    QHash<QString, Entry> entries_;
};
} // namespace litecode::extensions
