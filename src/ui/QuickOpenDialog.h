#pragma once

#include <QDialog>
#include <QIcon>
#include <QSet>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QListWidget;
class QShowEvent;
class QLabel;

namespace litecode::workspace {
class WorkspaceSearch;
struct SearchResult;
} // namespace litecode::workspace

namespace litecode::ui {

class PopupSurface;

struct QuickPickItem final {
    QString id;
    QString label;
    QString description;
    QString detail;
    QIcon icon;
    bool separator{}, picked{}, alwaysShow{};
};

class QuickOpenDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit QuickOpenDialog(QString workspaceRoot, workspace::WorkspaceSearch& search,
                             QWidget* parent = nullptr);
    QuickOpenDialog(const QString& title, const QString& placeholder, QStringList items,
                    QWidget* parent = nullptr);
    QuickOpenDialog(const QString& title, const QString& placeholder, QVector<QuickPickItem> items,
                    QString initialItemId = {}, QWidget* parent = nullptr);

    [[nodiscard]] QString selectedItemId() const;
    [[nodiscard]] QStringList selectedItemIds() const;
    void configureExtensionPicker(bool many, bool matchDescription, bool matchDetail,
                                  const QString& prompt);
    void setPickerItems(QVector<QuickPickItem> items);
    void setBusy(bool busy);

  signals:
    void fileSelected(const QString& filePath);
    void activeItemChanged(const QString& id);

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void refresh(const QString& query);
    void showResults(const QVector<workspace::SearchResult>& results);
    void activate();
    void toggleCurrentItem();
    void initialize(const QString& title, const QString& placeholder);
    void showStaticResults(const QString& query);
    void showExtensionResults(const QString& query);
    void updatePopupGeometry();

    QString workspaceRoot_;
    workspace::WorkspaceSearch* search_{};
    PopupSurface* surface_{};
    QLineEdit* query_{};
    QListWidget* results_{};
    QLabel* prompt_{};
    QLabel* title_{};
    QLabel* busy_{};
    QString currentQuery_;
    QStringList staticItems_;
    QVector<QuickPickItem> quickPickItems_;
    QString initialItemId_;
    QString selectedItemId_;
    QSet<QString> checkedIds_;
    bool extensionPicker_{}, many_{}, matchDescription_{}, matchDetail_{}, busyState_{};
};

} // namespace litecode::ui
