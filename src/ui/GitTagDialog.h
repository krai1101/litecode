#pragma once
class QWidget;
namespace litecode::workspace { class GitService; }
namespace litecode::ui {
enum class GitTagAction { Create, DeleteLocal, DeleteRemote, Push };
void requestGitTagAction(QWidget* owner, workspace::GitService* git, GitTagAction action);
}
