#pragma once
#include "extensions/ContentProviderApi.h"
#include "extensions/HostDocuments.h"
#include <QMutex>
#include <QPointer>
#include <QQueue>

namespace litecode::extensions {
class HostContentProviders final : public QObject {
  public:
    HostContentProviders(HostDocuments& documents, std::function<void(const QJsonObject&)> send);
    ~HostContentProviders() override;
    Disposable registerProvider(const QString& scheme,
                                std::shared_ptr<TextDocumentContentProvider> provider);
    void provide(quint64 handle, quint64 request, const QUrl& uri);
    void shutdown();

  private:
    struct Registration {
        QString scheme;
        quint64 handle{};
        std::shared_ptr<TextDocumentContentProvider> provider;
        Disposable subscription;
        QQueue<QUrl> changes;
        bool changing{};
    };
    void unregister(quint64 handle);
    void changed(const std::shared_ptr<Registration>& registration, const QUrl& uri);
    void nextChange(const std::shared_ptr<Registration>& registration);
    QFuture<std::optional<QString>> invoke(const std::shared_ptr<Registration>& registration,
                                           const QUrl& uri);
    void sendResult(quint64 handle, quint64 request, const QUrl& uri, std::optional<QString> text,
                    QString error, bool change);
    bool stopped() const;
    QPointer<HostDocuments> documents_;
    std::function<void(const QJsonObject&)> send_;
    mutable QMutex mutex_;
    QHash<quint64, std::shared_ptr<Registration>> registrations_;
    quint64 nextHandle_{}, nextTransfer_{};
    bool stopped_{};
};
} // namespace litecode::extensions
