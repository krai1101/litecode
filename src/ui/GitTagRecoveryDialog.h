#pragma once
class QWidget;
namespace litecode::workspace {
class GitService;
}
namespace litecode::ui {
void requestGitTagRecovery(QWidget* owner, workspace::GitService* git, bool restore = false);
}
