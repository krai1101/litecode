#pragma once
#include "extensions/TextContentWire.h"
#include <QFuture>
#include <QObject>
#include <QPromise>
#include <memory>

namespace litecode::extensions {
// Private, owner-scoped resolver. Multiple providers per scheme are supported;
// the newest provider is tried first and absence falls through to earlier ones.
class WorkbenchContentProviders final : public QObject {
    Q_OBJECT
  public:
    explicit WorkbenchContentProviders(QObject* parent = nullptr) : QObject(parent) {}
    ~WorkbenchContentProviders() override;
    bool registerProvider(const QString& owner, quint64 handle, const QString& scheme);
    bool unregisterProvider(const QString& owner, quint64 handle);
    void removeOwner(const QString& owner);
    QFuture<std::optional<QString>> provide(const QUrl& uri);
    bool accept(const QString& owner, const QJsonObject& message, QString* error);
    void fail(quint64 request, const QString& error);
  signals:
    void contentRequested(const QString& owner, quint64 handle, quint64 request, const QUrl& uri);
    void virtualDocumentChanged(const QUrl& uri, const QString& content);

  private:
    struct Provider {
        QString owner;
        quint64 handle;
        QString scheme;
    };
    struct Pending {
        Provider provider;
        QUrl uri;
        std::shared_ptr<QPromise<std::optional<QString>>> promise;
    };
    struct Resolution {
        QUrl uri;
        QVector<Provider> providers;
        qsizetype index{};
        std::shared_ptr<QPromise<std::optional<QString>>> promise;
    };
    void next(const std::shared_ptr<Resolution>& resolution);
    void complete(quint64 request, std::optional<QString> text, const QString& error);
    static QString key(const QString& owner, quint64 handle);
    QVector<Provider> providers_;
    QHash<QString, QString> known_; // Also authenticates already-started updates after disposal.
    QHash<QString, wire::TextContentDecoder> decoders_;
    QHash<quint64, Pending> pending_;
    QVector<std::weak_ptr<Resolution>> resolutions_;
    quint64 nextRequest_{};
    bool stopped_{};
};
} // namespace litecode::extensions
