#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QListWidget;
class QShowEvent;

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

  signals:
    void fileSelected(const QString& filePath);

  private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void refresh(const QString& query);
    void showResults(const QVector<workspace::SearchResult>& results);
    void activate();
    void initialize(const QString& title, const QString& placeholder);
    void showStaticResults(const QString& query);
    void updatePopupGeometry();

    QString workspaceRoot_;
    workspace::WorkspaceSearch* search_{};
    PopupSurface* surface_{};
    QLineEdit* query_{};
    QListWidget* results_{};
    QString currentQuery_;
    QStringList staticItems_;
    QVector<QuickPickItem> quickPickItems_;
    QString initialItemId_;
    QString selectedItemId_;
};

} // namespace litecode::ui
