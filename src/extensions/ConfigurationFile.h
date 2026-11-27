#pragma once

#include "extensions/ConfigurationModel.h"
#include <QMutex>

namespace litecode::extensions {

// A settings document (settings.json, or the "settings" object inside a
// .code-workspace file). It owns only parsing and atomic file persistence,
// not configuration targets, schema rules, editor buffers, IPC or events.
class ConfigurationFile final {
  public:
    explicit ConfigurationFile(QString path, QStringList settingsPath = {});
    [[nodiscard]] const QString& path() const { return path_; }
    [[nodiscard]] ConfigurationLayer read() const;
    // Undefined removes the key. Dotted setting names are literal properties
    // in settings files, not a request to create nested JSON objects.
    // Returns false for a no-op. Throws before changing an invalid document.
    bool update(const QString& key, const QJsonValue& value,
                const std::optional<QString>& languageId = {});

  private:
    QString path_;
    QStringList settingsPath_;
    mutable QMutex mutex_;
};

} // namespace litecode::extensions
