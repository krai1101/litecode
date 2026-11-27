#pragma once
#include "extensions/WorkbenchInputBox.h"
#include "extensions/WorkbenchQuickPick.h"
#include <QPointer>

namespace litecode::extensions {
// Installed before presentation services: one extension Quick Input at a time.
class WorkbenchQuickInputCoordinator final : public QObject {
  public:
    WorkbenchQuickInputCoordinator(WorkbenchInputBox& boxes, WorkbenchQuickPick& picks,
                                   QObject* parent = nullptr);

  private:
    struct Active {
        bool picker;
        QString owner;
        quint64 id;
    };
    void replace(Active next);
    void clear(bool picker, const QString& owner, quint64 id);
    QPointer<WorkbenchInputBox> boxes_;
    QPointer<WorkbenchQuickPick> picks_;
    std::optional<Active> active_;
};
} // namespace litecode::extensions
