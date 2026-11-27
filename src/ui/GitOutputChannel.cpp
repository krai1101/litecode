#include "GitOutputChannel.h"
#include "SourceControlPanel.h"
#include "extensions/WorkbenchOutput.h"

namespace litecode::ui {
namespace {
const QString owner = QStringLiteral("workbench.git");
}
QString GitOutputChannel::channelId() { return QStringLiteral("workbench.git.output"); }
GitOutputChannel::GitOutputChannel(extensions::WorkbenchOutput& output,
                                   SourceControlPanel& sourceControl, QObject* parent)
    : QObject(parent), output_(&output) {
    output.accept(owner, {{"id", channelId()}, {"operation", "create"}, {"name", "Git"},
                           {"languageId", "log"}, {"log", true},
                           {"level", int(extensions::LogLevel::Info)}});
    connect(&sourceControl, &SourceControlPanel::gitOutputLine, this,
            [this](const QString& line) { append(line); });
    connect(&sourceControl, &SourceControlPanel::gitOutputRequested, this, [this] {
        if (output_)
            output_->accept(owner, {{"id", channelId()}, {"operation", "show"},
                                    {"preserveFocus", false}});
    });
}
void GitOutputChannel::append(const QString& line) {
    if (!output_)
        return;
    output_->accept(owner, {{"id", channelId()}, {"operation", "append"},
                            {"text", line.left(4096) + '\n'}});
    const auto channel = output_->channel(channelId());
    if (channel && channel->text.count('\n') > 500) {
        const auto tail = channel->text.section('\n', -501);
        output_->accept(owner, {{"id", channelId()}, {"operation", "clear"}});
        for (qsizetype offset = 0; offset < tail.size(); offset += 32768)
            output_->accept(owner, {{"id", channelId()}, {"operation", "append"},
                                    {"text", tail.mid(offset, 32768)}});
    }
}
} // namespace litecode::ui
