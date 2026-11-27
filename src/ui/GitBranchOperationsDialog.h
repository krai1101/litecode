#pragma once
class QWidget;
namespace litecode::workspace {
class GitService;
}
namespace litecode::ui {
enum class GitBranchAction { Merge, Rebase, Rename, DeleteLocal, DeleteRemote };
void requestGitBranchAction(QWidget* owner, workspace::GitService* git, GitBranchAction action);
} // namespace litecode::ui
