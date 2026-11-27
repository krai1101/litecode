#include "extensions/HostDialogs.h"
#include "extensions/DialogWire.h"
#include <QJsonArray>
#include <QMutexLocker>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
HostDialogs::HostDialogs(std::function<void(const QJsonObject&)> send) : send_(std::move(send)) {}
HostDialogs::~HostDialogs() { shutdown(); }
QFuture<std::optional<QVector<QUrl>>>
HostDialogs::showOpenDialog(const OpenDialogOptions& options) {
    return request(false, wire::encodeOpenDialogOptions(options));
}
QFuture<std::optional<QUrl>> HostDialogs::showSaveDialog(const SaveDialogOptions& options) {
    return request(true, wire::encodeSaveDialogOptions(options))
        .then([](std::optional<QVector<QUrl>> resources) -> std::optional<QUrl> {
            if (!resources)
                return {};
            return resources->first();
        });
}
QFuture<std::optional<QVector<QUrl>>> HostDialogs::request(bool save, const QJsonObject& options) {
    auto promise = std::make_shared<QPromise<std::optional<QVector<QUrl>>>>();
    promise->start();
    const auto future = promise->future();
    const bool valid = save ? wire::decodeSaveDialogOptions(options).has_value()
                            : wire::decodeOpenDialogOptions(options).has_value();
    if (!valid) {
        promise->setException(
            std::make_exception_ptr(std::invalid_argument("Invalid file dialog options.")));
        promise->finish();
        return future;
    }
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
            promise->setException(
                std::make_exception_ptr(std::runtime_error("File dialog service unavailable.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, {promise, save, !save && options.value("canSelectMany").toBool()});
    }
    const QJsonObject message{{"event", "windowFileDialog"},
                              {"requestId", QString::number(id)},
                              {"save", save},
                              {"options", options}};
    QMetaObject::invokeMethod(
        this,
        [this, id, message] {
            {
                QMutexLocker lock(&mutex_);
                if (stopped_ || !pending_.contains(id))
                    return;
            }
            try {
                send_(message);
            } catch (...) {
                complete(id, {}, "Could not send file dialog request.");
            }
        },
        Qt::QueuedConnection);
    return future;
}
void HostDialogs::complete(quint64 id, const QJsonValue& resources, const QString& error) {
    Pending pending;
    {
        QMutexLocker lock(&mutex_);
        pending = pending_.take(id);
    }
    if (!pending.promise)
        return;
    try {
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        if (resources.isNull())
            pending.promise->addResult(std::nullopt);
        else {
            if (!resources.isArray())
                throw std::runtime_error("Invalid file dialog result.");
            const auto values = resources.toArray();
            if (values.isEmpty() || ((!pending.many || pending.save) && values.size() != 1))
                throw std::runtime_error("Invalid file dialog selection count.");
            QVector<QUrl> result;
            result.reserve(values.size());
            for (const auto& value : values) {
                if (!value.isString())
                    throw std::runtime_error("Invalid file dialog URI.");
                const QUrl uri(value.toString(), QUrl::StrictMode);
                if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty())
                    throw std::runtime_error("Invalid file dialog URI.");
                result.append(uri);
            }
            pending.promise->addResult(std::optional<QVector<QUrl>>(std::move(result)));
        }
    } catch (...) {
        pending.promise->setException(std::current_exception());
    }
    pending.promise->finish();
}
void HostDialogs::shutdown() {
    QHash<quint64, Pending> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending = std::exchange(pending_, {});
    }
    for (const auto& request : pending) {
        request.promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before file dialog completed.")));
        request.promise->finish();
    }
}
} // namespace litecode::extensions
