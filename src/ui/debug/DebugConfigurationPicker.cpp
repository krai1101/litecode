#include "ui/debug/DebugConfigurationPicker.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QWidget>
#include <utility>

namespace litecode::ui {
DebugConfigurationPicker::DebugConfigurationPicker(extensions::ExtensionManager* manager,
                                                   std::function<QString()> workspace,
                                                   QWidget* parent)
    : QObject(parent), manager_(manager), workspaceSource_(std::move(workspace)), window_(parent) {
    connect(manager, &extensions::ExtensionManager::debugConfigurationResult, this,
            &DebugConfigurationPicker::provided);
}
DebugConfigurationPicker::~DebugConfigurationPicker() { cancel(); }
void DebugConfigurationPicker::cancel() {
    const auto request = std::exchange(request_, 0);
    if (manager_ && request)
        manager_->cancelDebugConfiguration(request);
    if (picker_) {
        picker_->disconnect(this);
        picker_->reject();
        picker_->deleteLater();
        picker_ = nullptr;
    }
}
void DebugConfigurationPicker::createLaunch() { selectType(SelectionMode::CreateLaunch); }
void DebugConfigurationPicker::selectAndStart() { selectType(SelectionMode::StartDebugging); }
void DebugConfigurationPicker::selectDynamic(const QString& type) {
    cancel();
    if (!manager_)
        return;
    workspace_ = workspaceSource_();
    mode_ = SelectionMode::SelectConfiguration;
    requestConfigurations(type);
}
void DebugConfigurationPicker::selectType(SelectionMode mode) {
    cancel();
    if (!manager_)
        return;
    workspace_ = workspaceSource_();
    mode_ = mode;
    const bool create = mode == SelectionMode::CreateLaunch;
    if (create && workspace_.isEmpty()) {
        ConfirmationDialog::showMessage(window_, tr("Run and Debug"),
                                        tr("Open a folder to create launch.json."));
        return;
    }
    const auto path = QDir(workspace_).filePath(".vscode/launch.json");
    if (create && QFileInfo::exists(path)) {
        emit openConfiguration(path);
        return;
    }
    QVector<QuickPickItem> items;
    QSet<QString> seen, ambiguous;
    for (const auto& contribution : manager_->debuggerContributions()) {
        if (seen.contains(contribution.type)) {
            ambiguous.insert(contribution.type);
            continue;
        }
        seen.insert(contribution.type);
        items.append({contribution.type,
                      contribution.label.isEmpty() ? contribution.type : contribution.label,
                      contribution.extensionId});
    }
    items.removeIf([&](const QuickPickItem& item) { return ambiguous.contains(item.id); });
    if (items.isEmpty()) {
        ConfirmationDialog::showMessage(
            window_, tr("Run and Debug"),
            tr("Enable an extension that contributes a debugger first."));
        return;
    }
    auto* picker = new QuickOpenDialog(tr("Select a debugger"), tr("Debugger"), items, {}, window_);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker_ = picker;
    connect(picker, &QDialog::accepted, this, [this, picker] {
        if (!manager_ || workspace_ != workspaceSource_())
            return;
        picker_ = nullptr;
        requestConfigurations(picker->selectedItemId());
    });
    picker->open();
}
void DebugConfigurationPicker::requestConfigurations(const QString& type) {
    const bool create = mode_ == SelectionMode::CreateLaunch;
    if (create) {
        initial_ = {};
        for (const auto& contribution : manager_->debuggerContributions())
            if (contribution.type == type)
                initial_ = contribution.initialConfigurations;
    }
    request_ = manager_->requestDebugConfiguration(
        type, "provideDebugConfigurations", QJsonValue(QJsonValue::Undefined),
        create ? extensions::DebugConfigurationProviderTriggerKind::Initial
               : extensions::DebugConfigurationProviderTriggerKind::Dynamic);
    if (!request_)
        ConfirmationDialog::showMessage(window_, tr("Run and Debug"),
                                        tr("Could not activate configuration providers."));
}
void DebugConfigurationPicker::provided(quint64 request, const QJsonValue& result,
                                        const QString& error) {
    if (!request_ || request_ != request)
        return;
    request_ = 0;
    if (workspace_ != workspaceSource_())
        return;
    if (!error.isEmpty() || !result.isArray()) {
        ConfirmationDialog::showMessage(window_, tr("Run and Debug"),
                                        error.isEmpty() ? tr("Invalid provided configurations.")
                                                        : error);
        return;
    }
    const auto configurations = result.toArray();
    if (mode_ == SelectionMode::CreateLaunch) {
        saveInitial(configurations.isEmpty() ? initial_ : configurations);
        return;
    }
    if (configurations.isEmpty()) {
        ConfirmationDialog::showMessage(window_, tr("Run and Debug"),
                                        tr("This debugger supplied no dynamic configurations. "
                                           "Create launch.json to configure a session."));
        return;
    }
    QVector<QuickPickItem> items;
    for (qsizetype index = 0; index < configurations.size(); ++index) {
        const auto configuration = configurations.at(index).toObject();
        items.append({QString::number(index), configuration.value("name").toString(),
                      configuration.value("type").toString()});
    }
    auto* picker = new QuickOpenDialog(tr("Select a debug configuration"),
                                       tr("Debug Configuration"), items, {}, window_);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker_ = picker;
    connect(picker, &QDialog::accepted, this, [this, picker, configurations] {
        picker_ = nullptr;
        if (workspace_ != workspaceSource_())
            return;
        bool valid = false;
        const auto index = picker->selectedItemId().toInt(&valid);
        if (!valid || index < 0 || index >= configurations.size())
            return;
        const auto configuration = configurations.at(index).toObject();
        if (mode_ == SelectionMode::SelectConfiguration)
            emit dynamicConfigurationSelected(configuration);
        else
            emit configurationSelected(configuration);
    });
    picker->open();
}
void DebugConfigurationPicker::saveInitial(const QJsonArray& configurations) {
    QDir folder(workspace_);
    const auto path = folder.filePath(".vscode/launch.json");
    if (QFileInfo::exists(path)) {
        emit openConfiguration(path);
        return;
    } // never replace user changes while a provider runs
    if (!folder.mkpath(".vscode")) {
        ConfirmationDialog::showMessage(window_, tr("Run and Debug"),
                                        tr("Could not create the .vscode folder."));
        return;
    }
    QFile file(path);
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", "0.2.0"}, {"configurations", configurations}})
            .toJson();
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        ConfirmationDialog::showMessage(
            window_, tr("Run and Debug"),
            tr("Could not create launch.json: %1").arg(file.errorString()));
        return;
    }
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        const auto error = file.errorString();
        file.close();
        const bool removed = file.remove();
        ConfirmationDialog::showMessage(
            window_, tr("Run and Debug"),
            removed ? tr("Could not write launch.json; the incomplete new file was removed: %1")
                          .arg(error)
                    : tr("Could not write launch.json; the incomplete new file could not be "
                         "removed: %1")
                          .arg(error));
        return;
    }
    file.close();
    emit openConfiguration(path);
}
} // namespace litecode::ui
