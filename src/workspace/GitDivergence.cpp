#include "workspace/GitService.h"
#include <QRegularExpression>
#include <memory>
namespace litecode::workspace {
void GitService::listDivergence() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_,
        {"for-each-ref", "--format=%(HEAD)%00%(objectname)%00%(upstream)", "refs/heads/"},
        [this](int code, const QByteArray& bytes, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            QString head, upstream;
            for (const auto& row : bytes.split('\n')) {
                const auto fields = row.split('\0');
                if (fields.size() == 3 && fields[0] == "*") {
                    head = QString::fromUtf8(fields[1]);
                    upstream = QString::fromUtf8(fields[2]);
                    break;
                }
            }
            if (head.isEmpty() || upstream.isEmpty()) {
                emit divergenceReady({});
                return;
            }
            auto result = std::make_shared<GitDivergence>();
            result->head = head;
            run(repository_, {"rev-parse", "--verify", upstream + "^{commit}"},
                [this, result](int resolved, const QByteArray& oid, const QString&) {
                    if (resolved != 0) {
                        emit divergenceReady({});
                        return;
                    }
                    result->upstream = QString::fromUtf8(oid).trimmed();
                    run(repository_,
                        {"rev-list", "--left-right", "--count",
                         result->head + "..." + result->upstream},
                        [this, result](int counted, const QByteArray& counts,
                                       const QString& detail) {
                            if (counted != 0) {
                                emit error(detail);
                                return;
                            }
                            const auto fields = QString::fromUtf8(counts).trimmed().split(
                                QRegularExpression("\\s+"));
                            if (fields.size() != 2) {
                                emit error(tr("Invalid Git divergence result."));
                                return;
                            }
                            result->outgoing = fields[0].toInt();
                            result->incoming = fields[1].toInt();
                            run(repository_, {"merge-base", result->head, result->upstream},
                                [this, result](int common, const QByteArray& base,
                                               const QString& message) {
                                    if (common != 0 && common != 1) {
                                        emit error(message);
                                        return;
                                    }
                                    if (common == 0)
                                        result->mergeBase = QString::fromUtf8(base).trimmed();
                                    emit divergenceReady(*result);
                                });
                        });
                });
        });
}
} // namespace litecode::workspace
