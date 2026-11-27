#pragma once
#include <QHash>
#include <QPointer>
#include <QUrl>
#include <QWidget>
class QComboBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QLabel;
namespace litecode::extensions {
class ExtensionManager;
}
namespace litecode::ui::chat {
class ParticipantResponseView;
class ParticipantChatPanel final : public QWidget {
    Q_OBJECT
  public:
    ParticipantChatPanel(extensions::ExtensionManager* manager, QWidget* parent = nullptr);
    ~ParticipantChatPanel() override;
    void clearSession();
  signals:
    void resourceActivated(const QUrl& uri, int line, int character);

  private:
    void refreshParticipants();
    void refreshCommands();
    void submit(const QString& prompt, const QString& participant = {},
                const QString& command = {});
    void updateActions();
    QPointer<extensions::ExtensionManager> manager_;
    QString session_;
    QComboBox* participants_;
    QComboBox* commands_;
    QLineEdit* input_;
    QPushButton* send_;
    QPushButton* stop_;
    QLabel* status_;
    QVBoxLayout* turns_;
    QHash<quint64, QPointer<ParticipantResponseView>> responses_;
    QHash<quint64, quint64> sequences_;
    quint64 active_{};
};
} // namespace litecode::ui::chat
