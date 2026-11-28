#pragma once
#include "extensions/InputBoxApi.h"
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
namespace litecode::extensions {
class HostInputBox final : public QObject {
  public:
    explicit HostInputBox(std::function<void(const QJsonObject&)> send);
    ~HostInputBox() override;
    QFuture<std::optional<QString>> show(const InputBoxOptions& options, CancellationToken token);
    bool accept(const QJsonObject& message);
    void shutdown();

  private:
    struct Pending {
        std::shared_ptr<QPromise<std::optional<QString>>> promise;
        std::function<InputBoxValidatorResult(const QString&)> validator;
        Disposable cancellation;
        bool sent{};
    };
    void complete(quint64 id, const QJsonValue& value, const QString& error);
    void fail(quint64 id, const QString& error);
    void cancel(quint64 id);
    void validate(quint64 id, quint64 sequence, const QString& value);
    void validationReply(quint64 id, quint64 sequence, const InputBoxValidationResult& result,
                         const QString& error = {});
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
