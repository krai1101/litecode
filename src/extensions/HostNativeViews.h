#pragma once
#include "extensions/NativeViewApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
namespace litecode::extensions {
class HostNativeViews final : public QObject {
  public:
    explicit HostNativeViews(std::function<void(const QJsonObject&)> send);
    ~HostNativeViews() override;
    Disposable registerProvider(const QString& id, std::shared_ptr<NativeViewProvider> provider);
    bool accept(const QJsonObject& message);
    void shutdown();

  private:
    class View;
    struct Registration {
        std::shared_ptr<NativeViewProvider> provider;
        std::shared_ptr<View> view;
        std::shared_ptr<CancellationTokenSource> cancellation;
        QString handle;
    };
    void remove(const QString& id, const QString& handle);
    void publish(QJsonObject message);
    QHash<QString, Registration> registrations_;
    std::function<void(const QJsonObject&)> send_;
    quint64 nextHandle_{};
    bool stopped_{};
};
} // namespace litecode::extensions
