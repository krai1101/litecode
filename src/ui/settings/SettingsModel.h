#pragma once

#include <QString>
#include <QVector>

namespace litecode::ui::settings {

enum class EditorKind {
    FontFamily,
    FontWeight,
    FontSize,
    Boolean,
    Encoding,
};

struct Definition {
    QString id;
    QString title;
    QString description;
    QStringList keywords;
    EditorKind editorKind;
};

struct SectionDefinition {
    QString id;
    QString title;
    QVector<Definition> settings;
};

struct CategoryDefinition {
    QString id;
    QString title;
    QVector<SectionDefinition> sections;
};

// The settings UI is generated from this catalog. New settings belong here rather than in
// SettingsDialog's layout code; SettingsDialog only supplies renderers for editor kinds.
[[nodiscard]] const QVector<CategoryDefinition>& categories();

} // namespace litecode::ui::settings
