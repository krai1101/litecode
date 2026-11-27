#pragma once
#include "extensions/ExtensionManager.h"
#include <QObject>
#include <QPointer>
#include <functional>
class QWidget;
namespace litecode::ui {
class QuickOpenDialog;
class DebugConfigurationPicker final : public QObject {
    Q_OBJECT
  public:
    DebugConfigurationPicker(extensions::ExtensionManager* manager, std::function<QString()> workspace, QWidget* parent);
    ~DebugConfigurationPicker() override;
    void createLaunch();
    void selectAndStart();
    void selectDynamic(const QString& type);
    void cancel();
  signals:
    void openConfiguration(const QString& path);
    void configurationSelected(const QJsonObject& configuration);
    void dynamicConfigurationSelected(const QJsonObject& configuration);
  private:
    enum class SelectionMode { CreateLaunch, StartDebugging, SelectConfiguration };
    void selectType(SelectionMode mode);
    void requestConfigurations(const QString& type);
    void provided(quint64 request, const QJsonValue& result, const QString& error);
    void saveInitial(const QJsonArray& configurations);
    QPointer<extensions::ExtensionManager> manager_;
    std::function<QString()> workspaceSource_;
    QPointer<QWidget> window_;
    QPointer<QuickOpenDialog> picker_;
    QString workspace_;
    QJsonArray initial_;
    quint64 request_{};
    SelectionMode mode_{SelectionMode::CreateLaunch};
};
} // namespace litecode::ui
