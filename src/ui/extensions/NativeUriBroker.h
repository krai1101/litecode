#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>

namespace litecode::ui {
// Private desktop transport. It deliberately does not make the editor a
// single-instance application: every ordinary launch can still open a window.
class NativeUriBroker final : public QObject {
  public:
    explicit NativeUriBroker(QString profile, QObject* parent = nullptr);
    ~NativeUriBroker() override;
    bool listen(std::function<bool(const QUrl&)> receive);
    void activated();
    // Returns the accepted prefix; callers retain every undelivered callback.
    static qsizetype forward(const QString& profile, const QList<QUrl>& uris);
  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
