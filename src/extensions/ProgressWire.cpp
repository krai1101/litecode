#include "extensions/ProgressWire.h"
#include <cmath>

namespace litecode::extensions::wire {
QJsonObject encodeProgressOptions(const ProgressOptions& options) {
    QJsonObject result;
    if (const auto* location = std::get_if<ProgressLocation>(&options.location))
        result.insert("location", static_cast<int>(*location));
    else result.insert("location", QJsonObject{{"viewId", std::get<ProgressOptions::ViewLocation>(options.location).viewId}});
    if (options.title) result.insert("title", *options.title);
    if (options.cancellable) result.insert("cancellable", *options.cancellable);
    return result;
}
std::optional<ProgressOptions> decodeProgressOptions(const QJsonObject& object) {
    ProgressOptions options;
    const auto location = object.value("location");
    if (location.isDouble()) {
        const auto value = location.toInt(-1);
        if (location.toDouble() != value || (value != 1 && value != 10 && value != 15)) return {};
        options.location = static_cast<ProgressLocation>(value);
    } else if (location.isObject() && location.toObject().value("viewId").isString()) {
        options.location = ProgressOptions::ViewLocation{location.toObject().value("viewId").toString()};
    } else return {};
    if (object.contains("title")) {
        if (!object.value("title").isString()) return {};
        options.title = object.value("title").toString();
    }
    if (object.contains("cancellable")) {
        if (!object.value("cancellable").isBool()) return {};
        options.cancellable = object.value("cancellable").toBool();
    }
    return options;
}
QJsonObject encodeProgressReport(const ProgressReport& report) {
    QJsonObject result;
    if (report.message) result.insert("message", *report.message);
    if (report.increment) result.insert("increment", *report.increment);
    return result;
}
std::optional<ProgressReport> decodeProgressReport(const QJsonObject& object) {
    ProgressReport report;
    if (object.contains("message")) {
        if (!object.value("message").isString()) return {};
        report.message = object.value("message").toString();
    }
    if (object.contains("increment")) {
        const auto increment = object.value("increment");
        if (!increment.isDouble() || !std::isfinite(increment.toDouble())) return {};
        report.increment = increment.toDouble();
    }
    return report;
}
} // namespace litecode::extensions::wire
