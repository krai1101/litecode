#include "ui/GitReferenceBadges.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include <QApplication>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
namespace litecode::ui {
QVector<GitReferenceBadge> gitReferenceBadges(const QString& decorations) {
    QStringList locals, remotes, tags;
    QString current;
    for (auto ref : decorations.split(", ", Qt::SkipEmptyParts)) {
        if (ref == "HEAD") {
            current = "HEAD";
            continue;
        }
        if (ref.startsWith("HEAD -> ")) {
            ref = ref.mid(8);
            current = ref.startsWith("refs/heads/") ? ref.mid(11) : ref;
            locals.append(current);
            continue;
        }
        if (ref.startsWith("refs/heads/"))
            locals.append(ref.mid(11));
        else if (ref.startsWith("refs/remotes/"))
            remotes.append(ref.mid(13));
        else if (ref.startsWith("tag: refs/tags/"))
            tags.append(ref.mid(15));
        else if (ref.startsWith("tag: "))
            tags.append(ref.mid(5));
    }
    locals.removeDuplicates();
    remotes.removeDuplicates();
    tags.removeDuplicates();
    QVector<GitReferenceBadge> result;
    if (!current.isEmpty()) {
        result.append({GitBadgeKind::Current, current});
        locals.removeAll(current);
    } else if (!locals.isEmpty())
        result.append({GitBadgeKind::Local, locals.takeFirst()});
    else if (!remotes.isEmpty())
        result.append({GitBadgeKind::Remote, remotes.takeFirst()});
    else if (!tags.isEmpty())
        result.append({GitBadgeKind::Tag, tags.takeFirst()});
    if (!locals.isEmpty())
        result.append({GitBadgeKind::Local, {}, int(locals.size())});
    if (!remotes.isEmpty())
        result.append({GitBadgeKind::Remote, {}, int(remotes.size())});
    if (!tags.isEmpty())
        result.append({GitBadgeKind::Tag, {}, int(tags.size())});
    return result;
}
int paintGitReferenceBadges(QPainter& painter, const QRect& row, const QFontMetrics& metrics,
                            const QVector<GitReferenceBadge>& badges, const ThemeTokens& theme,
                            int start, int end) {
    static QHash<QString, QIcon> icons;
    const int badgeBudget = qMax(0, (end - start) / 2);
    const int originalEnd = end;
    for (int index = badges.size() - 1; index >= 0; --index) {
        const auto& badge = badges[index];
        const int remaining = badgeBudget - (originalEnd - end);
        if (remaining < 24)
            continue;
        const auto label = !badge.label.isEmpty() ? metrics.elidedText(badge.label, Qt::ElideRight,
                                                                       qMax(0, remaining - 24))
                           : badge.count > 1      ? QString::number(badge.count)
                                                  : QString();
        const int width = 24 + metrics.horizontalAdvance(label);
        const QRect bounds(end - width, row.top() + 2, width, 18);
        const bool tag = badge.kind == GitBadgeKind::Tag;
        const bool remote = badge.kind == GitBadgeKind::Remote;
        const auto foreground = tag ? theme.foreground : theme.onAccent;
        const auto background =
            tag      ? theme.hover
            : remote ? QColor(qApp->property("litecodeDarkTheme").toBool() ? "#b180d7" : "#652d90")
                     : theme.accent;
        const auto resource = tag                                   ? ":/icons/tag.svg"
                              : remote                              ? ":/icons/cloud.svg"
                              : badge.kind == GitBadgeKind::Current ? ":/icons/target.svg"
                                                                    : ":/icons/git-branch.svg";
        const auto key = QString::fromLatin1(resource) + foreground.name(QColor::HexArgb);
        if (!icons.contains(key))
            icons.insert(key, tintedIcon(resource, foreground, foreground));
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(bounds, 8, 8);
        icons.value(key).paint(&painter, QRect(bounds.left() + 3, bounds.top() + 1, 16, 16));
        painter.setPen(foreground);
        painter.drawText(bounds.adjusted(21, 0, -3, 0), Qt::AlignVCenter, label);
        painter.restore();
        end -= width + 6;
    }
    return end;
}
} // namespace litecode::ui
