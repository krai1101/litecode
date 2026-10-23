#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace litecode::workspace {

struct WorkspaceTextQuery final {
    QString query;
    bool matchCase{};
    bool matchWholeWord{};
    bool useRegularExpression{};
};

[[nodiscard]] QRegularExpression compileWorkspaceTextQuery(const WorkspaceTextQuery& query);
[[nodiscard]] QString expandWorkspaceReplacement(const QString& replacement,
                                                 const QStringList& captures,
                                                 bool useRegularExpression, bool preserveCase,
                                                 bool matchCase);

} // namespace litecode::workspace
