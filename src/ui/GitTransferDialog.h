#pragma once

class QWidget;
namespace litecode::workspace {
class GitService;
}

namespace litecode::ui {
enum class GitTransferAction { PullFrom, PushTo, PushToForce };
void requestGitTransfer(QWidget* owner, workspace::GitService* git, GitTransferAction action,
                        bool confirmForcePush = true);
} // namespace litecode::ui
