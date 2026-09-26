#pragma once

#include "core/DocumentTypes.h"
#include "ui/settings/SettingsModel.h"

#include <QDialog>
#include <QFont>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

class QCheckBox;
class QLabel;
class QStackedWidget;
class QListWidgetItem;
class QSpinBox;
class QShowEvent;
class QHideEvent;

namespace litecode::ui::components {
class ComboBox;
class List;
} // namespace litecode::ui::components

namespace litecode::ui {

class SettingsDialog final : public QDialog {
    Q_OBJECT

  public:
    SettingsDialog(const QFont& editorFont, bool autoGuessEncoding, core::TextEncoding fileEncoding,
                   QWidget* parent = nullptr);

    [[nodiscard]] QFont editorFont() const;
    [[nodiscard]] bool autoGuessEncoding() const;
    [[nodiscard]] core::TextEncoding fileEncoding() const;
    [[nodiscard]] bool hasPendingChanges() const;
    [[nodiscard]] QSet<QString> changedSettings() const;
    [[nodiscard]] QSet<QString> resetSettings() const;
    void setValues(const QFont& editorFont, bool autoGuessEncoding,
                   core::TextEncoding fileEncoding);
    void setUserEncodingError(bool error);
    void refreshTheme();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  signals:
    void valuesChanged();
    void settingChanged(const QString& id);
    void resetRequested(const QString& id);

  private:
    struct RenderedSetting {
        QString id;
        QString sectionKey;
        QString searchableText;
        QWidget* row{};
    };

    QWidget* createEditor(const settings::Definition& definition, QWidget* parent);
    QWidget* createSettingRow(const settings::Definition& definition, QWidget* parent);
    void buildSettingsTree(QWidget* content);
    void filterSettings(const QString& query);
    void showNavigationItem(QListWidgetItem* item);
    void setDraftValues(const QFont& editorFont, bool autoGuessEncoding,
                        core::TextEncoding fileEncoding);
    void updatePendingChange(const QString& id);
    void stageReset(const QString& id);
    QFont editorFont_;
    QFont savedEditorFont_;
    bool savedAutoGuessEncoding_{};
    core::TextEncoding savedFileEncoding_{core::TextEncoding::Utf8};
    QSet<QString> changedSettings_;
    QSet<QString> resetSettings_;
    components::ComboBox* editorFontFamily_{};
    components::ComboBox* editorFontWeight_{};
    QSpinBox* editorFontSize_{};
    QCheckBox* autoGuess_{};
    components::ComboBox* fileEncoding_{};
    QLabel* emptyState_{};
    QLabel* userEncodingError_{};
    components::List* navigation_{};
    QStackedWidget* pages_{};
    QWidget* emptyPage_{};
    QVector<RenderedSetting> renderedSettings_;
    QHash<QString, QListWidgetItem*> sectionItems_;
    QHash<QString, int> sectionPageIndexes_;
};

} // namespace litecode::ui
