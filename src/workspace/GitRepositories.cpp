#include "workspace/GitRepositories.h"
#include <QDir>
#include <QFileInfo>
#include <QQueue>
namespace litecode::workspace {
QStringList discoverGitRepositories(const QString& root, int maximumDepth, int maximumDirectories) {
    QStringList repositories;
    QQueue<QPair<QString, int>> pending;
    pending.enqueue({QFileInfo(root).absoluteFilePath(), 0});
    int visited = 0;
    while (!pending.isEmpty() && visited++ < maximumDirectories) {
        const auto entry = pending.dequeue();
        const QDir directory(entry.first);
        if (QFileInfo::exists(directory.filePath(".git")))
            repositories.append(QFileInfo(entry.first).canonicalFilePath());
        if (entry.second >= maximumDepth)
            continue;
        for (const auto& child : directory.entryInfoList(
                 QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            if (child.isSymLink() ||
                QStringList{".git", "node_modules", ".cache", ".venv", "vendor"}.contains(
                    child.fileName()))
                continue;
            pending.enqueue({child.absoluteFilePath(), entry.second + 1});
        }
    }
    repositories.removeAll({});
    repositories.removeDuplicates();
    repositories.sort();
    return repositories;
}
} // namespace litecode::workspace
