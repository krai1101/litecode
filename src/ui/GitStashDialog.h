#pragma once
class QWidget;
namespace litecode::workspace {
class GitService;
}
namespace litecode::ui {
enum class GitStashAction {
    Create,
    CreateUntracked,
    CreateStaged,
    ApplyLatest,
    Apply,
    PopLatest,
    Pop,
    Drop,
    DropAll,
    View
};
void requestGitStashAction(QWidget* owner, workspace::GitService* git, GitStashAction action);
} // namespace litecode::ui
