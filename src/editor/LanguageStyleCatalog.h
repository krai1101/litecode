#pragma once

#include "editor/SyntaxTheme.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace litecode::editor {

struct KeywordSet final {
    int index{};
    QByteArray words;
};

struct LexerStyleRule final {
    int style{};
    SyntaxRole role{SyntaxRole::Foreground};
    bool bold{};
    bool italic{};
    bool underline{};

    bool operator==(const LexerStyleRule&) const = default;
};

class LanguageStyleCatalog final {
  public:
    [[nodiscard]] static bool hasDefinition(const QString& languageId);
    [[nodiscard]] static QVector<KeywordSet> keywordSets(const QString& languageId);
    [[nodiscard]] static QVector<LexerStyleRule> styleRules(const QString& languageId);
    [[nodiscard]] static SyntaxRole roleForStyleMetadata(const QString& name, const QString& tags,
                                                         const QString& description);
};

} // namespace litecode::editor
