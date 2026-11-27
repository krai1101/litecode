#pragma once
#include <QObject>
#include <QPointer>
namespace litecode::extensions {
class WorkbenchOutput;
}
namespace litecode::ui {
class SourceControlPanel;
// Built-in Git uses the same channel store and Output presentation as extensions.
class GitOutputChannel final : public QObject {
  public:
    GitOutputChannel(extensions::WorkbenchOutput& output, SourceControlPanel& sourceControl,
                     QObject* parent);
    static QString channelId();

  private:
    void append(const QString& line);
    QPointer<extensions::WorkbenchOutput> output_;
};
} // namespace litecode::ui
