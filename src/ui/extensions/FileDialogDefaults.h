#pragma once
#include <QObject>
#include <QPointer>
#include <QUrl>

namespace litecode::extensions {
class ConfigurationService;
}
namespace litecode::ui {
class EditorSessionController;
class WorkbenchController;
// Workbench history/default-path policy is independent of host RPC and dialog
// presentation. A picker must not preselect the active file as its save target.
class FileDialogDefaults final : public QObject {
  public:
    FileDialogDefaults(EditorSessionController& editors, WorkbenchController& workbench,
                       extensions::ConfigurationService& configuration, QObject* parent);
    QUrl defaultUri() const;

  private:
    QString lastFile_, lastRoot_;
    QPointer<extensions::ConfigurationService> configuration_;
};
} // namespace litecode::ui
