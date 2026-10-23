#pragma once

#include <QByteArray>
#include <QString>

namespace litecode::editor {

struct LanguageSupport {
    QString id;
    QByteArray lexer;
    bool wordWrapByDefault{false};
};

[[nodiscard]] LanguageSupport languageSupportForFile(const QString& filePath);

} // namespace litecode::editor
