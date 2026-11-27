#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QStringList>
#include <memory>

namespace litecode::core {

// A parsed immutable JSON-with-comments document. Targeted edits use source
// byte spans rather than serializing the complete settings file. Undefined
// removes a property; null is a normal JSON value. Parsing/editing throws on
// invalid input. This utility has no filesystem or extension dependencies.
class JsoncDocument final {
  public:
    [[nodiscard]] static JsoncDocument parse(QByteArray source, bool allowEmptyContent = false);
    [[nodiscard]] const QByteArray& source() const;
    [[nodiscard]] QJsonValue value() const;
    [[nodiscard]] QStringList propertyNames(const QStringList& path = {}) const;
    [[nodiscard]] QByteArray updateProperty(const QStringList& path, const QJsonValue& value) const;

  private:
    struct Data;
    explicit JsoncDocument(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};

} // namespace litecode::core
