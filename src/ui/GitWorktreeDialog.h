#pragma once
class QWidget;
namespace litecode::workspace { class GitService; }
namespace litecode::ui {
void requestGitWorktree(QWidget* owner, workspace::GitService* git);
void requestGitWorktreeDeletion(QWidget* owner, workspace::GitService* git);
}
