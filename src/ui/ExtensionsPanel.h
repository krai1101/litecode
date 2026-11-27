#pragma once
#include "extensions/ExtensionManager.h"
#include <QWidget>
class QListWidget;
class QComboBox;
class QLineEdit;
class QLabel;
namespace litecode::ui {
class CommandRegistry;
class ExtensionDetailsPage;
class ExtensionsPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit ExtensionsPanel(CommandRegistry* commands, QWidget* parent = nullptr);
    void discover();
    void revealExtension(const QString& id);
    ExtensionDetailsPage* createDetailsPage(const QString& id);
    // Private workbench composition, never exposed to native extension plugins.
    extensions::ExtensionManager* manager() const { return manager_; }
    void setWorkspace(const QString& path);
    void setConfigurationWriteGuard(std::function<QString(const QString&)> guard);
    void activateForLanguage(const QString& languageId);
    QStringList commandIds() const;
    QString commandTitle(const QString& id) const;
    QVector<extensions::DebuggerContribution> debuggerContributions() const;
    quint64 requestDebugAdapter(const QString& type, const QJsonObject& configuration, const QString& sessionId = {},
        const QJsonValue& folderOverride = QJsonValue::Undefined);
    void executeCommand(const QString& id);
    QVector<extensions::FormatterContribution> formatterContributions(const QString& languageId) const;
    quint64 requestDocumentFormat(const QString& languageId, const QString& text,
                                  const QString& extensionId = {});
  signals:
    void openFileRequested(const QString& path);
    void detailsRequested(const QString& id);
    void commandsChanged(const QStringList& ids);
    void debuggersChanged();
    void enabledStateChanged();
    void nativeViewOpenRequested(const QString& viewId);
    void configurationFileUpdated(const QString& path);
    void configurationDiagnostic(const QString& path, const QString& error);
    void documentFormatResult(quint64 requestId, const QString& text, const QString& error);
    void debugAdapterResult(quint64 requestId, const QJsonObject& descriptor, const QString& error);

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void render();
    void chooseImport(bool folder);
    void importExtension(const QString& source);
    void changeEnabled(const QString& id, bool enabled);
    void uninstallExtension(const QString& id);
    QString directory_;
    bool importing_{};
    QLabel* importStatus_{};
    extensions::ExtensionManager* manager_{};
    QListWidget* list_{};
    QLineEdit* filter_{};
    QLabel* installed_{};
    QLabel* empty_{};
};
} // namespace litecode::ui
