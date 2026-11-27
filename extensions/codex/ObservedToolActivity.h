#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace litecode::codex {
// Consumes rollout records newest first, within a single turn. Tool-call status
// describes generation; only its matching output establishes execution completion.
class ObservedToolActivity final {
  public:
    void read(const QJsonObject& record);
    void readOutputPrefix(const QByteArray& prefix);
    QJsonArray pending() const;

  private:
    QSet<QString> completed_;
    QJsonArray pending_;
};
} // namespace litecode::codex
