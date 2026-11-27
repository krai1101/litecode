#pragma once
#include "extensions/DebugApi.h"
#include "debug/DapProtocol.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>

namespace litecode::extensions {
// A one-client, user-local stream connects the workbench DAP transport to an
// adapter object inside the extension host. No adapter process is spawned.
class HostInlineDebugAdapter final : public QObject {
  public:
    HostInlineDebugAdapter(std::shared_ptr<DebugAdapter> adapter, QObject* parent);
    ~HostInlineDebugAdapter() override;
    QString endpoint() const { return server_.fullServerName(); }
    void stop() noexcept;
  private:
    struct Delivery;
    void accept();
    void receive();
    void sendFrame(const QByteArray& frame);
    void flush();
    std::shared_ptr<DebugAdapter> adapter_;
    std::shared_ptr<Delivery> delivery_;
    Disposable subscription_;
    QLocalServer server_;
    QPointer<QLocalSocket> socket_;
    debug::DapDecoder decoder_;
    QByteArray output_;
    bool stopped_{};
};
} // namespace litecode::extensions
