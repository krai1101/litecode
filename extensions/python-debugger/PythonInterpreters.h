#pragma once
#include <extensions/ExtensionApi.h>
namespace litecode::python {
struct Interpreter {
    QString executable, version;
    QStringList arguments;
    QJsonArray command() const;
};
class PythonInterpreters final : public QObject {
  public:
    explicit PythonInterpreters(extensions::ExtensionContext& context);
    QFuture<Interpreter> resolve(const QJsonValue& configured = {},
                                 extensions::CancellationToken token = {});
    QFuture<std::optional<Interpreter>> select(extensions::CancellationToken token = {});
    static QStringList executableCommand(const QJsonValue& value);

  private:
    QStringList candidates() const;
    QFuture<std::optional<Interpreter>> probe(QStringList command,
                                              extensions::CancellationToken token);
    extensions::ExtensionContext& context_;
};
} // namespace litecode::python
