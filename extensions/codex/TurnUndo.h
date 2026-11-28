#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QStringList>
#include <QVector>
namespace litecode::codex {
class TurnUndo final {
  public:
    TurnUndo(const QString& diff, const QString& workspace, const QString& recordPath = {});
    static QString recordPath(const QString& workspace, const QString& threadId,
                              const QString& turnId);
    static bool captureBefore(const QString& recordPath, const QString& workspace,
                              const QJsonArray& changes);
    static QString recordedDiff(const QString& recordPath, const QString& workspace);
    bool available() const { return !files_.isEmpty() && reason_.isEmpty() && !used_; }
    QString reason() const { return reason_; }
    QStringList paths() const;
    struct Snapshot {
        QString path;
        QByteArray before, after;
    };
    QVector<Snapshot> snapshots() const;
    bool apply(QString* error);

  private:
    struct File {
        QString path, canonicalParent;
        QByteArray before, after;
        bool beforeExists{}, afterExists{};
    };
    QVector<File> files_;
    QString reason_;
    bool used_{};
    QString recordPath_, workspace_, diffHash_, diff_;
    bool save() const;
};
} // namespace litecode::codex
