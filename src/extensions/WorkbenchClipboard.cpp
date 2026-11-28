#include "extensions/WorkbenchClipboard.h"
#include <exception>

namespace litecode::extensions {
bool WorkbenchClipboard::accept(const QJsonObject& request, const Reply& reply) {
    const auto encoded = request.value("requestId").toString();
    bool valid = false;
    const auto id = encoded.toULongLong(&valid);
    if (request.value("event") != QJsonValue("clipboard") || !valid || !id ||
        QString::number(id) != encoded || !request.value("write").isBool() ||
        (request.value("write").toBool() && !request.value("text").isString()))
        return false;
    QJsonValue result(QJsonValue::Null);
    QString error;
    // A native backend can emit UI signals synchronously. Copy its callbacks so
    // reentrant owner destruction cannot invalidate the callable being invoked.
    const auto read = read_;
    const auto write = write_;
    try {
        if (request.value("write").toBool()) {
            if (!write)
                error = "System clipboard is unavailable.";
            else
                write(request.value("text").toString());
        } else {
            if (!read)
                error = "System clipboard is unavailable.";
            else
                result = read();
        }
    } catch (const std::exception& exception) {
        error = QString::fromUtf8(exception.what());
    } catch (...) {
        error = "System clipboard operation failed.";
    }
    reply(result, error);
    return true;
}
} // namespace litecode::extensions
