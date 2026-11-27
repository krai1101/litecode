#include "workspace/GitService.h"
#include <QRegularExpression>

namespace litecode::workspace {
namespace {
bool validTagName(const QString& name) {
    return !name.isEmpty() && !name.contains(QChar('\0')) && !name.contains('\n') &&
           !name.contains('\r') && !name.startsWith('-');
}
bool validRemote(const QString& name) {
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return pattern.match(name).hasMatch();
}
} // namespace

void GitService::createTag(const QString& name, const QString& message) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validTagName(name) || message.contains(QChar('\0'))) {
        emit error(tr("Enter a valid tag name and message."));
        return;
    }
    run(repository_, {"check-ref-format", "refs/tags/" + name},
        [this, name, message](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Invalid tag name.") : diagnostic);
                return;
            }
            if (message.isEmpty())
                mutate({"tag", name});
            else
                mutate({"tag", "-a", name, "-m", message});
        });
}

void GitService::deleteTag(const QString& name) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validTagName(name)) {
        emit error(tr("Select a valid local tag."));
        return;
    }
    run(repository_, {"check-ref-format", "refs/tags/" + name},
        [this, name](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0)
                emit error(diagnostic.isEmpty() ? tr("Invalid tag name.") : diagnostic);
            else
                mutate({"tag", "-d", "--", name});
        });
}

void GitService::listRemoteTags(const QString& remote) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validRemote(remote)) {
        emit error(tr("Select a configured remote."));
        return;
    }
    run(repository_, {"ls-remote", "--tags", "--refs", remote},
        [this, remote](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Could not list remote tags.") : diagnostic);
                return;
            }
            QStringList names;
            for (const auto& row : output.split('\n')) {
                const auto ref = QString::fromUtf8(row.mid(row.indexOf('\t') + 1)).trimmed();
                if (row.indexOf('\t') > 0 && ref.startsWith("refs/tags/"))
                    names.append(ref.mid(10));
            }
            names.removeDuplicates();
            names.sort(Qt::CaseInsensitive);
            emit remoteTagsReady(remote, names);
        });
}

void GitService::deleteRemoteTag(const QString& remote, const QString& name) {
    if (!validRemote(remote) || !validTagName(name)) {
        emit error(tr("Select a remote and a valid tag."));
        return;
    }
    mutate({"push", remote, ":refs/tags/" + name});
}

void GitService::pushTags(const QString& remote) {
    if (!validRemote(remote)) {
        emit error(tr("Select a configured remote."));
        return;
    }
    mutate({"push", "--tags", remote});
}
} // namespace litecode::workspace
