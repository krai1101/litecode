#pragma once
#include <QString>
#include <QVector>
class QPainter;
class QFontMetrics;
class QRect;
namespace litecode::ui {
struct ThemeTokens;
enum class GitBadgeKind { Current, Local, Remote, Tag };
struct GitReferenceBadge {
    GitBadgeKind kind;
    QString label;
    int count{1};
};
QVector<GitReferenceBadge> gitReferenceBadges(const QString& decorations);
int paintGitReferenceBadges(QPainter& painter, const QRect& row, const QFontMetrics& metrics,
                            const QVector<GitReferenceBadge>& badges, const ThemeTokens& theme,
                            int start, int end);
} // namespace litecode::ui
