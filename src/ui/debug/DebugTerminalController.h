#pragma once
#include "core/OperationResult.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QList>
#include <QJsonObject>
#include <functional>
namespace litecode::ui {
class RunDebugPanel;
class TerminalPanel;
class DebugTerminalController final : public QObject {
    Q_OBJECT
  public:
    DebugTerminalController(RunDebugPanel* debug, TerminalPanel* terminal,
        std::function<QString()> workspace, QObject* parent = nullptr);
    ~DebugTerminalController() override;
  signals:
    void terminalCreated();
  private:
    void launch(const QJsonObject& request);
    void complete(core::OperationId id, qint64 pid, const QString& error);
    void cancelPending();
    QPointer<RunDebugPanel> debug_;
    QPointer<TerminalPanel> terminal_;
    std::function<QString()> workspace_;
    QHash<quint64, QJsonObject> pending_;
    QList<core::OperationId> leased_;
};
} // namespace litecode::ui
