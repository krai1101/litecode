#pragma once
#include "extensions/ProgressApi.h"
#include <QJsonObject>
#include <QObject>

namespace litecode::extensions {
struct ProgressRuntime;
class HostProgress final : public QObject {
  public:
    explicit HostProgress(std::function<void(const QJsonObject&)> send);
    ~HostProgress() override;
    detail::ProgressSession begin(const ProgressOptions& options);
    void cancel(quint64 id);
    void shutdown();

  private:
    std::shared_ptr<ProgressRuntime> runtime_;
};
} // namespace litecode::extensions
