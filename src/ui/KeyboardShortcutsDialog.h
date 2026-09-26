#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QTableWidget;

namespace litecode::ui::components {
class Button;
}

namespace litecode::ui {

class CommandRegistry;

class KeyboardShortcutsDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit KeyboardShortcutsDialog(CommandRegistry& commands, QWidget* parent = nullptr);
    void refreshTheme();
    void reloadFromRegistry();

  signals:
    void shortcutChanged(const QString& id, const QKeySequence& sequence);
    void shortcutReset(const QString& id);

  private:
    struct Row final {
        QString id;
        QString title;
        QKeySequence sequence;
        QKeySequence defaultSequence;
        bool customized{};
    };

    [[nodiscard]] int selectedRow() const;
    void beginEditing(int row);
    void applySequence(int row, const QKeySequence& sequence);
    void removeSelectedShortcut();
    void resetSelectedShortcut();
    void refreshRow(int row);
    void refreshActions();
    void filterRows(const QString& query);
    void showConflict(const QString& commandTitle);
    void clearMessage();

    CommandRegistry& commands_;
    QVector<Row> rows_;
    QLineEdit* search_{};
    QTableWidget* table_{};
    QLabel* message_{};
    components::Button* changeButton_{};
    components::Button* removeButton_{};
    components::Button* resetButton_{};
};

} // namespace litecode::ui
