#include "workspace/WorkspaceTextQuery.h"

namespace litecode::workspace {

QRegularExpression compileWorkspaceTextQuery(const WorkspaceTextQuery& query) {
    QString pattern =
        query.useRegularExpression ? query.query : QRegularExpression::escape(query.query);
    if (query.matchWholeWord) {
        pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
    }
    return QRegularExpression(pattern, query.matchCase ? QRegularExpression::NoPatternOption
                                                       : QRegularExpression::CaseInsensitiveOption);
}

QString expandWorkspaceReplacement(const QString& replacement, const QStringList& captures,
                                   bool useRegularExpression, bool preserveCase, bool matchCase) {
    QString expanded;
    if (useRegularExpression) {
        for (qsizetype index = 0; index < replacement.size(); ++index) {
            if (replacement.at(index) == QLatin1Char('$') && index + 1 < replacement.size()) {
                if (replacement.at(index + 1) == QLatin1Char('$')) {
                    expanded += QLatin1Char('$');
                    ++index;
                    continue;
                }
                qsizetype end = index + 1;
                while (end < replacement.size() && replacement.at(end).isDigit())
                    ++end;
                if (end > index + 1) {
                    const int capture = replacement.mid(index + 1, end - index - 1).toInt();
                    if (capture >= 0 && capture < captures.size())
                        expanded += captures.at(capture);
                    index = end - 1;
                    continue;
                }
            }
            expanded += replacement.at(index);
        }
    } else {
        expanded = replacement;
    }
    if (!preserveCase || matchCase || captures.isEmpty())
        return expanded;

    const QString& matched = captures.constFirst();
    if (matched == matched.toUpper())
        return expanded.toUpper();
    if (matched == matched.toLower())
        return expanded.toLower();
    if (!expanded.isEmpty() && !matched.isEmpty() && matched.front().isUpper() &&
        matched.mid(1) == matched.mid(1).toLower()) {
        expanded = expanded.toLower();
        expanded.front() = expanded.front().toUpper();
    }
    return expanded;
}

} // namespace litecode::workspace
